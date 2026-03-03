#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    RValuePtr Builtin_Which(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("which() requires at least 1 argument");
        RValuePtr x = args[0];
        RValuePtr arr_ind_arg = GetArg(args, names, "arr.ind", 1, nullptr);
        bool arr_ind = arr_ind_arg ? IsTrue(arr_ind_arg) : false;
        
        // Collect indices where TRUE
        MiniVector<int> true_indices;
        for(int i=0; i<x->Length(); ++i) {
            int lv = AsLogicalAt(x, i);
            if (lv == 1) { // TRUE; NA treated as FALSE, like R
                 true_indices.push_back(i);
            }
        }
        
        if (arr_ind && x->attributes.count("dim")) {
            RValuePtr dim = x->attributes["dim"];
            if (dim->Length() >= 2) {
                int nr = dim->GetInt(0), nc = dim->GetInt(1);
                // Return matrix: n rows (one per TRUE), 2 cols (row, col indices, 1-based)
                // Column-major storage: push all row indices, then all col indices
                auto res = std::make_shared<RValue>(RType::INTEGER);
                MiniVector<int> rows, cols;
                for (int flat : true_indices) {
                    int r = (flat % nr) + 1;  // 1-based row
                    int c = (flat / nr) + 1;  // 1-based col
                    rows.push_back(r);
                    cols.push_back(c);
                }
                // Column-major: column 0 (all rows), then column 1 (all cols)
                for (int r : rows) res->i_vec.push_back(r);
                for (int c : cols) res->i_vec.push_back(c);
                auto new_dim = std::make_shared<RValue>(RType::INTEGER);
                new_dim->i_vec = {(int)true_indices.size(), 2};
                res->attributes["dim"] = new_dim;
                return res;
            }
        }
        
        // Default: flat indices (1-based)
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for (int i : true_indices) res->i_vec.push_back(i + 1);
        return res;
    }
    
    RValuePtr Builtin_WhichMin(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("which.min() requires at least 1 argument");
        RValuePtr x = args[0];
        int idx = -1;
        double min_val = 1e9;
        
        for(int i=0; i<x->Length(); ++i) {
             double d = x->GetDouble(i);
             // handle NA? R returns index of first min non-NA usually?
             if (d < min_val) {
                 min_val = d;
                 idx = i;
             }
        }
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        if (idx != -1) res->i_vec.push_back(idx + 1);
        return res;
    }
    
    RValuePtr Builtin_WhichMax(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("which.max() requires at least 1 argument");
        RValuePtr x = args[0];
        int idx = -1;
        double max_val = -1e9;
        
        for(int i=0; i<x->Length(); ++i) {
             double d = x->GetDouble(i);
             if (d > max_val) {
                 max_val = d;
                 idx = i;
             }
        }
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        if (idx != -1) res->i_vec.push_back(idx + 1);
        return res;
    }
    
    // --- MATRIX OPS ---
    RValuePtr Builtin_Bind(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env, bool col_bind) {
        // Collect all data and dims
        // Simplified: Assume all inputs are vectors or matrices
        // If vector, treat as nx1 (cbind) or 1xn (rbind)
        
        // Pass 1: Calc total rows/cols
        int total_rows = 0; // for rbind, this grows. For cbind, this is fixed max.
        int total_cols = 0; // for cbind, this grows.
        
        // Check consistency
        // For Cbind: Rows must match (recycling?)
        // For Rbind: Cols must match
        
        // Let's implement simple Cbind first (most common?)
        // Cbind: Result has max(rows) of inputs. Cols = sum(cols).
        
        int common_dim = 0; // Rows for Cbind, Cols for Rbind
        bool first = true;
        
        for(auto& arg : args) {
             int r=1, c=1;
             if (arg->attributes.count("dim")) {
                 r = arg->attributes["dim"]->GetInt(0);
                 c = arg->attributes["dim"]->GetInt(1);
             } else {
                 // Vector
                 r = arg->Length(); 
                 c = 1; 
             }
             
             if (col_bind) {
                 if (first) common_dim = r;
                 else if (r > common_dim) common_dim = r; // Grow to max
                 total_cols += c;
             } else {
                 // Rbind
                 // Vectors are treated as 1xN 
                 // Wait: rbind(1:3, 1:3) -> 2x3 matrix.
                 // So vector length is COLUMNS in rbind?
                 // R says: "vectors are treated as rows"
                 c = arg->Length(); 
                 r = 1;
                 
                 // If matrix, keep its c
                 if (arg->attributes.count("dim")) {
                      c = arg->attributes["dim"]->GetInt(1);
                      r = arg->attributes["dim"]->GetInt(0);
                 }
                 
                 if (first) common_dim = c; 
                 else if (c > common_dim) common_dim = c;
                 total_rows += r;
             }
             first = false;
        }
        
        // Allocate Result
        int n_rows = col_bind ? common_dim : total_rows;
        int n_cols = col_bind ? total_cols : common_dim;
        
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(n_rows * n_cols);
        
        // Fill
        int current_col_offset = 0;
        int current_row_offset = 0;
        
        for(auto& arg : args) {
            int r=0, c=0;
            // Re-resolve dims
            if (arg->attributes.count("dim")) {
                 r = arg->attributes["dim"]->GetInt(0);
                 c = arg->attributes["dim"]->GetInt(1);
            } else {
                 if (col_bind) { r = arg->Length(); c = 1; }
                 else { r = 1; c = arg->Length(); }
            }
            
            // Copy data
            for(int j=0; j<c; ++j) {
                for(int i=0; i<r; ++i) {
                     // Source Index (Col-Major)
                     int src_idx = j*r + i;
                     double val = arg->GetDouble(src_idx);
                     
                     // Dest Index
                     int dest_r = col_bind ? i : current_row_offset + i;
                     int dest_c = col_bind ? current_col_offset + j : j;
                     
                     // Recycle row index for cbind?
                     if (col_bind) dest_r = dest_r % n_rows;
                     // Recycle col index for rbind?
                     if (!col_bind) dest_c = dest_c % n_cols;
                     
                     int dest_idx = dest_c * n_rows + dest_r; // Col-Major dst
                     res->d_vec[dest_idx] = val;
                }
            }
            
            if (col_bind) current_col_offset += c;
            else current_row_offset += r;
        }
        
        // Set Dim
        auto dim = std::make_shared<RValue>(RType::INTEGER);
        dim->i_vec = {n_rows, n_cols};
        res->attributes["dim"] = dim;
        
        return res;
    }
    
    RValuePtr Builtin_Rbind(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Bind(args, names, env, false); }
    RValuePtr Builtin_Cbind(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Bind(args, names, env, true); }
    
    // t(x)
    RValuePtr Builtin_Transpose(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("t() requires at least 1 argument");
        RValuePtr x = args[0];
        
        int nr = 1, nc = x->Length();
        if (x->attributes.count("dim")) {
            nr = x->attributes["dim"]->GetInt(0);
            nc = x->attributes["dim"]->GetInt(1);
        }
        
        auto res = std::make_shared<RValue>(RType::DOUBLE); // Simplify type
        res->d_vec.resize(nr * nc);
        
        for(int r=0; r<nr; ++r) {
            for(int c=0; c<nc; ++c) {
                // Orig: [r, c] -> flat c*nr + r
                // Dest: [c, r] -> flat r*nc + c
                res->d_vec[r*nc + c] = x->GetDouble(c*nr + r);
            }
        }
        
        auto dim = std::make_shared<RValue>(RType::INTEGER);
        dim->i_vec = {nc, nr};
        res->attributes["dim"] = dim;
        return res;
    }
    
    // %*%
    RValuePtr Builtin_MatMult(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.size() < 2) return RR_Error("Need 2 args for %*%");
        RValuePtr A = args[0];
        RValuePtr B = args[1];
        
        int ar=1, ac=A->Length();
        if (A->attributes.count("dim")) { ar = A->attributes["dim"]->GetInt(0); ac = A->attributes["dim"]->GetInt(1); }
        
        int br=B->Length(), bc=1;
        if (B->attributes.count("dim")) { br = B->attributes["dim"]->GetInt(0); bc = B->attributes["dim"]->GetInt(1); }
        
        if (ac != br) return RR_Error("Non-conformable arrays");
        
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(ar * bc);
        
        for(int i=0; i<ar; ++i) {
            for(int j=0; j<bc; ++j) {
                double sum = 0;
                for(int k=0; k<ac; ++k) {
                    // A[i, k] * B[k, j]
                    double av = A->GetDouble(k*ar + i);
                    double bv = B->GetDouble(j*br + k);
                    sum += av * bv;
                }
                res->d_vec[j*ar + i] = sum;
            }
        }
        
        auto dim = std::make_shared<RValue>(RType::INTEGER);
        dim->i_vec = {ar, bc};
        res->attributes["dim"] = dim;
        return res;
    }
    
    // row(x), col(x)
    RValuePtr Builtin_Row(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty() || !args[0]->attributes.count("dim")) return RR_Error("row expects matrix");
        int nr = args[0]->attributes["dim"]->GetInt(0);
        int nc = args[0]->attributes["dim"]->GetInt(1);
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.resize(nr*nc);
        for(int c=0; c<nc; ++c) {
            for(int r=0; r<nr; ++r) res->i_vec[c*nr + r] = r + 1;
        }
        res->attributes["dim"] = args[0]->attributes["dim"];
        return res;
    }
    RValuePtr Builtin_Col(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty() || !args[0]->attributes.count("dim")) return RR_Error("col expects matrix");
        int nr = args[0]->attributes["dim"]->GetInt(0);
        int nc = args[0]->attributes["dim"]->GetInt(1);
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.resize(nr*nc);
        for(int c=0; c<nc; ++c) {
            for(int r=0; r<nr; ++r) res->i_vec[c*nr + r] = c + 1;
        }
        res->attributes["dim"] = args[0]->attributes["dim"];
        return res;
    }
    
    // factor(x)
    RValuePtr Builtin_Factor(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("factor() requires at least 1 argument");
        RValuePtr x = args[0];
        // Simplified: Strings -> Integers + Levels
        // 1. Collect unique strings
        MiniVector<MiniString> raw;
        for(int i=0; i<x->Length(); ++i) {
             if (x->type == RType::CHARACTER) raw.push_back(x->s_vec[i]);
             else if (x->type == RType::LOGICAL) raw.push_back(x->i_vec[i] == 1 ? "TRUE" : "FALSE");
             else raw.push_back(MiniToString(x->GetDouble(i)));
        }
        
        MiniVector<MiniString> levels = raw;
        std::sort(levels.begin(), levels.end());
        levels.erase(std::unique(levels.begin(), levels.end()), levels.end());
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for(auto& s : raw) {
            auto it = std::lower_bound(levels.begin(), levels.end(), s);
            res->i_vec.push_back((int)(it - levels.begin()) + 1);
        }
        
        auto lev = std::make_shared<RValue>(RType::CHARACTER);
        lev->s_vec = levels;
        res->attributes["levels"] = lev;
        
        auto cls = std::make_shared<RValue>(RType::CHARACTER);
        cls->s_vec.push_back("factor");
        res->attributes["class"] = cls;
        
        return res;
    }
    
    // list(...) - named or positional list
    RValuePtr Builtin_List(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        auto res = std::make_shared<RValue>(RType::LIST);
        for (size_t i = 0; i < args.size(); ++i) {
            res->l_vec.push_back(args[i]);
        }
        if (!names.empty() && names.size() >= res->l_vec.size()) {
            auto names_vec = std::make_shared<RValue>(RType::CHARACTER);
            for (size_t i = 0; i < res->l_vec.size(); ++i) {
                names_vec->s_vec.push_back(i < names.size() && !names[i].empty() ? names[i] : "");
            }
            res->attributes["names"] = names_vec;
        }
        return res;
    }
    
    // data.frame(...) - Simplified to List + Class
    RValuePtr Builtin_DataFrame(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        // Reuse list logic but set class
        RValuePtr l = Builtin_List(args, names, env);
        auto cls = std::make_shared<RValue>(RType::CHARACTER);
        cls->s_vec.push_back("data.frame");
        l->attributes["class"] = cls;
        
        // Row names (1..N)
        if (!l->l_vec.empty()) {
            int n = l->l_vec[0]->Length();
            auto rn = std::make_shared<RValue>(RType::INTEGER);
            for(int i=1;i<=n;++i) rn->i_vec.push_back(i); // Simplified
            l->attributes["row.names"] = rn;
        }
        return l;
    }
    
    // as.matrix(x)
    RValuePtr Builtin_AsMatrix(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("as.matrix() requires at least 1 argument");
        RValuePtr x = args[0];
        if (x->attributes.count("dim")) return x; // already matrix?

        // data.frame -> matrix: coerce columns to common type and bind as columns
        if (x->type == RType::LIST && HasClass(x, "data.frame")) {
            int nc = (int)x->l_vec.size();
            int nr = (nc > 0 && x->l_vec[0]) ? x->l_vec[0]->Length() : 0;

            bool any_char = false;
            bool any_double = false;
            for (int c = 0; c < nc; ++c) {
                if (!x->l_vec[c]) continue;
                any_char |= (x->l_vec[c]->type == RType::CHARACTER);
                any_double |= (x->l_vec[c]->type == RType::DOUBLE);
            }

            if (any_char) {
                auto res = std::make_shared<RValue>(RType::CHARACTER);
                res->s_vec.resize(nr * nc);
                for (int c = 0; c < nc; ++c) {
                    RValuePtr col = x->l_vec[c];
                    for (int r = 0; r < nr; ++r) {
                        MiniString out = "NA";
                        if (!col || r >= col->Length()) out = "NA";
                        else if (col->type == RType::CHARACTER) out = col->s_vec[r];
                        else if (col->type == RType::INTEGER) out = (col->i_vec[r] == R_INT_NA) ? "NA" : MiniToString(col->i_vec[r]);
                        else if (col->type == RType::LOGICAL) {
                            int v = col->i_vec[r];
                            out = (v == R_LOGICAL_NA) ? "NA" : (v ? "TRUE" : "FALSE");
                        } else if (col->type == RType::DOUBLE) {
                            double d = col->d_vec[r];
                            if (IsNAReal(d)) out = "NA";
                            else if (std::isnan(d)) out = "NaN";
                            else {
                                out = MiniToString(d);
                            }
                        }
                        res->s_vec[c * nr + r] = out; // column-major
                    }
                }
                auto dim = std::make_shared<RValue>(RType::INTEGER);
                dim->i_vec = {nr, nc};
                res->attributes["dim"] = dim;
                return res;
            }

            // numeric matrix: DOUBLE (also coerces logical to 1/0)
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            res->d_vec.resize(nr * nc);
            for (int c = 0; c < nc; ++c) {
                RValuePtr col = x->l_vec[c];
                for (int r = 0; r < nr; ++r) {
                    double out = NAReal();
                    if (!col || r >= col->Length()) out = NAReal();
                    else if (col->type == RType::DOUBLE) out = col->d_vec[r];
                    else if (col->type == RType::INTEGER) out = (col->i_vec[r] == R_INT_NA) ? NAReal() : (double)col->i_vec[r];
                    else if (col->type == RType::LOGICAL) {
                        int v = col->i_vec[r];
                        out = (v == R_LOGICAL_NA) ? NAReal() : (v ? 1.0 : 0.0);
                    } else out = col->GetDouble(r);
                    res->d_vec[c * nr + r] = out; // column-major
                }
            }
            auto dim = std::make_shared<RValue>(RType::INTEGER);
            dim->i_vec = {nr, nc};
            res->attributes["dim"] = dim;
            return res;
        }
        
        // Wrap with dim
        // Copy x
        auto res = std::make_shared<RValue>(x->type); // naive copy constructor needed or manual
        if (x->type == RType::DOUBLE) res->d_vec = x->d_vec;
        if (x->type == RType::INTEGER) res->i_vec = x->i_vec;
        if (x->type == RType::CHARACTER) res->s_vec = x->s_vec;
        if (x->type == RType::LOGICAL) res->i_vec = x->i_vec;
        
        int n = x->Length();
        auto dim = std::make_shared<RValue>(RType::INTEGER);
        dim->i_vec = {n, 1};
        res->attributes["dim"] = dim;
        return res;
    }

    RValuePtr Builtin_PMax(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("pmax() requires at least 1 argument");
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        
        // Find max length
        int N = 0;
        for(auto& a : args) if (a->Length() > N) N = a->Length();
        
        for(int i=0; i<N; ++i) {
            double m = -1e9;
            for(auto& a : args) {
                // skip named args like na.rm
                if (a->Length() == 0) continue;
                double val = a->GetDouble(i % a->Length());
                if (val > m) m = val;
            }
            res->d_vec.push_back(m);
        }
        return res;
    }

    RValuePtr Builtin_PMin(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("pmin() requires at least 1 argument");
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        
        // Find max length
        int N = 0;
        for(auto& a : args) if (a->Length() > N) N = a->Length();
        
        for(int i=0; i<N; ++i) {
            double m = 1e9;
            for(auto& a : args) {
                if (a->Length() == 0) continue;
                double val = a->GetDouble(i % a->Length());
                if (val < m) m = val;
            }
            res->d_vec.push_back(m);
        }
        return res;
    }

    RValuePtr Builtin_Summary(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env, int op) {
        // na.rm is 2nd param (e.g. sum(x, na.rm=FALSE)); don't use last arg position or we steal the only arg for mean(x)
        RValuePtr narm_arg = GetArg(args, names, "na.rm", 1, nullptr);
        bool na_rm = narm_arg ? IsTrue(narm_arg) : false;
        
        double sum = 0;
        double min_val = std::numeric_limits<double>::infinity();
        double max_val = -std::numeric_limits<double>::infinity();
        bool any_val = false;
        bool all_val = true;
        int count = 0;   // number of non-NA values (for na_rm)
        bool seen_na = false;
        
        for(size_t k=0; k<args.size(); ++k) {
             if (args[k] == narm_arg) continue;
             
             RValuePtr v = args[k];
             int len = v->Length();
             for(int i=0; i<len; ++i) {
                 double d = v->GetDouble(i);
                 bool is_na = std::isnan(d);
                 
                 if (is_na) {
                     if (na_rm) continue;
                     if (op == 3 || op == 4) seen_na = true;  // any/all: don't break, keep scanning
                     else { seen_na = true; break; }
                 } else {
                 count++;
                 if (op == 0) sum += d;
                 if (op == 1) if (d > max_val) max_val = d;
                 if (op == 2) if (d < min_val) min_val = d;
                 if (op == 3) if (d != 0) any_val = true;
                 if (op == 4) if (d == 0) all_val = false;
                 }
             }
             if (seen_na && !na_rm && op != 3 && op != 4) break;
        }
        
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        if (op == 0) {
             if (seen_na && !na_rm) res->d_vec.push_back(NAReal());
             else res->d_vec.push_back(sum);
        } else if (op == 1) {
             if (seen_na && !na_rm) res->d_vec.push_back(NAReal());
             else if (na_rm && count == 0) res->d_vec.push_back(-std::numeric_limits<double>::infinity());
             else res->d_vec.push_back(max_val);
        } else if (op == 2) {
             if (seen_na && !na_rm) res->d_vec.push_back(NAReal());
             else if (na_rm && count == 0) res->d_vec.push_back(std::numeric_limits<double>::infinity());
             else res->d_vec.push_back(min_val);
        } else if (op == 3 || op == 4) {
             res->type = RType::LOGICAL;
             // any: TRUE if any TRUE; FALSE if all FALSE; NA only if no TRUE but has NA
             // all: FALSE if any FALSE; TRUE if all TRUE; NA only if no FALSE but has NA
             if (op == 3) res->i_vec.push_back(any_val ? 1 : (seen_na ? R_LOGICAL_NA : 0));
             else res->i_vec.push_back(!all_val ? 0 : (seen_na ? R_LOGICAL_NA : 1));
             res->d_vec.clear();
        }
        
        return res;
    }
    
    RValuePtr Builtin_Sum(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 0); }
    RValuePtr Builtin_Max(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 1); }
    RValuePtr Builtin_Min(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 2); }
    RValuePtr Builtin_Any(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 3); }
    RValuePtr Builtin_All(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 4); }
    
    RValuePtr Builtin_Mean(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("mean() requires at least 1 argument");
        RValuePtr narm_arg = GetArg(args, names, "na.rm", 1, nullptr);
        bool na_rm = narm_arg ? IsTrue(narm_arg) : false;
        RValuePtr s = Builtin_Sum(args, names, env);
        if (s->type == RType::ERROR) return s;
        double sum = s->GetDouble(0);
        int count = 0;
        if (na_rm) {
            for (size_t k = 0; k < args.size(); ++k) {
                if (args[k] == narm_arg) continue;
                RValuePtr v = args[k];
                for (int i = 0; i < v->Length(); ++i)
                    if (!std::isnan(v->GetDouble(i))) count++;
            }
        } else {
            for (auto& arg : args) if (arg != narm_arg) count += arg->Length();
        }
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.push_back(count > 0 ? sum / count : NAReal());
        return res;
    }

    // sort(x, decreasing = FALSE, na.last = TRUE, ...)  na.last: TRUE=last, FALSE=first, NA=remove
    RValuePtr Builtin_Sort(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("sort() requires at least 1 argument");
        RValuePtr x = args[0];
        RValuePtr dec_arg = GetArg(args, names, "decreasing", 1, nullptr);
        RValuePtr na_last_arg = GetArg(args, names, "na.last", 2, nullptr);
        bool decreasing = dec_arg ? IsTrue(dec_arg) : false;
        // na.last: 1 = last (default), 0 = first, -1 = remove
        int na_last = 1;
        if (na_last_arg && na_last_arg->Length() > 0) {
            if (na_last_arg->type == RType::LOGICAL && na_last_arg->i_vec[0] == R_LOGICAL_NA) na_last = -1;
            else if (na_last_arg->type == RType::INTEGER && na_last_arg->i_vec[0] == R_INT_NA) na_last = -1;
            else if (na_last_arg->type == RType::DOUBLE && std::isnan(na_last_arg->GetDouble(0))) na_last = -1;
            else na_last = IsTrue(na_last_arg) ? 1 : 0;
        }
        int n = x->Length();
        auto res = std::make_shared<RValue>(x->type);
        if (x->type == RType::CHARACTER) {
            MiniVector<std::pair<MiniString, int>> paired;
            for (int i = 0; i < n; ++i) paired.push_back({x->s_vec[i], i});
            bool na_at_end = (na_last == 1);
            auto cmp = [decreasing, na_at_end](const std::pair<MiniString, int>& a, const std::pair<MiniString, int>& b) {
                bool a_na = (a.first.empty() || a.first == "NA"), b_na = (b.first.empty() || b.first == "NA");
                if (a_na && b_na) return false;
                if (a_na) return !na_at_end;   // a "less than" b only when we want NA first
                if (b_na) return na_at_end;    // a "less than" b (NA) when we want NA last
                return decreasing ? (a.first > b.first) : (a.first < b.first);
            };
            std::stable_sort(paired.begin(), paired.end(), cmp);
            for (int i = 0; i < n; ++i) {
                if (na_last == -1 && (paired[i].first.empty() || paired[i].first == "NA")) continue;
                res->s_vec.push_back(x->s_vec[paired[i].second]);
            }
            return res;
        }
        MiniVector<std::pair<double, int>> paired;
        for (int i = 0; i < n; ++i) paired.push_back({x->GetDouble(i), i});
        if (na_last == -1) {
            paired.erase(std::remove_if(paired.begin(), paired.end(), [](const std::pair<double, int>& p) { return std::isnan(p.first); }), paired.end());
        }
        bool na_at_end = (na_last == 1);
        auto cmp = [decreasing, na_at_end](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            bool a_na = std::isnan(a.first), b_na = std::isnan(b.first);
            if (a_na && b_na) return false;
            if (a_na) return !na_at_end;  // a "less than" b only when we want NA first
            if (b_na) return na_at_end;    // a "less than" b (NA) when we want NA last
            return decreasing ? (a.first > b.first) : (a.first < b.first);
        };
        std::stable_sort(paired.begin(), paired.end(), cmp);
        for (size_t i = 0; i < paired.size(); ++i) {
            int idx = paired[i].second;
            if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
            else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[idx]);
            else if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[idx]);
        }
        return res;
    }

    // order(...) - returns integer permutation (1-based) so x[order(x)] is sorted
    RValuePtr Builtin_Order(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("order() requires at least 1 argument");
        RValuePtr x = args[0];
        RValuePtr dec_arg = GetArg(args, names, "decreasing", 1, nullptr);
        bool decreasing = dec_arg ? IsTrue(dec_arg) : false;
        int n = x->Length();
        MiniVector<std::pair<double, int>> paired;
        for (int i = 0; i < n; ++i) paired.push_back({x->GetDouble(i), i});
        auto cmp = [decreasing](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            bool a_na = std::isnan(a.first), b_na = std::isnan(b.first);
            if (a_na && b_na) return a.second < b.second;
            if (a_na) return false;
            if (b_na) return true;
            if (a.first != b.first) return decreasing ? (a.first > b.first) : (a.first < b.first);
            return a.second < b.second;
        };
        std::stable_sort(paired.begin(), paired.end(), cmp);
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for (int i = 0; i < n; ++i) res->i_vec.push_back(paired[i].second + 1);
        return res;
    }

    // rank(x, na.last = TRUE, ties.method = "average")  ties.method: "average", "min", "max", "first"
    RValuePtr Builtin_Rank(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("rank() requires at least 1 argument");
        RValuePtr x = args[0];
        RValuePtr ties_arg = GetArg(args, names, "ties.method", 2, nullptr);
        MiniString ties_method = "average";
        if (ties_arg && ties_arg->type == RType::CHARACTER && ties_arg->Length() > 0)
            ties_method = ties_arg->s_vec[0];
        int n = x->Length();
        MiniVector<std::pair<double, int>> paired;
        for (int i = 0; i < n; ++i) paired.push_back({x->GetDouble(i), i});
        auto cmp = [](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            if (std::isnan(a.first) && std::isnan(b.first)) return a.second < b.second;
            if (std::isnan(a.first)) return false;
            if (std::isnan(b.first)) return true;
            return a.first < b.first;
        };
        std::stable_sort(paired.begin(), paired.end(), cmp);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(n);
        for (int i = 0; i < n; ++i) {
            if (std::isnan(paired[i].first)) {
                res->d_vec[paired[i].second] = NAReal();
                continue;
            }
            int j = i;
            while (j + 1 < n && !std::isnan(paired[j+1].first) && paired[j+1].first == paired[i].first) ++j;
            double rank_val;
            if (ties_method == "min" || ties_method == "first")
                rank_val = i + 1;  // 1-based minimum rank
            else if (ties_method == "max")
                rank_val = j + 1;
            else
                rank_val = (i + j + 2) / 2.0;  // average
            if (ties_method == "first") {
                for (int k = i; k <= j; ++k) res->d_vec[paired[k].second] = (k - i) + (i + 1);
            } else {
                for (int k = i; k <= j; ++k) res->d_vec[paired[k].second] = rank_val;
            }
            i = j;
        }
        return res;
    }

    // max.col(m, ties.method = "random") - index of max in each row (1-based)
    RValuePtr Builtin_MaxCol(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty() || !args[0]->attributes.count("dim")) return RR_Nil();
        RValuePtr m = args[0];
        int nr = m->attributes["dim"]->GetInt(0);
        int nc = m->attributes["dim"]->GetInt(1);
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for (int r = 0; r < nr; ++r) {
            int best_j = 0;
            double best = m->GetDouble(0 * nr + r);
            for (int c = 1; c < nc; ++c) {
                double v = m->GetDouble(c * nr + r);
                if (std::isnan(best) || (!std::isnan(v) && v > best)) { best = v; best_j = c; }
            }
            res->i_vec.push_back(best_j + 1);
        }
        return res;
    }

    // rowSums(x, na.rm = FALSE) / colSums(x, na.rm = FALSE)
    static RValuePtr Builtin_RowColOp(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env, bool row_op, bool do_mean) {
        if (args.empty()) return RR_Error("row/col op requires at least 1 argument");
        RValuePtr x = args[0];
        RValuePtr narm_arg = GetArg(args, names, "na.rm", 1, nullptr);
        bool na_rm = narm_arg ? IsTrue(narm_arg) : false;
        int nr = 1, nc = x->Length();
        if (x->attributes.count("dim")) {
            nr = x->attributes["dim"]->GetInt(0);
            nc = x->attributes["dim"]->GetInt(1);
        }
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        if (row_op) {
            for (int r = 0; r < nr; ++r) {
                double sum = 0;
                int count = 0;
                bool seen_na = false;
                for (int c = 0; c < nc; ++c) {
                    double v = x->GetDouble(c * nr + r);
                    if (std::isnan(v)) { if (!na_rm) { seen_na = true; break; } continue; }
                    sum += v; count++;
                }
                if (seen_na) res->d_vec.push_back(NAReal());
                else if (do_mean && count > 0) res->d_vec.push_back(sum / count);
                else if (do_mean && count == 0) res->d_vec.push_back(NAReal());
                else res->d_vec.push_back(sum);
            }
        } else {
            for (int c = 0; c < nc; ++c) {
                double sum = 0;
                int count = 0;
                bool seen_na = false;
                for (int r = 0; r < nr; ++r) {
                    double v = x->GetDouble(c * nr + r);
                    if (std::isnan(v)) { if (!na_rm) { seen_na = true; break; } continue; }
                    sum += v; count++;
                }
                if (seen_na) res->d_vec.push_back(NAReal());
                else if (do_mean && count > 0) res->d_vec.push_back(sum / count);
                else if (do_mean && count == 0) res->d_vec.push_back(NAReal());
                else res->d_vec.push_back(sum);
            }
        }
        return res;
    }
    RValuePtr Builtin_RowSums(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, true, false); }
    RValuePtr Builtin_ColSums(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, false, false); }
    RValuePtr Builtin_RowMeans(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, true, true); }
    RValuePtr Builtin_ColMeans(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, false, true); }

    // x$name - extract element by name from list/data.frame
    RValuePtr Builtin_Dollar(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("$ requires 2 arguments");
        RValuePtr x = args[0];
        RValuePtr name_val = args[1];
        if (x->type != RType::LIST && !x->attributes.count("class")) return RR_Nil();
        MiniString key;
        if (name_val->type == RType::SYMBOL) key = name_val->sym_name;
        else if (name_val->type == RType::CHARACTER && name_val->Length() > 0) key = name_val->s_vec[0];
        else return RR_Nil();
        if (!x->attributes.count("names")) return RR_Nil();
        RValuePtr names_attr = x->attributes["names"];
        for (size_t i = 0; i < x->l_vec.size() && i < (size_t)names_attr->Length(); ++i) {
            if (names_attr->s_vec[i] == key) return x->l_vec[i];
        }
        return RR_Nil();
    }


} // namespace Evaluator
