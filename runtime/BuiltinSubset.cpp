#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    RValuePtr Builtin_Head(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("head() requires at least 1 argument");
        RValuePtr x = args[0];
        int n = 6;
        if (args.size() > 1) n = args[1]->GetInt(0);
        
        if (n > x->Length()) n = x->Length(); // Clamp
        
        // Subset x[1:n] logic
        // For now, assume vector. Matrix head usually heads rows.
        if (x->attributes.count("dim")) {
            int nr = x->attributes["dim"]->GetInt(0);
            int nc = x->attributes["dim"]->GetInt(1);
            if (n > nr) n = nr;
            int nrows = n;
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            for (int c = 0; c < nc; ++c)
                for (int r = 0; r < nrows; ++r)
                    res->d_vec.push_back(x->GetDouble(c * nr + r));
            auto dim = std::make_shared<RValue>(RType::INTEGER);
            dim->i_vec = {nrows, nc};
            res->attributes["dim"] = dim;
            return res;
        }
        
        auto res = std::make_shared<RValue>(x->type);
        for(int i=0; i<n; ++i) {
             if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
             else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[i]);
             else if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
             else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
             else if (x->type == RType::LIST) res->l_vec.push_back(x->l_vec[i]);
        }
        return res;
    }
    
    RValuePtr Builtin_Tail(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("tail() requires at least 1 argument");
        RValuePtr x = args[0];
        int n = 6;
        if (args.size() > 1) n = args[1]->GetInt(0);
        
        if (x->attributes.count("dim")) {
            int nr = x->attributes["dim"]->GetInt(0);
            int nc = x->attributes["dim"]->GetInt(1);
            if (n > nr) n = nr;
            int start_row = nr - n;
            if (start_row < 0) start_row = 0;
            int nrows = nr - start_row;
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            for (int c = 0; c < nc; ++c)
                for (int r = start_row; r < nr; ++r)
                    res->d_vec.push_back(x->GetDouble(c * nr + r));
            auto dim = std::make_shared<RValue>(RType::INTEGER);
            dim->i_vec = {nrows, nc};
            res->attributes["dim"] = dim;
            return res;
        }
        
        int len = x->Length();
        if (n > len) n = len;
        int start = len - n;
        auto res = std::make_shared<RValue>(x->type);
        for(int i=start; i<len; ++i) {
             if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
             else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[i]);
             else if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
             else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
             else if (x->type == RType::LIST) res->l_vec.push_back(x->l_vec[i]);
        }
        return res;
    }

    // matrix(data = NA, nrow = 1, ncol = 1, byrow = FALSE, dimnames = NULL)
    RValuePtr Builtin_Matrix(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        RValuePtr data = GetArg(args, names, "data", 0);
        RValuePtr nrow_arg = GetArg(args, names, "nrow", 1);
        RValuePtr ncol_arg = GetArg(args, names, "ncol", 2);
        RValuePtr byrow_arg = GetArg(args, names, "byrow", 3, nullptr);
        bool byrow = byrow_arg ? IsTrue(byrow_arg) : false;
        
        // Data defaults
        if (!data) { 
            data = std::make_shared<RValue>(RType::LOGICAL); 
            data->i_vec.push_back(R_LOGICAL_NA);
        }
        
        int nr = nrow_arg ? nrow_arg->GetInt(0) : 1;
        int nc = ncol_arg ? ncol_arg->GetInt(0) : 1;
        
        // Infer dimensions if missing
        if (!nrow_arg && !ncol_arg) {
            nr = data->Length();
            nc = 1;
        } else if (!nrow_arg && ncol_arg) {
            nr = (int)ceil((double)data->Length() / nc);
        } else if (nrow_arg && !ncol_arg) {
            nc = (int)ceil((double)data->Length() / nr);
        }
        
        // Create Matrix ID
        auto res = std::make_shared<RValue>(RType::DOUBLE); // Force double for now
        // Populate
        int total = nr * nc;
        res->d_vec.resize(total);
        
        int dlen = data->Length();
        if (dlen == 0) {
            for (int i = 0; i < total; ++i) res->d_vec[i] = NAReal(); // NA_real_
        } else if (byrow) {
            // Fill row-by-row from data, but store column-major (R's storage is always column-major)
            // Position (r,c) in matrix is stored at index c*nr + r
            int idx = 0;
            for (int r = 0; r < nr; ++r) {
                for (int c = 0; c < nc; ++c) {
                    res->d_vec[c * nr + r] = data->GetDouble(idx % dlen);
                    idx++;
                }
            }
        } else {
            // Fill Column Major (default): flat = c * nr + r
            for (int i = 0; i < total; ++i)
                res->d_vec[i] = data->GetDouble(i % dlen); // Recycle
        }
        
        // Set Dim Attribute
        auto dim = std::make_shared<RValue>(RType::INTEGER);
        dim->i_vec = {nr, nc};
        res->attributes["dim"] = dim;
        
        return res;
    }
    
    // dim(x)
    RValuePtr Builtin_Dim(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        if (args[0]->attributes.count("dim")) {
            return args[0]->attributes["dim"];
        }
        return RR_Nil();
    }


    // --- SUBSETTING ---
    // [ (x, i, j, ..., drop)
    RValuePtr Builtin_Subset(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("Empty subset");
        RValuePtr x = args[0];
        
        // --- data.frame subsetting ---
        if (x->type == RType::LIST && HasClass(x, "data.frame")) {
            // Helpers: determine dimensions
            int nc = (int)x->l_vec.size();
            int nr = 0;
            if (nc > 0 && x->l_vec[0]) nr = x->l_vec[0]->Length();
            RValuePtr names_attr = x->attributes.count("names") ? x->attributes["names"] : RR_Nil();
            RValuePtr row_names_attr = x->attributes.count("row.names") ? x->attributes["row.names"] : RR_Nil();

            auto build_col_indices = [&](RValuePtr col_idx, MiniVector<int>& cols) -> RValuePtr {
                if (!col_idx || col_idx->type == RType::NIL) {
                    // All columns
                    for (int j = 0; j < nc; ++j) cols.push_back(j);
                    return RR_Nil();
                }
                if (col_idx->type == RType::CHARACTER && names_attr && names_attr->type == RType::CHARACTER) {
                    for (int k = 0; k < col_idx->Length(); ++k) {
                        const MiniString &key = col_idx->s_vec[k];
                        bool found = false;
                        for (size_t j = 0; j < x->l_vec.size() && j < (size_t)names_attr->Length(); ++j) {
                            if (names_attr->s_vec[j] == key) { cols.push_back((int)j); found = true; break; }
                        }
                        if (!found) return RR_Error("Subscript out of bounds");
                    }
                    return RR_Nil();
                }
                if (col_idx->type == RType::INTEGER || col_idx->type == RType::DOUBLE) {
                    for (int k = 0; k < col_idx->Length(); ++k) {
                        int j = col_idx->GetInt(k) - 1;
                        if (j < 0 || j >= nc) return RR_Error("Subscript out of bounds");
                        cols.push_back(j);
                    }
                    return RR_Nil();
                }
                if (col_idx->type == RType::LOGICAL) {
                    int L = col_idx->Length();
                    if (L == 0) return RR_Nil();
                    for (int j = 0; j < nc; ++j) {
                        int lv = col_idx->GetInt(j % L);
                        if (lv == R_LOGICAL_NA) continue; // skip NA indices
                        if (lv != 0) {
                            if (j < 0 || j >= nc) return RR_Error("Subscript out of bounds");
                            cols.push_back(j);
                        }
                    }
                    return RR_Nil();
                }
                return RR_Error("Unsupported column index for data.frame");
            };

            auto build_row_indices = [&](RValuePtr row_idx, MiniVector<int>& rows) -> RValuePtr {
                if (!row_idx || row_idx->type == RType::NIL) {
                    // All rows
                    for (int i = 0; i < nr; ++i) rows.push_back(i);
                    return RR_Nil();
                }
                if (row_idx->type == RType::INTEGER || row_idx->type == RType::DOUBLE) {
                    for (int k = 0; k < row_idx->Length(); ++k) {
                        int i = row_idx->GetInt(k) - 1;
                        if (i < 0 || i >= nr) return RR_Error("Subscript out of bounds");
                        rows.push_back(i);
                    }
                    return RR_Nil();
                }
                if (row_idx->type == RType::LOGICAL) {
                    int L = row_idx->Length();
                    if (L == 0) return RR_Nil();
                    for (int i = 0; i < nr; ++i) {
                        int lv = row_idx->GetInt(i % L);
                        if (lv == R_LOGICAL_NA) continue; // skip NA rows
                        if (lv != 0) {
                            if (i < 0 || i >= nr) return RR_Error("Subscript out of bounds");
                            rows.push_back(i);
                        }
                    }
                    return RR_Nil();
                }
                return RR_Error("Unsupported row index for data.frame");
            };

            // df["Height"], df[2]  (column-only subset, all rows)
            if (args.size() == 2) {
                RValuePtr col_idx = args[1];
                MiniVector<int> cols;
                RValuePtr err = build_col_indices(col_idx, cols);
                if (err && err->type == RType::ERROR) return err;

                auto res = std::make_shared<RValue>(RType::LIST);
                for (int j : cols) res->l_vec.push_back(x->l_vec[j]);
                // copy attributes
                res->attributes = x->attributes;
                if (names_attr && names_attr->type == RType::CHARACTER) {
                    auto new_names = std::make_shared<RValue>(RType::CHARACTER);
                    for (int j : cols) new_names->s_vec.push_back(names_attr->s_vec[j]);
                    res->attributes["names"] = new_names;
                }
                // row.names unchanged (all rows)
                return res;
            }

            // df[rows, cols]  (rows or cols may be NIL for "all")
            if (args.size() >= 3) {
                RValuePtr row_arg = args[1];
                RValuePtr col_arg = args[2];

                MiniVector<int> row_idx;
                MiniVector<int> col_idx;

                RValuePtr err = build_row_indices(row_arg, row_idx);
                if (err && err->type == RType::ERROR) return err;
                err = build_col_indices(col_arg, col_idx);
                if (err && err->type == RType::ERROR) return err;

                // If no explicit column index (e.g., df[rows, ]) treat as all columns
                if (col_idx.size() == 0 && (!col_arg || col_arg->type == RType::NIL)) {
                    for (int j = 0; j < nc; ++j) col_idx.push_back(j);
                }

                // Special case: df[, j] with all rows and a single column -> return the column vector directly
                bool all_rows = false;
                if (!row_arg || row_arg->type == RType::NIL) {
                    all_rows = true;
                } else if (row_idx.size() == (size_t)nr) {
                    all_rows = true;
                }
                if (all_rows && col_idx.size() == 1) {
                    int j = col_idx[0];
                    if (j < 0 || j >= nc) return RR_Error("Subscript out of bounds");
                    return x->l_vec[j];
                }

                // General case: return a new data.frame with selected rows and columns
                auto res = std::make_shared<RValue>(RType::LIST);
                for (int cj = 0; cj < (int)col_idx.size(); ++cj) {
                    int j = col_idx[cj];
                    if (j < 0 || j >= nc) return RR_Error("Subscript out of bounds");
                    RValuePtr col = x->l_vec[j];
                    if (!col) {
                        res->l_vec.push_back(RR_Nil());
                        continue;
                    }
                    auto new_col = std::make_shared<RValue>(col->type);
                    for (int r : row_idx) {
                        if (r < 0 || r >= col->Length()) return RR_Error("Subscript out of bounds");
                        if (col->type == RType::DOUBLE) new_col->d_vec.push_back(col->d_vec[r]);
                        else if (col->type == RType::INTEGER || col->type == RType::LOGICAL) new_col->i_vec.push_back(col->i_vec[r]);
                        else if (col->type == RType::CHARACTER) new_col->s_vec.push_back(col->s_vec[r]);
                        else new_col->l_vec.push_back(col->l_vec[r]);
                    }
                    res->l_vec.push_back(new_col);
                }

                // copy and subset attributes
                res->attributes = x->attributes;
                if (names_attr && names_attr->type == RType::CHARACTER) {
                    auto new_names = std::make_shared<RValue>(RType::CHARACTER);
                    for (int j : col_idx) {
                        if (j >= 0 && j < (int)names_attr->Length())
                            new_names->s_vec.push_back(names_attr->s_vec[j]);
                        else
                            new_names->s_vec.push_back("");
                    }
                    res->attributes["names"] = new_names;
                }
                if (row_names_attr && row_names_attr->type == RType::INTEGER) {
                    auto new_rn = std::make_shared<RValue>(RType::INTEGER);
                    for (int r : row_idx) {
                        if (r >= 0 && r < (int)row_names_attr->Length())
                            new_rn->i_vec.push_back(row_names_attr->i_vec[r]);
                        else
                            new_rn->i_vec.push_back(R_INT_NA);
                    }
                    res->attributes["row.names"] = new_rn;
                }
                return res;
            }
        }
        
        // Matrix Case: x[i, j] -> 3 args (x, i, j)
        // If x has dim, treat as matrix if 2 indices provided or explicit matrix subset
        if (x->attributes.count("dim") && args.size() >= 3) {
            RValuePtr dim = x->attributes["dim"];
            int nr = dim->GetInt(0);
            int nc = dim->GetInt(1);
            
            RValuePtr rows = args[1]; // i
            RValuePtr cols = args[2]; // j
            
            // Handle Drop logic (defaults to TRUE)
            bool drop = true;
            RValuePtr drop_arg = GetArg(args, names, "drop", -1);
            if (drop_arg) drop = IsTrue(drop_arg);
            
            // Row indices: -1 means NA (output NA), else 0-based valid index
            MiniVector<int> r_idx;
            if (rows->type == RType::NIL || rows->Length() == 0) {
                for(int i=0; i<nr; ++i) r_idx.push_back(i);
            } else {
                for(int k=0; k<rows->Length(); ++k) {
                    int raw = rows->GetInt(k);
                    if (raw == R_INT_NA) { r_idx.push_back(-1); continue; }
                    int idx = raw - 1;
                    r_idx.push_back((idx >= 0 && idx < nr) ? idx : -1);
                }
            }
            MiniVector<int> c_idx;
            if (cols->type == RType::NIL || cols->Length() == 0) {
                for(int i=0; i<nc; ++i) c_idx.push_back(i);
            } else {
                for(int k=0; k<cols->Length(); ++k) {
                    int raw = cols->GetInt(k);
                    if (raw == R_INT_NA) { c_idx.push_back(-1); continue; }
                    int idx = raw - 1;
                    c_idx.push_back((idx >= 0 && idx < nc) ? idx : -1);
                }
            }
            bool ret_matrix = (r_idx.size() > 1 && c_idx.size() > 1) || !drop;
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            for (int c : c_idx) {
                for (int r : r_idx) {
                    if (r == -1 || c == -1)
                        res->d_vec.push_back(NAReal());
                    else
                        res->d_vec.push_back(x->GetDouble(c * nr + r));
                }
            }
            if (ret_matrix || !drop) {
                auto new_dim = std::make_shared<RValue>(RType::INTEGER);
                new_dim->i_vec = {(int)r_idx.size(), (int)c_idx.size()};
                res->attributes["dim"] = new_dim;
            }
            return res;
        }
        
        // Vector/List Case: x[i]  (i can be integer indices or logical mask)
        if (args.size() >= 2) {
            RValuePtr idx = args[1];
            auto res = std::make_shared<RValue>(x->type);
            
            if (idx->type == RType::NIL) return x; // [] -> x
            
            // Logical index: select x[i] where idx[i] is TRUE (recycle idx to length of x)
            if (idx->type == RType::LOGICAL) {
                int xlen = x->Length();
                int ilen = idx->Length();
                for (int i = 0; i < xlen; ++i) {
                    int m = (i < ilen) ? idx->i_vec[i] : idx->i_vec[i % ilen];
                    if (m == R_LOGICAL_NA) {
                        if (x->type == RType::DOUBLE) res->d_vec.push_back(NAReal());
                        else if (x->type == RType::INTEGER || x->type == RType::LOGICAL) res->i_vec.push_back(x->type == RType::LOGICAL ? R_LOGICAL_NA : R_INT_NA);
                        else if (x->type == RType::CHARACTER) res->s_vec.push_back(R_STRING_NA);
                        else if (x->type == RType::LIST) res->l_vec.push_back(RR_Nil());
                    } else if (m != 0) { // TRUE
                        if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
                        else if (x->type == RType::INTEGER || x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
                        else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
                        else if (x->type == RType::LIST) res->l_vec.push_back(x->l_vec[i]);
                    }
                }
                return res;
            }
            
            // Negative indices exclude elements: x[-1], x[-c(1,3)]. Zeros are ignored.
            bool any_neg = false, any_pos = false;
            for (int k = 0; k < idx->Length(); ++k) {
                int raw = idx->GetInt(k);
                if (raw == R_INT_NA) continue;
                if (raw < 0) any_neg = true;
                else if (raw > 0) any_pos = true;
            }
            if (any_neg) {
                if (any_pos) return RR_Error("can't mix positive and negative subscripts");
                MiniVector<int> drop(x->Length());
                for (int k = 0; k < idx->Length(); ++k) {
                    int raw = idx->GetInt(k);
                    if (raw == R_INT_NA) return RR_Error("can't mix NAs and negative subscripts");
                    if (raw < 0 && -raw <= x->Length()) drop[-raw - 1] = 1;
                }
                for (int i = 0; i < x->Length(); ++i) {
                    if (drop[i]) continue;
                    if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
                    if (x->type == RType::INTEGER || x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
                    if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
                    if (x->type == RType::LIST) res->l_vec.push_back(x->l_vec[i]);
                }
                return res;
            }

            // Integer index: for each index value k, take x[k] (1-based); 0 selects nothing
            for(int k=0; k<idx->Length(); ++k) {
                int raw = idx->GetInt(k);
                if (raw == 0) continue;
                int i = (raw == R_INT_NA) ? -1 : raw - 1;
                if (i >= 0 && i < x->Length()) {
                     if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
                     if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[i]);
                     if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
                     if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
                     if (x->type == RType::LIST) res->l_vec.push_back(x->l_vec[i]);
                } else {
                    if (x->type == RType::DOUBLE) res->d_vec.push_back(NAReal());
                    if (x->type == RType::INTEGER) res->i_vec.push_back(R_INT_NA);
                    if (x->type == RType::LOGICAL) res->i_vec.push_back(R_LOGICAL_NA);
                    if (x->type == RType::CHARACTER) res->s_vec.push_back(R_STRING_NA);
                    if (x->type == RType::LIST) res->l_vec.push_back(RR_Nil());
                }
            }
            return res;
        }
        
        return x; 
    }
    
    // [[ (x, i) - Extract single element
    RValuePtr Builtin_Subset2(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need index for [[");
        RValuePtr x = args[0];
        RValuePtr idx = args[1];

        // Name-based extraction for list/data.frame: x[["name"]]
        if ((idx->type == RType::CHARACTER || idx->type == RType::SYMBOL) &&
            x->type == RType::LIST && x->attributes.count("names")) {
            MiniString key;
            if (idx->type == RType::SYMBOL) key = idx->sym_name;
            else if (idx->type == RType::CHARACTER && idx->Length() > 0) key = idx->s_vec[0];
            RValuePtr names_attr = x->attributes["names"];
            if (names_attr && names_attr->type == RType::CHARACTER) {
                for (size_t j = 0; j < x->l_vec.size() && j < (size_t)names_attr->Length(); ++j) {
                    if (names_attr->s_vec[j] == key) return x->l_vec[j];
                }
            }
            return RR_Error("Subscript out of bounds");
        }

        int raw = idx->GetInt(0);
        if (raw == R_INT_NA) {
            // R: x[[NA_integer_]] returns NA (atomic) or NULL (list)
            if (x->type == RType::LIST) return RR_Nil();
            auto res = std::make_shared<RValue>(x->type);
            if (x->type == RType::DOUBLE) res->d_vec.push_back(NAReal());
            if (x->type == RType::INTEGER || x->type == RType::LOGICAL) res->i_vec.push_back(x->type == RType::LOGICAL ? R_LOGICAL_NA : R_INT_NA);
            if (x->type == RType::CHARACTER) res->s_vec.push_back("NA");
            return res;
        }
        int i = raw - 1;
        if (i < 0 || i >= x->Length()) return RR_Error("Subscript out of bounds");
        if (x->type == RType::LIST) return x->l_vec[i];
        auto res = std::make_shared<RValue>(x->type);
        if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
        if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[i]);
        if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
        if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
        return res;
    }

    RValuePtr Builtin_NRow(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
         if (args.empty()) return RR_Error("nrow() requires at least 1 argument");
         RValuePtr x = args[0];
         if (x->attributes.count("dim")) {
             auto r = std::make_shared<RValue>(RType::INTEGER);
             r->i_vec.push_back(x->attributes["dim"]->GetInt(0));
             return r;
         }
         if (x->type == RType::LIST && HasClass(x, "data.frame") && !x->l_vec.empty()) {
             auto r = std::make_shared<RValue>(RType::INTEGER);
             r->i_vec.push_back(x->l_vec[0]->Length());
             return r;
         }
         return RR_Nil();
    }
    
    RValuePtr Builtin_NCol(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
         if (args.empty()) return RR_Error("ncol() requires at least 1 argument");
         RValuePtr x = args[0];
         if (x->attributes.count("dim")) {
             auto r = std::make_shared<RValue>(RType::INTEGER);
             r->i_vec.push_back(x->attributes["dim"]->GetInt(1));
             return r;
         }
         if (x->type == RType::LIST && HasClass(x, "data.frame")) {
             auto r = std::make_shared<RValue>(RType::INTEGER);
             r->i_vec.push_back((int)x->l_vec.size());
             return r;
         }
         return RR_Nil();
    }

    // --- SUMMARIES ---
    
    // Generic Aggregator
    // Ops: 0=SUM, 1=MAX, 2=MIN, 3=ANY, 4=ALL
    // --- COERCION & CHECKS ---
    
    RValuePtr Builtin_AsLogical(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("as.logical() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        for (int i = 0; i < x->Length(); ++i) res->i_vec.push_back(AsLogicalAt(x, i));
        return res;
    }
    
    RValuePtr Builtin_AsCharacter(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("as.character() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<x->Length(); ++i) res->s_vec.push_back(AsStringAt(x, i));
        return res;
    }

    RValuePtr Builtin_IsMatrix(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.matrix() requires at least 1 argument");
        int v = (args[0]->attributes.count("dim") > 0) ? 1 : 0;
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(v);
        return res;
    }

    RValuePtr Builtin_IsVector(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.vector() requires at least 1 argument");
        // R says is.vector returns TRUE if it has no attributes other than names
        // Simplified: return true if atomic type and no dim?
        int v = (args[0]->attributes.count("dim") == 0) ? 1 : 0;
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(v);
        return res;
    }

    RValuePtr Builtin_IsNA(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.na() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        for(int i=0; i<x->Length(); ++i) {
            bool is_na = false;
            if (x->type == RType::DOUBLE) {
                // R: is.na() is TRUE for NA_real_ and also for NaN
                is_na = std::isnan(x->d_vec[i]);
            } else if (x->type == RType::INTEGER) {
                is_na = (x->i_vec[i] == R_INT_NA);
} else if (x->type == RType::LOGICAL) {
                is_na = (x->i_vec[i] == R_LOGICAL_NA);
            } else if (x->type == RType::CHARACTER) {
                is_na = IsNAString(x->s_vec[i]);
            }
            res->i_vec.push_back(is_na ? 1 : 0);
        }
        return res;
    }

    // --- INDEXING & PARALLEL ---

    // which(x, arr.ind=FALSE) - Returns flat indices, or matrix of (row,col) if arr.ind=TRUE

} // namespace Evaluator
