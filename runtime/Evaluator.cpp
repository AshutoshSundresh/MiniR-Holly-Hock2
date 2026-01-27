#include "Evaluator.hpp"
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>
#include <cstdint>
#include <cstring>
#include <limits>

namespace Evaluator {

    // Helper: Is a value TRUE?
    bool IsTrue(RValuePtr v) {
        if (!v) return false;
        if (v->type == RType::LOGICAL || v->type == RType::INTEGER) {
            return !v->i_vec.empty() && v->i_vec[0] != 0 && v->i_vec[0] != R_LOGICAL_NA;
        }
        return false;
    }

    // R-style logical scalar encoding: 1=TRUE, 0=FALSE, -1=NA
    // NA_real_ in R is a specific NaN payload. We'll emulate that so we can
    // distinguish NA from plain NaN when printing.
    static inline double NAReal() {
        // Common R NA_real_ payload (IEEE-754 quiet NaN with specific bits).
        // This matches R's NA_REAL on typical platforms.
        const uint64_t bits = 0x7ff00000000007a2ULL;
        double d;
        std::memcpy(&d, &bits, sizeof(d));
        return d;
    }
    static inline bool IsNAReal(double d) {
        if (!std::isnan(d)) return false;
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof(bits));
        return bits == 0x7ff00000000007a2ULL;
    }

    int AsLogicalAt(RValuePtr v, int i) {
        if (!v) return 0;
        if (v->type == RType::LOGICAL) {
            if (i < 0 || i >= (int)v->i_vec.size()) return 0;
            return v->i_vec[i];
        }
        if (v->type == RType::INTEGER) {
            if (i < 0 || i >= (int)v->i_vec.size()) return 0;
            if (v->i_vec[i] == R_INT_NA) return R_LOGICAL_NA;
            return v->i_vec[i] == 0 ? 0 : 1;
        }
        if (v->type == RType::DOUBLE) {
            if (i < 0 || i >= (int)v->d_vec.size()) return 0;
            double d = v->d_vec[i];
            if (std::isnan(d)) return R_LOGICAL_NA;
            return d == 0.0 ? 0 : 1;
        }
        // For this MiniR subset, treat other types as FALSE.
        return 0;
    }

    bool ConditionToBoolOrError(RValuePtr cond, std::string& err) {
        if (!cond) { err = "argument is of length zero"; return false; }
        if (cond->Length() == 0) { err = "argument is of length zero"; return false; }
        if (cond->Length() != 1) { err = "the condition has length > 1"; return false; }
        int lv = AsLogicalAt(cond, 0);
        if (lv == R_LOGICAL_NA) { err = "missing value where TRUE/FALSE needed"; return false; }
        return lv == 1;
    }

    void WarnRecycle(const char* op, int lenA, int lenB) {
        if (lenA == 0 || lenB == 0) return;
        int N = std::max(lenA, lenB);
        if ((lenA != 0 && N % lenA != 0) || (lenB != 0 && N % lenB != 0)) {
            std::cerr << "Warning: longer object length is not a multiple of shorter object length in '" << op << "'\n";
        }
    }

    bool AnyDouble(RValuePtr a, RValuePtr b) {
        return (a && a->type == RType::DOUBLE) || (b && b->type == RType::DOUBLE);
    }

    bool IsIntLike(RValuePtr v) {
        return v && (v->type == RType::INTEGER || v->type == RType::LOGICAL);
    }

    int32_t IntOpResultOrNA(int64_t x) {
        if (x < (int64_t)INT32_MIN || x > (int64_t)INT32_MAX) return R_INT_NA;
        return (int32_t)x;
    }

    RValuePtr Lookup(const std::string& name, RValuePtr env) {
        // Simple search up parent chain
        RValuePtr cur = env;
        while (cur) {
            auto it = cur->frame.find(name);
            if (it != cur->frame.end()) return it->second;
            cur = cur->parent_env;
        }
        return RR_Error("Object '" + name + "' not found");
    }

    void Define(const std::string& name, RValuePtr val, RValuePtr env) {
        if (env) env->frame[name] = val;
    }

    // --- ARGUMENT MATCHING ---
    // Returns the value if found, or default_val.
    // Logic: Look for exact name match in arg_names.
    // If not found, look for positional (index pos) IF that position hasn't been named in the call.
    // (Simplified: if arg_names[pos] is empty, it's a candidate for positional)
    RValuePtr GetArg(const std::vector<RValuePtr>& args, const std::vector<std::string>& call_names, 
                     const std::string& target_name, int target_pos, RValuePtr default_val = nullptr) {
                         
        // 1. Try Name Match
        for(size_t i=0; i<args.size(); ++i) {
            if (i < call_names.size() && call_names[i] == target_name) {
                return args[i];
            }
        }
        
        // 2. Try Positional Match
        // R logic: Positional args are those that were NOT matched by name.
        // We simplified: We just check if slot 'target_pos' exists and has no name or empty name.
        if (target_pos >= 0 && target_pos < (int)args.size()) {
             if (target_pos >= (int)call_names.size() || call_names[target_pos].empty()) {
                 return args[target_pos];
             }
        }
        return default_val;
    }

    // Clone x (shallow copy of vectors and attributes)
    static RValuePtr CloneForAssign(RValuePtr x) {
        auto r = std::make_shared<RValue>(x->type);
        r->d_vec = x->d_vec;
        r->i_vec = x->i_vec;
        r->s_vec = x->s_vec;
        r->l_vec = x->l_vec;
        r->attributes = x->attributes;
        r->sym_name = x->sym_name;
        return r;
    }

    // Subassignment: x[i] <- value, x[i,j] <- value, x[[i]] <- value, x$name <- value. Returns modified clone.
    static RValuePtr SubAssign(RValuePtr x, const std::string& op, const std::vector<RValuePtr>& index_vals, RValuePtr value, std::string& err) {
        err.clear();
        if (!x) { err = "object not found"; return nullptr; }
        RValuePtr res = CloneForAssign(x);
        if (op == "$") {
            if (x->type != RType::LIST || index_vals.empty()) { err = "invalid $ assignment"; return nullptr; }
            std::string key = index_vals[0]->type == RType::CHARACTER ? index_vals[0]->s_vec[0] : index_vals[0]->sym_name;
            if (!res->attributes.count("names")) { err = "no names for $ assign"; return nullptr; }
            RValuePtr names_attr = res->attributes["names"];
            for (size_t i = 0; i < res->l_vec.size() && i < (size_t)names_attr->Length(); ++i) {
                if (names_attr->s_vec[i] == key) { res->l_vec[i] = value; return res; }
            }
            err = "name not found for $ assign"; return nullptr;
        }
        if (op == "[[") {
            if (index_vals.empty()) { err = "need index for [[ assign"; return nullptr; }
            int i = index_vals[0]->GetInt(0) - 1;
            if (i < 0 || i >= res->Length()) { err = "Subscript out of bounds"; return nullptr; }
            if (res->type == RType::LIST) { res->l_vec[i] = value; return res; }
            if (res->type == RType::DOUBLE) res->d_vec[i] = value->GetDouble(0);
            else if (res->type == RType::INTEGER || res->type == RType::LOGICAL) res->i_vec[i] = value->Length() ? value->GetInt(0) : R_INT_NA;
            else if (res->type == RType::CHARACTER) res->s_vec[i] = value->Length() ? value->s_vec[0] : "NA";
            return res;
        }
        if (op == "[") {
            if (index_vals.empty()) { err = "need index for [ assign"; return nullptr; }
            if (res->attributes.count("dim") && index_vals.size() >= 2) {
                int nr = res->attributes["dim"]->GetInt(0);
                int nc = res->attributes["dim"]->GetInt(1);
                int r = index_vals[0]->GetInt(0) - 1;
                int c = index_vals[1]->GetInt(0) - 1;
                if (r >= 0 && r < nr && c >= 0 && c < nc) {
                    int flat = c * nr + r;
                    res->d_vec[flat] = value->Length() ? value->GetDouble(0) : NAReal();
                }
                return res;
            }
            RValuePtr idx = index_vals[0];
            int nidx = idx->Length();
            int vlen = value->Length();
            if (vlen == 0) { err = "replacement has length zero"; return nullptr; }
            for (int k = 0; k < nidx; ++k) {
                int i = idx->GetInt(k) - 1;
                if (i == R_INT_NA - 1 || i < 0) continue;
                if (i >= res->Length()) { err = "Subscript out of bounds"; return nullptr; }
                double v = value->GetDouble(k % vlen);
                int vi = value->Length() ? value->GetInt(k % vlen) : R_INT_NA;
                if (res->type == RType::DOUBLE) res->d_vec[i] = v;
                else if (res->type == RType::INTEGER || res->type == RType::LOGICAL) res->i_vec[i] = vi;
                else if (res->type == RType::CHARACTER) res->s_vec[i] = value->s_vec[k % vlen];
            }
            return res;
        }
        err = "invalid subassignment op"; return nullptr;
    }

    // --- BUILTINS ---

    // assign(x, value, ...) — assign value to name x in environment (R: assign("name", value))
    RValuePtr Builtin_Assign(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Nil();
        std::string name_str;
        if (args[0]->type == RType::CHARACTER && args[0]->Length() > 0) name_str = args[0]->s_vec[0];
        else if (args[0]->type == RType::SYMBOL) name_str = args[0]->sym_name;
        else return RR_Nil();
        RValuePtr val = args[1];
        Define(name_str, val, env);
        return val;
    }
    
    RValuePtr Builtin_C(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        // Combine with R-like coercion priority: character > double > integer > logical
        bool has_char = false, has_double = false, has_int = false, has_lgl = false;
        for (auto& arg : args) {
            has_char |= (arg->type == RType::CHARACTER);
            has_double |= (arg->type == RType::DOUBLE);
            has_int |= (arg->type == RType::INTEGER);
            has_lgl |= (arg->type == RType::LOGICAL);
        }

        if (has_char) {
            auto res = std::make_shared<RValue>(RType::CHARACTER);
            for (auto& arg : args) {
                if (arg->type == RType::CHARACTER) {
                    res->s_vec.insert(res->s_vec.end(), arg->s_vec.begin(), arg->s_vec.end());
                } else if (arg->type == RType::INTEGER) {
                    for (int i = 0; i < arg->Length(); ++i)
                        res->s_vec.push_back(arg->i_vec[i] == R_INT_NA ? "NA" : std::to_string(arg->i_vec[i]));
                } else if (arg->type == RType::LOGICAL) {
                    for (int i = 0; i < arg->Length(); ++i) {
                        int v = arg->i_vec[i];
                        res->s_vec.push_back(v == R_LOGICAL_NA ? "NA" : (v ? "TRUE" : "FALSE"));
                    }
                } else if (arg->type == RType::DOUBLE) {
                    for (int i = 0; i < arg->Length(); ++i) {
                        double d = arg->d_vec[i];
                        if (std::isnan(d)) res->s_vec.push_back("NA");
                        else if (d == std::floor(d) && d >= INT32_MIN && d <= INT32_MAX)
                            res->s_vec.push_back(std::to_string(static_cast<int>(d)));
                        else res->s_vec.push_back(std::to_string(d));
                    }
                }
            }
            return res;
        }

        if (has_double) {
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            for (auto& arg : args) {
                if (arg->type == RType::DOUBLE) {
                    res->d_vec.insert(res->d_vec.end(), arg->d_vec.begin(), arg->d_vec.end());
                } else if (arg->type == RType::INTEGER || arg->type == RType::LOGICAL) {
                    for (int i = 0; i < arg->Length(); ++i) {
                        if (arg->type == RType::LOGICAL && arg->i_vec[i] == R_LOGICAL_NA) res->d_vec.push_back(NAReal());
                        else if (arg->type == RType::INTEGER && arg->i_vec[i] == R_INT_NA) res->d_vec.push_back(NAReal());
                        else res->d_vec.push_back(arg->GetDouble(i));
                    }
                }
            }
            return res;
        }

        if (has_int) {
            auto res = std::make_shared<RValue>(RType::INTEGER);
            for (auto& arg : args) {
                if (arg->type == RType::INTEGER) res->i_vec.insert(res->i_vec.end(), arg->i_vec.begin(), arg->i_vec.end());
                else if (arg->type == RType::LOGICAL) res->i_vec.insert(res->i_vec.end(), arg->i_vec.begin(), arg->i_vec.end());
            }
            return res;
        }

        // logical-only
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        for (auto& arg : args) {
            if (arg->type == RType::LOGICAL) res->i_vec.insert(res->i_vec.end(), arg->i_vec.begin(), arg->i_vec.end());
        }
        return res;
    }

    RValuePtr Builtin_Add(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("Need 1 or 2 args for +");
        if (args.size() == 1) return args[0]; // unary + is identity
        int lenA = args[0]->Length();
        int lenB = args[1]->Length();
        if (lenA == 0 || lenB == 0) {
            return AnyDouble(args[0], args[1]) ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
        }
        WarnRecycle("+", lenA, lenB);
        int N = std::max(lenA, lenB);

        // Type promotion: if either is DOUBLE => DOUBLE else INTEGER
        if (AnyDouble(args[0], args[1])) {
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            res->d_vec.resize(N);
            for (int i = 0; i < N; ++i) {
                double a = args[0]->GetDouble(i % lenA);
                double b = args[1]->GetDouble(i % lenB);
                res->d_vec[i] = a + b;
            }
            return res;
        }
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.resize(N);
        for (int i = 0; i < N; ++i) {
            int a = args[0]->GetInt(i % lenA);
            int b = args[1]->GetInt(i % lenB);
            if (a == R_INT_NA || b == R_INT_NA) res->i_vec[i] = R_INT_NA;
            else res->i_vec[i] = IntOpResultOrNA((int64_t)a + (int64_t)b);
        }
        return res;
    }
    
    RValuePtr Builtin_Diff(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         if (args.empty()) return RR_Error("Need 1 or 2 args for -");
         if (args.size() == 1) {
             // unary minus: negate
             RValuePtr x = args[0];
             int n = x->Length();
             if (n == 0) return std::make_shared<RValue>(x->type == RType::DOUBLE ? RType::DOUBLE : RType::INTEGER);
             if (x->type == RType::DOUBLE) {
                 auto res = std::make_shared<RValue>(RType::DOUBLE);
                 for (int i = 0; i < n; ++i) res->d_vec.push_back(-x->GetDouble(i));
                 return res;
             }
             if (x->type == RType::INTEGER || x->type == RType::LOGICAL) {
                 auto res = std::make_shared<RValue>(RType::INTEGER);
                 for (int i = 0; i < n; ++i) {
                     int v = x->i_vec[i];
                     res->i_vec.push_back((v == R_INT_NA || v == R_LOGICAL_NA) ? R_INT_NA : IntOpResultOrNA(-(int64_t)v));
                 }
                 return res;
             }
             return RR_Error("Unary - needs numeric");
         }
         int lenA = args[0]->Length();
         int lenB = args[1]->Length();
         if (lenA == 0 || lenB == 0) {
             return AnyDouble(args[0], args[1]) ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
         }
         WarnRecycle("-", lenA, lenB);
         int N = std::max(lenA, lenB);
         if (AnyDouble(args[0], args[1])) {
             auto res = std::make_shared<RValue>(RType::DOUBLE);
             res->d_vec.resize(N);
             for (int i = 0; i < N; ++i) {
                 double a = args[0]->GetDouble(i % lenA);
                 double b = args[1]->GetDouble(i % lenB);
                 res->d_vec[i] = a - b;
             }
             return res;
         }
         auto res = std::make_shared<RValue>(RType::INTEGER);
         res->i_vec.resize(N);
         for (int i = 0; i < N; ++i) {
             int a = args[0]->GetInt(i % lenA);
             int b = args[1]->GetInt(i % lenB);
             if (a == R_INT_NA || b == R_INT_NA) res->i_vec[i] = R_INT_NA;
             else res->i_vec[i] = IntOpResultOrNA((int64_t)a - (int64_t)b);
         }
         return res;
    }

     RValuePtr Builtin_Mul(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         if (args.size() < 2) return RR_Error("Need 2 args for *");
         int lenA = args[0]->Length();
         int lenB = args[1]->Length();
         if (lenA == 0 || lenB == 0) {
             return AnyDouble(args[0], args[1]) ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
         }
         WarnRecycle("*", lenA, lenB);
         int N = std::max(lenA, lenB);
         if (AnyDouble(args[0], args[1])) {
             auto res = std::make_shared<RValue>(RType::DOUBLE);
             res->d_vec.resize(N);
             for (int i = 0; i < N; ++i) {
                 double a = args[0]->GetDouble(i % lenA);
                 double b = args[1]->GetDouble(i % lenB);
                 res->d_vec[i] = a * b;
             }
             return res;
         }
         auto res = std::make_shared<RValue>(RType::INTEGER);
         res->i_vec.resize(N);
         for (int i = 0; i < N; ++i) {
             int a = args[0]->GetInt(i % lenA);
             int b = args[1]->GetInt(i % lenB);
             if (a == R_INT_NA || b == R_INT_NA) res->i_vec[i] = R_INT_NA;
             else res->i_vec[i] = IntOpResultOrNA((int64_t)a * (int64_t)b);
         }
         return res;
    }

    RValuePtr Builtin_Div(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for /");
        int lenA = args[0]->Length();
        int lenB = args[1]->Length();
        if (lenA == 0 || lenB == 0) return std::make_shared<RValue>(RType::DOUBLE);
        WarnRecycle("/", lenA, lenB);
        int N = std::max(lenA, lenB);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        for (int i = 0; i < N; ++i) {
            double a = args[0]->GetDouble(i % lenA);
            double b = args[1]->GetDouble(i % lenB);
            res->d_vec[i] = a / b;
        }
        return res;
    }

    RValuePtr Builtin_Pow(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for ^");
        int lenA = args[0]->Length();
        int lenB = args[1]->Length();
        if (lenA == 0 || lenB == 0) return std::make_shared<RValue>(RType::DOUBLE);
        WarnRecycle("^", lenA, lenB);
        int N = std::max(lenA, lenB);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        for (int i = 0; i < N; ++i) {
            double a = args[0]->GetDouble(i % lenA);
            double b = args[1]->GetDouble(i % lenB);
            res->d_vec[i] = std::pow(a, b);
        }
        return res;
    }

    // Comparison ops: < > <= >= == !=  (elementwise, recycle, return LOGICAL; NA propagates)
    enum class CmpOp { LT, GT, LE, GE, EQ, NE };
    static RValuePtr Builtin_Compare(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env, CmpOp op) {
        if (args.size() < 2) return RR_Error("Need 2 args for comparison");
        RValuePtr a = args[0], b = args[1];
        int lenA = a->Length(), lenB = b->Length();
        if (lenA == 0 || lenB == 0) return std::make_shared<RValue>(RType::LOGICAL);
        WarnRecycle(op == CmpOp::LT ? "<" : op == CmpOp::GT ? ">" : op == CmpOp::LE ? "<=" : op == CmpOp::GE ? ">=" : op == CmpOp::EQ ? "==" : "!=", lenA, lenB);
        int N = std::max(lenA, lenB);
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        bool both_char = (a->type == RType::CHARACTER && b->type == RType::CHARACTER);
        for (int i = 0; i < N; ++i) {
            int ia = i % lenA, ib = i % lenB;
            if (both_char) {
                const std::string& sa = a->s_vec[ia];
                const std::string& sb = b->s_vec[ib];
                int c = sa.compare(sb);
                bool r = (op == CmpOp::LT && c < 0) || (op == CmpOp::GT && c > 0) || (op == CmpOp::LE && c <= 0) || (op == CmpOp::GE && c >= 0) || (op == CmpOp::EQ && c == 0) || (op == CmpOp::NE && c != 0);
                res->i_vec.push_back(r ? 1 : 0);
            } else {
                double da = a->GetDouble(ia), db = b->GetDouble(ib);
                if (std::isnan(da) || std::isnan(db)) { res->i_vec.push_back(R_LOGICAL_NA); continue; }
                bool r = (op == CmpOp::LT && da < db) || (op == CmpOp::GT && da > db) || (op == CmpOp::LE && da <= db) || (op == CmpOp::GE && da >= db) || (op == CmpOp::EQ && da == db) || (op == CmpOp::NE && da != db);
                res->i_vec.push_back(r ? 1 : 0);
            }
        }
        return res;
    }
    RValuePtr Builtin_Lt(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::LT); }
    RValuePtr Builtin_Gt(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::GT); }
    RValuePtr Builtin_Le(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::LE); }
    RValuePtr Builtin_Ge(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::GE); }
    RValuePtr Builtin_Eq(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::EQ); }
    RValuePtr Builtin_Ne(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::NE); }

    RValuePtr Builtin_Mod(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for %%");
        int lenA = args[0]->Length();
        int lenB = args[1]->Length();
        if (lenA == 0 || lenB == 0) {
            return (AnyDouble(args[0], args[1]) ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER));
        }
        WarnRecycle("%%", lenA, lenB);
        int N = std::max(lenA, lenB);
        // If both are int-like, return INTEGER like R does for 5L %% 2L
        if (!AnyDouble(args[0], args[1]) && IsIntLike(args[0]) && IsIntLike(args[1])) {
            auto res = std::make_shared<RValue>(RType::INTEGER);
            res->i_vec.resize(N);
            for (int i = 0; i < N; ++i) {
                int a = args[0]->GetInt(i % lenA);
                int b = args[1]->GetInt(i % lenB);
                if (a == R_INT_NA || b == R_INT_NA) res->i_vec[i] = R_INT_NA;
                else if (b == 0) res->i_vec[i] = R_INT_NA;
                else res->i_vec[i] = a % b;
            }
            return res;
        }
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        for (int i = 0; i < N; ++i) {
            double a = args[0]->GetDouble(i % lenA);
            double b = args[1]->GetDouble(i % lenB);
            res->d_vec[i] = std::fmod(a, b);
        }
        return res;
    }

    RValuePtr Builtin_IntDiv(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for %/%");
        int lenA = args[0]->Length();
        int lenB = args[1]->Length();
        if (lenA == 0 || lenB == 0) {
            return (AnyDouble(args[0], args[1]) ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER));
        }
        WarnRecycle("%/%", lenA, lenB);
        int N = std::max(lenA, lenB);
        if (!AnyDouble(args[0], args[1]) && IsIntLike(args[0]) && IsIntLike(args[1])) {
            auto res = std::make_shared<RValue>(RType::INTEGER);
            res->i_vec.resize(N);
            for (int i = 0; i < N; ++i) {
                int a = args[0]->GetInt(i % lenA);
                int b = args[1]->GetInt(i % lenB);
                if (a == R_INT_NA || b == R_INT_NA) res->i_vec[i] = R_INT_NA;
                else if (b == 0) res->i_vec[i] = R_INT_NA;
                else res->i_vec[i] = (int)std::floor((double)a / (double)b);
            }
            return res;
        }
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        for (int i = 0; i < N; ++i) {
            double a = args[0]->GetDouble(i % lenA);
            double b = args[1]->GetDouble(i % lenB);
            res->d_vec[i] = std::floor(a / b);
        }
        return res;
    }
    
    // --- STANDARD MATH ---
    RValuePtr Builtin_Log(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        // base? default e
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) res->d_vec.push_back(std::log(x->GetDouble(i)));
        return res;
    }
    RValuePtr Builtin_Exp(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) res->d_vec.push_back(std::exp(x->GetDouble(i)));
        return res;
    }
    RValuePtr Builtin_Sqrt(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) {
            double v = x->GetDouble(i);
            // R: sqrt(negative) -> NaN with warning (we don't implement warnings yet)
            res->d_vec.push_back(v < 0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(v));
        }
        return res;
    }
    RValuePtr Builtin_Abs(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) res->d_vec.push_back(std::abs(x->GetDouble(i)));
        return res;
    }
    RValuePtr Builtin_Round(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        RValuePtr dig = GetArg(args, names, "digits", 1);
        int d = dig ? dig->GetInt(0) : 0;
        double scale = std::pow(10.0, d);
        
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) {
            double v = x->GetDouble(i);
            res->d_vec.push_back(std::round(v * scale) / scale);
        }
        return res;
    }
    
    // --- LOGIC OPS ---
    RValuePtr Builtin_And(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
          if (args.size() < 2) return RR_Error("Need 2 args for &");
          // Elementwise
          int lenA = args[0]->Length();
          int lenB = args[1]->Length();
          int N = std::max(lenA, lenB);
          auto res = std::make_shared<RValue>(RType::LOGICAL);
          for (int i = 0; i < N; ++i) {
              int a = lenA ? AsLogicalAt(args[0], i % lenA) : 0;
              int b = lenB ? AsLogicalAt(args[1], i % lenB) : 0;
              // R rules: FALSE & NA -> FALSE; TRUE & NA -> NA; NA & NA -> NA
              int out;
              if (a == 0 || b == 0) out = 0;
              else if (a == R_LOGICAL_NA || b == R_LOGICAL_NA) out = R_LOGICAL_NA;
              else out = 1;
              res->i_vec.push_back(out);
          }
          return res;
    }
    RValuePtr Builtin_Or(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
          if (args.size() < 2) return RR_Error("Need 2 args for |");
          int lenA = args[0]->Length();
          int lenB = args[1]->Length();
          int N = std::max(lenA, lenB);
          auto res = std::make_shared<RValue>(RType::LOGICAL);
          for (int i = 0; i < N; ++i) {
              int a = lenA ? AsLogicalAt(args[0], i % lenA) : 0;
              int b = lenB ? AsLogicalAt(args[1], i % lenB) : 0;
              // R rules: TRUE | NA -> TRUE; FALSE | NA -> NA; NA | NA -> NA
              int out;
              if (a == 1 || b == 1) out = 1;
              else if (a == R_LOGICAL_NA || b == R_LOGICAL_NA) out = R_LOGICAL_NA;
              else out = 0;
              res->i_vec.push_back(out);
          }
          return res;
    }

    RValuePtr Builtin_Not(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("Need 1 arg for !");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        for (int i = 0; i < x->Length(); ++i) {
            int v = AsLogicalAt(x, i);
            res->i_vec.push_back(v == R_LOGICAL_NA ? R_LOGICAL_NA : (v == 1 ? 0 : 1));
        }
        return res;
    }
    // --- CLOSURE CALL HELPER ---
    // We need to expose a way for BUILTINS (like lapply) to call functions (closures/builtins)
    RValuePtr ApplyFunction(RValuePtr func, const std::vector<RValuePtr>& args, RValuePtr env) {
          if (func->type == RType::BUILTIN) {
              std::vector<std::string> names(args.size(), ""); // No names for now
              return func->builtin(args, names, env);
          }
          if (func->type == RType::CLOSURE) {
               auto new_env = std::make_shared<RValue>(RType::ENV);
               new_env->parent_env = func->env;
               RValuePtr formals = func->formals;
               for(size_t i=0; i<formals->l_vec.size(); ++i) {
                    RValuePtr sym = formals->l_vec[i];
                    if (i < args.size()) Define(sym->sym_name, args[i], new_env);
                    else Define(sym->sym_name, RR_Nil(), new_env);
               }
               return Eval(func->body, new_env);
          }
          return RR_Error("Not a function");
    }

    // --- FUNCTIONAL OPS ---
    RValuePtr Builtin_Lapply(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("lapply(X, FUN)");
        RValuePtr X = args[0];
        RValuePtr FUN = args[1];
        
        // FUN might be symbol -> lookup
        if (FUN->type == RType::SYMBOL) {
            FUN = Lookup(FUN->sym_name, env);
            if (FUN->type == RType::ERROR) return FUN;
        }
        
        auto res = std::make_shared<RValue>(RType::LIST);
        int n = X->Length();
        for(int i=0; i<n; ++i) {
             // Extract element
             auto elem = std::make_shared<RValue>(RType::NIL);
             // Logic to extract single element form X as RValue
             if (X->type == RType::LIST) elem = X->l_vec[i];
             else {
                 if (X->type == RType::DOUBLE) { elem->type = RType::DOUBLE; elem->d_vec.push_back(X->d_vec[i]); }
                 else if (X->type == RType::INTEGER) { elem->type = RType::INTEGER; elem->i_vec.push_back(X->i_vec[i]); }
                 else if (X->type == RType::LOGICAL) { elem->type = RType::LOGICAL; elem->i_vec.push_back(X->i_vec[i]); }
                 else if (X->type == RType::CHARACTER) { elem->type = RType::CHARACTER; elem->s_vec.push_back(X->s_vec[i]); }
             }
             
             std::vector<RValuePtr> fargs = { elem };
             // Additional args?
             RValuePtr val = ApplyFunction(FUN, fargs, env);
             res->l_vec.push_back(val);
        }
        return res;
    }
    
    RValuePtr Builtin_Sapply(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         // lapply then simplify
         RValuePtr l = Builtin_Lapply(args, names, env);
         if (l->type == RType::ERROR) return l;
         
         // Simplify?
         // If all elements are length 1 and same type -> Vector
         if (l->l_vec.empty()) return RR_Nil();
         
         RType common = RType::NIL;
         for(auto& v : l->l_vec) {
             if (v->Length() != 1) return l; // Don't simplify
             if (common == RType::NIL) common = v->type;
             else if (common != v->type) return l; // Mixed types
         }
         
         auto res = std::make_shared<RValue>(common);
         for (auto& v : l->l_vec) {
             if (common == RType::DOUBLE) res->d_vec.push_back(v->d_vec[0]);
             else if (common == RType::INTEGER) res->i_vec.push_back(v->i_vec[0]);
             else if (common == RType::LOGICAL) res->i_vec.push_back(v->i_vec[0]);
             else if (common == RType::CHARACTER) res->s_vec.push_back(v->s_vec[0]);
         }
         return res;
    }
    
    // --- STRING OPS ---
    RValuePtr Builtin_Nchar(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for(int i=0; i<x->Length(); ++i) {
            if (x->type == RType::CHARACTER) res->i_vec.push_back(x->s_vec[i].length());
            else res->i_vec.push_back(0); // Coerce?
        }
        return res;
    }
    
    RValuePtr Builtin_Substr(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        // substr(x, start, stop)
        if (args.size() < 3) return RR_Error("substr(x, start, stop)");
        RValuePtr x = args[0];
        int start = args[1]->GetInt(0);
        int stop = args[2]->GetInt(0);
        
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<x->Length(); ++i) {
            std::string s = "";
            if (x->type == RType::CHARACTER) s = x->s_vec[i];
            
            // R uses 1-based indexing
            int s_len = s.length();
            int r_start = std::max(1, start) - 1;
            int r_stop = std::min(s_len, stop);
            int len = r_stop - r_start;
            
            if (len > 0 && r_start < s_len) res->s_vec.push_back(s.substr(r_start, len));
            else res->s_vec.push_back("");
        }
        return res;
    }
    
    // --- MATCHING ---
    // match(x, table, nomatch = NA_integer_, incomparables = NULL)
    // %in% calls match
    RValuePtr Builtin_Match(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("match(x, table)");
        RValuePtr x = args[0];
        RValuePtr table = args[1];
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        
        // Brute force match
        for(int i=0; i<x->Length(); ++i) {
            int found = -1; // NA? R uses NA_integer_
            // Search in table
            for(int k=0; k<table->Length(); ++k) {
                // Equality check
                bool eq = false;
                // Type specific
                if (x->type == RType::INTEGER && table->type == RType::INTEGER) eq = (x->i_vec[i] == table->i_vec[k]);
                else if (x->type == RType::DOUBLE) eq = (x->d_vec[i] == table->GetDouble(k));
                else if (x->type == RType::CHARACTER) eq = (x->s_vec[i] == (table->type==RType::CHARACTER ? table->s_vec[k] : ""));
                
                if (eq) { found = k + 1; break; } // 1-based index
            }
            res->i_vec.push_back(found != -1 ? found : R_INT_NA);
        }
        return res;
    }

    RValuePtr Builtin_In(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        // x %in% table -> match(x, table, nomatch=0) > 0
        RValuePtr m = Builtin_Match(args, names, env);
        if (m->type == RType::ERROR) return m;
        
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        for(int v : m->i_vec) res->i_vec.push_back(v == R_INT_NA ? R_LOGICAL_NA : (v > 0 ? 1 : 0));
        return res;
    }

    // --- LOGIC OPS ---

    // --- RANDOMNESS ---
    static std::mt19937 g_rng(1234);
    
    RValuePtr Builtin_SetSeed(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (!args.empty()) g_rng.seed(args[0]->GetInt(0));
        return RR_Nil();
    }
    
    RValuePtr Builtin_Runif(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        double min = 0, max = 1;
        if (args.size() > 1) min = args[1]->GetDouble(0);
        if (args.size() > 2) max = args[2]->GetDouble(0);
        
        std::uniform_real_distribution<double> dist(min, max);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<n; ++i) res->d_vec.push_back(dist(g_rng));
        return res;
    }
    
    RValuePtr Builtin_Rnorm(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        double mean = 0, sd = 1;
        if (args.size() > 1) mean = args[1]->GetDouble(0);
        if (args.size() > 2) sd = args[2]->GetDouble(0);
        
        std::normal_distribution<double> dist(mean, sd);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<n; ++i) res->d_vec.push_back(dist(g_rng));
        return res;
    }
    
    RValuePtr Builtin_Sample(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        // sample(x, size, replace = FALSE)
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        
        // If x is a single number, sample from 1:x
         std::vector<int> indices;
         int N = x->Length();
         if (N == 1 && x->type == RType::INTEGER && x->GetInt(0) > 0) {
             N = x->GetInt(0);
             for(int i=0; i<N; ++i) indices.push_back(i); // 0..N-1
             // Logic branch: x isn't used as data, indices are
         } else {
             for(int i=0; i<N; ++i) indices.push_back(i);
         }
         
         int size = N;
         if (args.size() > 1) size = args[1]->GetInt(0);
         
         bool replace = false;
         RValuePtr rep_arg = GetArg(args, names, "replace", -1);
         if (rep_arg) replace = IsTrue(rep_arg);
         
         auto res = std::make_shared<RValue>(x->type);
         // If x was scalar int treated as 1:x range
         if (x->Length() == 1 && x->type == RType::INTEGER && x->GetInt(0) == N) {
             res->type = RType::INTEGER;
         }
         
         if (replace) {
             std::uniform_int_distribution<int> dist(0, N-1);
             for(int i=0; i<size; ++i) {
                 int idx = dist(g_rng);
                 if (x->Length() == 1 && x->type == RType::INTEGER && x->GetInt(0) == N) {
                     res->i_vec.push_back(idx + 1);
                 } else {
                     // Extract from x
                     if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
                     else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[idx]);
                     else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[idx]);
                 }
             }
         } else {
             if (size > N) return RR_Error("Cannot take a sample larger than the population when 'replace = FALSE'");
             std::shuffle(indices.begin(), indices.end(), g_rng);
             for(int i=0; i<size; ++i) {
                 int idx = indices[i];
                 if (x->Length() == 1 && x->type == RType::INTEGER && x->GetInt(0) == N) {
                     res->i_vec.push_back(idx + 1);
                 } else {
                      if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
                      else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[idx]);
                      else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[idx]);
                 }
             }
         }
         return res;
    }
    
    // --- LOGIC & SETS ---
    // ifelse(test, yes, no)
    RValuePtr Builtin_IfElse(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 3) return RR_Error("ifelse needs 3 args");
        RValuePtr test = args[0];
        RValuePtr yes = args[1];
        RValuePtr no = args[2];
        
        int n = test->Length(); // Result length determined by test (usually)
        // R recycles yes/no to match test
        
        // Output type? Try to match yes/no. Priority: Char > Double > Int
        RType outType = RType::INTEGER;
        if (yes->type == RType::CHARACTER || no->type == RType::CHARACTER) outType = RType::CHARACTER;
        else if (yes->type == RType::DOUBLE || no->type == RType::DOUBLE) outType = RType::DOUBLE;
        
        auto res = std::make_shared<RValue>(outType);
        
        for(int i=0; i<n; ++i) {
            // Test
            bool t = false;
            if (test->type == RType::LOGICAL || test->type == RType::INTEGER) t = (test->i_vec[i] != 0);
            else t = (test->GetDouble(i) != 0);
            
            // Pick value
            RValuePtr src = t ? yes : no;
            int src_idx = i % src->Length();
            
            if (outType == RType::DOUBLE) res->d_vec.push_back(src->GetDouble(src_idx));
            else if (outType == RType::INTEGER) res->i_vec.push_back(src->GetInt(src_idx)); // Simplified
            else if (outType == RType::CHARACTER) {
                if (src->type == RType::CHARACTER) res->s_vec.push_back(src->s_vec[src_idx]);
                else res->s_vec.push_back(std::to_string(src->GetDouble(src_idx)));
            }
        }
        return res;
    }
    
    // unique(x)
    RValuePtr Builtin_Unique(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        
        auto res = std::make_shared<RValue>(x->type);
        std::set<double> seen_d;
        std::set<int> seen_i;
        std::set<std::string> seen_s;
        
        bool seen_na_double = false;
        for(int i=0; i<x->Length(); ++i) {
            if (x->type == RType::DOUBLE) {
                double v = x->d_vec[i];
                if (std::isnan(v)) {
                    if (!seen_na_double) { seen_na_double = true; res->d_vec.push_back(v); }
                } else if (seen_d.find(v) == seen_d.end()) { seen_d.insert(v); res->d_vec.push_back(v); }
            } else if (x->type == RType::CHARACTER) {
                std::string v = x->s_vec[i];
                if (seen_s.find(v) == seen_s.end()) { seen_s.insert(v); res->s_vec.push_back(v); }
            } else {
                int v = x->i_vec[i];
                bool is_na = (v == R_INT_NA || v == R_LOGICAL_NA);
                if (is_na) {
                    if (seen_i.find(R_INT_NA) == seen_i.end()) { seen_i.insert(R_INT_NA); res->i_vec.push_back(v); }
                } else if (seen_i.find(v) == seen_i.end()) { seen_i.insert(v); res->i_vec.push_back(v); }
            }
        }
        return res;
    }
    
    // cumsum(x)
    RValuePtr Builtin_Cumsum(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE); // Force double
        double acc = 0;
        for(int i=0; i<x->Length(); ++i) {
            acc += x->GetDouble(i);
            res->d_vec.push_back(acc);
        }
        return res;
    }

    // seq(from = 1, to = 1, by = ((to - from)/(length.out - 1)), length.out = NULL, along.with = NULL, ...)
    RValuePtr Builtin_Seq(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        RValuePtr from = GetArg(args, names, "from", 0);
        RValuePtr to = GetArg(args, names, "to", 1);
        RValuePtr by = GetArg(args, names, "by", 2);
        RValuePtr len_out = GetArg(args, names, "length.out", 3);
        
        double d_from = from ? from->GetDouble(0) : 1.0;
        double d_to = to ? to->GetDouble(0) : 1.0;
        double d_by = by ? by->GetDouble(0) : ((d_to >= d_from) ? 1.0 : -1.0);
        
        // Logic for length.out (R: seq(from, to, length.out = n))
        if (len_out) {
            int n = len_out->GetInt(0);
            if (n <= 0) {
                auto res = std::make_shared<RValue>(RType::DOUBLE);
                return res;
            }
            if (n == 1) {
                auto res = std::make_shared<RValue>(RType::DOUBLE);
                res->d_vec.push_back(d_from);
                return res;
            }
            d_by = (d_to - d_from) / (n - 1);
        }
        
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        if (d_by == 0) { res->d_vec.push_back(d_from); return res; }
        
        if (d_by > 0) {
            for(double d = d_from; d <= d_to + 0.000001; d += d_by) res->d_vec.push_back(d);
        } else {
            for(double d = d_from; d >= d_to - 0.000001; d += d_by) res->d_vec.push_back(d);
        }
        return res;
    }
    
    // rep(x, times = 1, length.out = NA, each = 1) - preserve type; support length.out
    RValuePtr Builtin_Rep(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        RValuePtr x = GetArg(args, names, "x", 0);
        RValuePtr times = GetArg(args, names, "times", 1);
        RValuePtr len_out = GetArg(args, names, "length.out", -1);
        RValuePtr each_arg = GetArg(args, names, "each", 2);
        if (!each_arg) each_arg = GetArg(args, names, "each", -1);
        if (!x) return RR_Nil();
        int each = each_arg ? each_arg->GetInt(0) : 1;
        int t = times ? times->GetInt(0) : 1;
        if (each < 1) each = 1;
        if (t < 1) t = 1;
        int want_len = -1;
        if (len_out && len_out->Length() > 0) {
            int L = len_out->GetInt(0);
            if (L != R_INT_NA && L >= 0) want_len = L;
        }
        auto res = std::make_shared<RValue>(x->type);
        int xlen = x->Length();
        if (xlen == 0) return res;
        for (int i = 0; i < xlen; ++i) {
            for (int k = 0; k < each; ++k) {
                if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[i]);
                else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[i]);
                else if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[i]);
                else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
            }
        }
        int base_len = (int)res->Length();
        for (int rep = 1; rep < t; ++rep) {
            for (int i = 0; i < base_len; ++i) {
                if (x->type == RType::DOUBLE) res->d_vec.push_back(res->d_vec[i]);
                else if (x->type == RType::INTEGER) res->i_vec.push_back(res->i_vec[i]);
                else if (x->type == RType::LOGICAL) res->i_vec.push_back(res->i_vec[i]);
                else if (x->type == RType::CHARACTER) res->s_vec.push_back(res->s_vec[i]);
            }
        }
        if (want_len >= 0) {
            int cur = res->Length();
            if (cur > want_len) {
                if (x->type == RType::DOUBLE) res->d_vec.resize(want_len);
                else if (x->type == RType::INTEGER || x->type == RType::LOGICAL) res->i_vec.resize(want_len);
                else if (x->type == RType::CHARACTER) res->s_vec.resize(want_len);
            } else if (cur < want_len) {
                for (int i = cur; i < want_len; ++i) {
                    int j = i % cur;
                    if (x->type == RType::DOUBLE) res->d_vec.push_back(res->d_vec[j]);
                    else if (x->type == RType::INTEGER) res->i_vec.push_back(res->i_vec[j]);
                    else if (x->type == RType::LOGICAL) res->i_vec.push_back(res->i_vec[j]);
                    else if (x->type == RType::CHARACTER) res->s_vec.push_back(res->s_vec[j]);
                }
            }
        }
        return res;
    }

    RValuePtr Builtin_Colon(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         if (args.size() < 2) return RR_Error("Need 2 args for :");
         double a = args[0]->GetDouble(0);
         double b = args[1]->GetDouble(0);
         bool int_like = (!std::isnan(a) && !std::isnan(b) && a == std::floor(a) && b == std::floor(b) && a >= INT32_MIN && a <= INT32_MAX && b >= INT32_MIN && b <= INT32_MAX);
         if (int_like && a <= b && (b - a) <= 1000000) {
             auto res = std::make_shared<RValue>(RType::INTEGER);
             for (int i = (int)a; i <= (int)b; ++i) res->i_vec.push_back(i);
             return res;
         }
         if (int_like && a >= b && (a - b) <= 1000000) {
             auto res = std::make_shared<RValue>(RType::INTEGER);
             for (int i = (int)a; i >= (int)b; --i) res->i_vec.push_back(i);
             return res;
         }
         auto res = std::make_shared<RValue>(RType::DOUBLE);
         if (a <= b) {
             for (double i = a; i <= b + 1e-10; i += 1.0) res->d_vec.push_back(i);
         } else {
             for (double i = a; i >= b - 1e-10; i -= 1.0) res->d_vec.push_back(i);
         }
         return res;
    }
    
    // sequence(nvec) -> 1:n1, 1:n2 ...
    RValuePtr Builtin_Sequence(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr nvec = args[0];
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for(int i=0; i<nvec->Length(); ++i) {
            int n = nvec->GetInt(i);
            for(int k=1; k<=n; ++k) res->i_vec.push_back(k);
        }
        
        return res;
    }

    // --- INTROSPECTION ---
    RValuePtr Builtin_Length(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.push_back(args[0]->Length());
        return res;
    }
    
    RValuePtr Builtin_Names(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        if (args[0]->attributes.count("names")) return args[0]->attributes["names"];
        return RR_Nil();
    }
    
    RValuePtr Builtin_Class(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        if (args[0]->attributes.count("class")) {
            return args[0]->attributes["class"];
        }
        // Implicit classes
        if (args[0]->attributes.count("dim")) {
            res->s_vec.push_back("matrix");
             res->s_vec.push_back("array"); // R often returns both
        } else {
            if (args[0]->type == RType::DOUBLE) res->s_vec.push_back("numeric");
            else if (args[0]->type == RType::INTEGER) res->s_vec.push_back("integer");
            else if (args[0]->type == RType::LOGICAL) res->s_vec.push_back("logical");
            else if (args[0]->type == RType::CHARACTER) res->s_vec.push_back("character");
            else if (args[0]->type == RType::LIST) res->s_vec.push_back("list");
            else res->s_vec.push_back("unknown");
        }
        return res;
    }
    
    RValuePtr Builtin_Levels(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        if (args[0]->attributes.count("levels")) return args[0]->attributes["levels"];
        return RR_Nil();
    }
    
    // rev(x)
    RValuePtr Builtin_Rev(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(x->type);
        res->attributes = x->attributes; // preserve attrs? usually not all
        
        // Manual reverse copy
        int n = x->Length();
        if (x->type == RType::DOUBLE) { for(int i=n-1; i>=0; --i) res->d_vec.push_back(x->d_vec[i]); }
        else if (x->type == RType::INTEGER || x->type == RType::LOGICAL) { for(int i=n-1; i>=0; --i) res->i_vec.push_back(x->i_vec[i]); }
        else if (x->type == RType::CHARACTER) { for(int i=n-1; i>=0; --i) res->s_vec.push_back(x->s_vec[i]); }
        else if (x->type == RType::LIST) { for(int i=n-1; i>=0; --i) res->l_vec.push_back(x->l_vec[i]); }
        
        return res;
    }
    
    // --- STATISTICS ---
    // var(x), sd(x) => denominator n-1; na.rm supported
    RValuePtr Builtin_Var(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr narm_arg = GetArg(args, names, "na.rm", 1, nullptr);
        bool na_rm = narm_arg ? IsTrue(narm_arg) : false;
        RValuePtr x = nullptr;
        for (size_t i = 0; i < args.size(); ++i) { if (args[i] != narm_arg) { x = args[i]; break; } }
        if (!x || x->Length() == 0) {
            auto r = std::make_shared<RValue>(RType::DOUBLE); r->d_vec.push_back(NAReal()); return r;
        }
        int n = 0;
        double sum = 0;
        std::vector<double> vals;
        bool seen_na = false;
        for (int i = 0; i < x->Length(); ++i) {
            double d = x->GetDouble(i);
            if (std::isnan(d)) {
                if (na_rm) continue;
                seen_na = true;
                break;
            }
            vals.push_back(d);
            sum += d;
            n++;
        }
        if (seen_na && !na_rm) {
            auto r = std::make_shared<RValue>(RType::DOUBLE); r->d_vec.push_back(NAReal()); return r;
        }
        if (n < 2) {
            auto r = std::make_shared<RValue>(RType::DOUBLE); r->d_vec.push_back(NAReal()); return r;
        }
        double mean = sum / n;
        double sq_diff = 0;
        for (double v : vals) sq_diff += (v - mean) * (v - mean);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.push_back(sq_diff / (n - 1));
        return res;
    }
    
    RValuePtr Builtin_Sd(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        RValuePtr v = Builtin_Var(args, names, env);
        if (v->type == RType::ERROR) return v;
        if (!v->d_vec.empty() && !std::isnan(v->d_vec[0])) {
            v->d_vec[0] = std::sqrt(v->d_vec[0]);
        }
        return v;
    }

    // --- TYPE CHECKERS & CONSTRUCTORS ---
    RValuePtr Builtin_IsNumeric(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        bool is = (args[0]->type == RType::DOUBLE || args[0]->type == RType::INTEGER);
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(is ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsCharacter(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(args[0]->type == RType::CHARACTER ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsLogical(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(args[0]->type == RType::LOGICAL ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsList(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        bool is_list = (x->type == RType::LIST);
        if (!is_list && x->attributes.count("class")) {
            RValuePtr cls = x->attributes["class"];
            if (cls->type == RType::CHARACTER)
                for (const auto& c : cls->s_vec) if (c == "data.frame") { is_list = true; break; }
        }
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(is_list ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsNull(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(args[0]->type == RType::NIL ? 1 : 0);
        return res;
    }
    
    // Internal helper: allocate vector of given length (used by numeric/character/logical via their own REG)
    RValuePtr Builtin_AllocVector(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         int n = 0;
         if (!args.empty()) n = args[0]->GetInt(0);
         if (n < 0) n = 0;
         auto res = std::make_shared<RValue>(RType::DOUBLE);
         res->d_vec.resize(n, 0.0);
         return res;
    }
    
    RValuePtr Builtin_Numeric(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(n, 0.0);
        return res;
    }
    RValuePtr Builtin_Character(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        res->s_vec.resize(n, "");
        return res;
    }
    RValuePtr Builtin_Logical(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.resize(n, 0); // FALSE default
        return res;
    }

    // --- STRING OPS ---
    // paste(..., sep=" ", collapse=NULL)
    RValuePtr Builtin_Paste(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        RValuePtr sep_arg = GetArg(args, names, "sep", -1);
        std::string sep = " ";
        if (sep_arg && sep_arg->Length() > 0) sep = sep_arg->s_vec[0];
        
        // Collect args that are NOT sep/collapse
        std::vector<RValuePtr> inputs;
        int max_len = 0;
        for(size_t i=0; i<args.size(); ++i) {
             if (names.size() > i && (names[i] == "sep" || names[i] == "collapse")) continue;
             if (args[i] == sep_arg) continue;
             inputs.push_back(args[i]);
             if (args[i]->Length() > max_len) max_len = args[i]->Length();
        }
        
        if (inputs.empty()) return std::make_shared<RValue>(RType::CHARACTER);
        
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<max_len; ++i) {
            std::string s;
            for(size_t k=0; k<inputs.size(); ++k) {
                if (k > 0) s += sep;
                int idx = i % inputs[k]->Length(); // Recycle
                // Convert to string
                RValuePtr v = inputs[k];
                if (v->type == RType::CHARACTER) s += v->s_vec[idx];
                else if (v->type == RType::DOUBLE) s += std::to_string(v->d_vec[idx]); // format?
                else if (v->type == RType::INTEGER) s += std::to_string(v->i_vec[idx]);
                else if (v->type == RType::LOGICAL) s += (v->i_vec[idx] ? "TRUE" : "FALSE");
            }
            res->s_vec.push_back(s);
        }
        
        // collapse?
        RValuePtr col_arg = GetArg(args, names, "collapse", -1);
        if (col_arg) {
             std::string col_sep = col_arg->s_vec.empty() ? "" : col_arg->s_vec[0];
             std::string final_s;
             for(size_t i=0; i<res->s_vec.size(); ++i) {
                 if (i > 0) final_s += col_sep;
                 final_s += res->s_vec[i];
             }
             res->s_vec.clear();
             res->s_vec.push_back(final_s);
        }
        
        return res;
    }
    
    RValuePtr Builtin_Paste0(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        // paste(..., sep="")
        // Inject sep="" logic or just copy paste
        // For simplicity, just force sep="" in logic.
        // Or cleaner: modify args? No, args are RValues.
        // Just duplicate logic simplified:
        std::vector<RValuePtr> inputs;
        int max_len = 0;
        for(auto& a : args) {
            inputs.push_back(a);
            if (a->Length() > max_len) max_len = a->Length();
        }
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<max_len; ++i) {
            std::string s;
            for(size_t k=0; k<inputs.size(); ++k) {
                int idx = i % inputs[k]->Length(); 
                RValuePtr v = inputs[k];
                if (v->type == RType::CHARACTER) s += v->s_vec[idx];
                else if (v->type == RType::DOUBLE) {
                    // strip trailing zeros?
                    std::string tmp = std::to_string(v->d_vec[idx]);
                    s += tmp.substr(0, tmp.find_last_not_of('0')+1); 
                    if (s.back() == '.') s.pop_back();
                }
                else if (v->type == RType::INTEGER) s += std::to_string(v->i_vec[idx]);
                else if (v->type == RType::LOGICAL) s += (v->i_vec[idx] ? "TRUE" : "FALSE");
            }
            res->s_vec.push_back(s);
        }
        return res;
    }
    
    // --- MATRIX: DIAG ---
    // diag(x) -> if scalar, Identity(n). If matrix, extract diag. If vector, make diag matrix.
    RValuePtr Builtin_Diag(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        
        if (x->attributes.count("dim")) {
            // Extract Diagonal
            int nr = x->attributes["dim"]->GetInt(0);
            int nc = x->attributes["dim"]->GetInt(1);
            int n = std::min(nr, nc);
            auto res = std::make_shared<RValue>(x->type); // match type
            for(int i=0; i<n; ++i) {
                int idx = i * nr + i;
                if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
                // other types...
            }
            return res;
        } else {
             // Vector or Scalar
             if (x->Length() == 1 && x->type == RType::INTEGER) { // diag(3) -> 3x3 identity
                  int n = x->GetInt(0);
                  auto res = std::make_shared<RValue>(RType::DOUBLE);
                  res->d_vec.resize(n*n, 0.0);
                  for(int i=0; i<n; ++i) res->d_vec[i*n + i] = 1.0;
                  
                  auto dim = std::make_shared<RValue>(RType::INTEGER);
                  dim->i_vec = {n, n};
                  res->attributes["dim"] = dim;
                  return res;
             } else {
                  // Make Diagonal Matrix
                  int n = x->Length();
                  auto res = std::make_shared<RValue>(x->type);
                  // Resize to n*n, init 0
                  if (x->type == RType::DOUBLE) res->d_vec.resize(n*n, 0.0);
                  // ...
                  for(int i=0; i<n; ++i) {
                      if (x->type == RType::DOUBLE) res->d_vec[i*n + i] = x->d_vec[i];
                  }
                  auto dim = std::make_shared<RValue>(RType::INTEGER);
                  dim->i_vec = {n, n};
                  res->attributes["dim"] = dim;
                  return res;
             }
        }
    }
    
    // --- UTILS: Table, Head, Tail ---
    RValuePtr Builtin_Table(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        // Simplified table: 1 arg only
         if (args.empty()) return RR_Nil();
         RValuePtr x = args[0];
         std::map<std::string, int> counts;
         for(int i=0; i<x->Length(); ++i) {
             std::string s; // Key
             if (x->type == RType::CHARACTER) s = x->s_vec[i];
             else if (x->type == RType::INTEGER) s = std::to_string(x->i_vec[i]);
             else s = std::to_string(x->GetDouble(i));
             counts[s]++;
         }
         
         auto res = std::make_shared<RValue>(RType::INTEGER);
         auto nms = std::make_shared<RValue>(RType::CHARACTER);
         for(auto& pair : counts) {
             nms->s_vec.push_back(pair.first);
             res->i_vec.push_back(pair.second);
         }
         res->attributes["names"] = nms;
         auto cls = std::make_shared<RValue>(RType::CHARACTER);
         cls->s_vec.push_back("table");
         res->attributes["class"] = cls;
         return res;
    }
    
    RValuePtr Builtin_Head(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
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
    
    RValuePtr Builtin_Tail(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
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
    RValuePtr Builtin_Matrix(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        RValuePtr data = GetArg(args, names, "data", 0);
        RValuePtr nrow_arg = GetArg(args, names, "nrow", 1);
        RValuePtr ncol_arg = GetArg(args, names, "ncol", 2);
        
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
        
        // Fill Column Major (default)
        // Todo: support byrow
        int dlen = data->Length();
        if (dlen == 0) {
            for (int i = 0; i < total; ++i) res->d_vec[i] = NAReal(); // NA_real_
        } else {
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
    RValuePtr Builtin_Dim(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        if (args[0]->attributes.count("dim")) {
            return args[0]->attributes["dim"];
        }
        return RR_Nil();
    }


    // --- SUBSETTING ---
    // [ (x, i, j, ..., drop)
    RValuePtr Builtin_Subset(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("Empty subset");
        RValuePtr x = args[0];
        
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
            std::vector<int> r_idx;
            if (rows->type == RType::NIL) {
                for(int i=0; i<nr; ++i) r_idx.push_back(i);
            } else {
                for(int k=0; k<rows->Length(); ++k) {
                    int raw = rows->GetInt(k);
                    if (raw == R_INT_NA) { r_idx.push_back(-1); continue; }
                    int idx = raw - 1;
                    r_idx.push_back((idx >= 0 && idx < nr) ? idx : -1);
                }
            }
            std::vector<int> c_idx;
            if (cols->type == RType::NIL) {
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
                        else if (x->type == RType::CHARACTER) res->s_vec.push_back("NA");
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
            
            // Integer index: for each index value k, take x[k] (1-based)
            for(int k=0; k<idx->Length(); ++k) {
                int i = idx->GetInt(k) - 1;
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
                }
            }
            return res;
        }
        
        return x; 
    }
    
    // [[ (x, i) - Extract single element
    RValuePtr Builtin_Subset2(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need index for [[");
        RValuePtr x = args[0];
        RValuePtr idx = args[1];
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

    RValuePtr Builtin_NRow(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         if (args.empty()) return RR_Nil();
         if (args[0]->attributes.count("dim")) {
             auto r = std::make_shared<RValue>(RType::INTEGER);
             r->i_vec.push_back(args[0]->attributes["dim"]->GetInt(0));
             return r;
         }
         return RR_Nil();
    }

    RValuePtr Builtin_NCol(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
         if (args.empty()) return RR_Nil();
         if (args[0]->attributes.count("dim")) {
             auto r = std::make_shared<RValue>(RType::INTEGER);
             r->i_vec.push_back(args[0]->attributes["dim"]->GetInt(1));
             return r;
         }
         return RR_Nil();
    }

    // --- SUMMARIES ---
    
    // Generic Aggregator
    // Ops: 0=SUM, 1=MAX, 2=MIN, 3=ANY, 4=ALL
    // --- COERCION & CHECKS ---
    
    RValuePtr Builtin_AsLogical(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        for (int i = 0; i < x->Length(); ++i) res->i_vec.push_back(AsLogicalAt(x, i));
        return res;
    }
    
    RValuePtr Builtin_AsCharacter(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<x->Length(); ++i) {
             if (x->type == RType::DOUBLE) {
                 double d = x->d_vec[i];
                 if (IsNAReal(d)) res->s_vec.push_back("NA");
                 else if (std::isnan(d)) res->s_vec.push_back("NaN");
                 else if (std::isinf(d)) res->s_vec.push_back(d > 0 ? "Inf" : "-Inf");
                 else {
                     std::ostringstream oss;
                     oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
                     oss << std::setprecision(7) << d;
                     res->s_vec.push_back(oss.str());
                 }
             } else if (x->type == RType::INTEGER) {
                 if (x->i_vec[i] == R_INT_NA) res->s_vec.push_back("NA");
                 else res->s_vec.push_back(std::to_string(x->i_vec[i]));
             } else if (x->type == RType::LOGICAL) {
                 if (x->i_vec[i] == R_LOGICAL_NA) res->s_vec.push_back("NA");
                 else res->s_vec.push_back(x->i_vec[i] ? "TRUE" : "FALSE");
             }
             else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[i]);
        }
        return res;
    }

    RValuePtr Builtin_IsMatrix(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        int v = (args[0]->attributes.count("dim") > 0) ? 1 : 0;
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(v);
        return res;
    }

    RValuePtr Builtin_IsVector(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        // R says is.vector returns TRUE if it has no attributes other than names
        // Simplified: return true if atomic type and no dim?
        int v = (args[0]->attributes.count("dim") == 0) ? 1 : 0;
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(v);
        return res;
    }

    RValuePtr Builtin_IsNA(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
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
            }
            res->i_vec.push_back(is_na ? 1 : 0);
        }
        return res;
    }

    // --- INDEXING & PARALLEL ---

    // which(x, arr.ind=FALSE) - Simplified: just flat indices
    RValuePtr Builtin_Which(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        // arr.ind todo
        
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for(int i=0; i<x->Length(); ++i) {
            int lv = AsLogicalAt(x, i);
            if (lv == 1) { // TRUE; NA treated as FALSE, like R
                 res->i_vec.push_back(i + 1); // 1-based
            }
        }
        return res;
    }
    
    RValuePtr Builtin_WhichMin(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
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
    
    RValuePtr Builtin_WhichMax(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
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
    RValuePtr Builtin_Bind(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env, bool col_bind) {
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
    
    RValuePtr Builtin_Rbind(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Bind(args, names, env, false); }
    RValuePtr Builtin_Cbind(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Bind(args, names, env, true); }
    
    // t(x)
    RValuePtr Builtin_Transpose(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
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
    RValuePtr Builtin_MatMult(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
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
    RValuePtr Builtin_Row(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
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
    RValuePtr Builtin_Col(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
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
    RValuePtr Builtin_Factor(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        // Simplified: Strings -> Integers + Levels
        // 1. Collect unique strings
        std::vector<std::string> raw;
        for(int i=0; i<x->Length(); ++i) {
             if (x->type == RType::CHARACTER) raw.push_back(x->s_vec[i]);
             else if (x->type == RType::LOGICAL) raw.push_back(x->i_vec[i] == 1 ? "TRUE" : "FALSE");
             else raw.push_back(std::to_string(x->GetDouble(i)));
        }
        
        std::vector<std::string> levels = raw;
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
    RValuePtr Builtin_List(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
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
    RValuePtr Builtin_DataFrame(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
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
    RValuePtr Builtin_AsMatrix(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        if (x->attributes.count("dim")) return x; // already matrix?
        
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

    RValuePtr Builtin_PMax(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
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

    RValuePtr Builtin_PMin(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
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

    RValuePtr Builtin_Summary(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env, int op) {
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
    
    RValuePtr Builtin_Sum(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 0); }
    RValuePtr Builtin_Max(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 1); }
    RValuePtr Builtin_Min(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 2); }
    RValuePtr Builtin_Any(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 3); }
    RValuePtr Builtin_All(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_Summary(args, names, env, 4); }
    
    RValuePtr Builtin_Mean(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
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

    // sort(x, decreasing = FALSE, na.last = NA, ...)
    RValuePtr Builtin_Sort(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        RValuePtr dec_arg = GetArg(args, names, "decreasing", 1, nullptr);
        bool decreasing = dec_arg ? IsTrue(dec_arg) : false;
        int n = x->Length();
        auto res = std::make_shared<RValue>(x->type);
        if (x->type == RType::CHARACTER) {
            std::vector<std::pair<std::string, int>> paired;
            for (int i = 0; i < n; ++i) paired.push_back({x->s_vec[i], i});
            auto cmp = [decreasing](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b) {
                bool a_na = (a.first.empty() || a.first == "NA"), b_na = (b.first.empty() || b.first == "NA");
                if (a_na && b_na) return false;
                if (a_na) return !decreasing;
                if (b_na) return decreasing;
                return decreasing ? (a.first > b.first) : (a.first < b.first);
            };
            std::stable_sort(paired.begin(), paired.end(), cmp);
            for (int i = 0; i < n; ++i) res->s_vec.push_back(x->s_vec[paired[i].second]);
            return res;
        }
        std::vector<std::pair<double, int>> paired;
        for (int i = 0; i < n; ++i) paired.push_back({x->GetDouble(i), i});
        // na.last = TRUE: NA/NaN always at end (for both increasing and decreasing)
        auto cmp = [decreasing](const std::pair<double, int>& a, const std::pair<double, int>& b) {
            bool a_na = std::isnan(a.first), b_na = std::isnan(b.first);
            if (a_na && b_na) return false;
            if (a_na) return false;  // a (NA) never "less than" -> NA goes last
            if (b_na) return true;   // a (number) "less than" NA -> number goes first
            return decreasing ? (a.first > b.first) : (a.first < b.first);
        };
        std::stable_sort(paired.begin(), paired.end(), cmp);
        for (int i = 0; i < n; ++i) {
            int idx = paired[i].second;
            if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
            else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[idx]);
            else if (x->type == RType::LOGICAL) res->i_vec.push_back(x->i_vec[idx]);
        }
        return res;
    }

    // order(...) - returns integer permutation (1-based) so x[order(x)] is sorted
    RValuePtr Builtin_Order(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        RValuePtr dec_arg = GetArg(args, names, "decreasing", 1, nullptr);
        bool decreasing = dec_arg ? IsTrue(dec_arg) : false;
        int n = x->Length();
        std::vector<std::pair<double, int>> paired;
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

    // rank(x, na.last = TRUE, ties.method = "average")
    RValuePtr Builtin_Rank(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.empty()) return RR_Nil();
        RValuePtr x = args[0];
        int n = x->Length();
        std::vector<std::pair<double, int>> paired;
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
            double avg_rank = (i + j + 2) / 2.0; // 1-based average
            for (int k = i; k <= j; ++k) res->d_vec[paired[k].second] = avg_rank;
            i = j;
        }
        return res;
    }

    // max.col(m, ties.method = "random") - index of max in each row (1-based)
    RValuePtr Builtin_MaxCol(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
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
    static RValuePtr Builtin_RowColOp(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env, bool row_op, bool do_mean) {
        if (args.empty()) return RR_Nil();
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
    RValuePtr Builtin_RowSums(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, true, false); }
    RValuePtr Builtin_ColSums(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, false, false); }
    RValuePtr Builtin_RowMeans(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, true, true); }
    RValuePtr Builtin_ColMeans(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) { return Builtin_RowColOp(args, names, env, false, true); }

    // x$name - extract element by name from list/data.frame
    RValuePtr Builtin_Dollar(const std::vector<RValuePtr>& args, const std::vector<std::string>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Nil();
        RValuePtr x = args[0];
        RValuePtr name_val = args[1];
        if (x->type != RType::LIST && !x->attributes.count("class")) return RR_Nil();
        std::string key;
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

    void InitGlobalEnv(RValuePtr env) {
        // Simple registrations
        #define REG(name, func) { auto f = std::make_shared<RValue>(RType::BUILTIN); f->builtin = func; Define(name, f, env); }
        
        REG("+", Builtin_Add);
        REG("-", Builtin_Diff);
        REG("*", Builtin_Mul);
        REG("/", Builtin_Div);
        REG("^", Builtin_Pow);
        REG("%%", Builtin_Mod);
        REG("%/%", Builtin_IntDiv);
        REG("%*%", Builtin_MatMult);
        
        REG("<", Builtin_Lt);
        REG(">", Builtin_Gt);
        REG("<=", Builtin_Le);
        REG(">=", Builtin_Ge);
        REG("==", Builtin_Eq);
        REG("!=", Builtin_Ne);
        
        REG("log", Builtin_Log);
        REG("exp", Builtin_Exp);
        REG("sqrt", Builtin_Sqrt);
        REG("abs", Builtin_Abs);
        REG("round", Builtin_Round);
        
        REG("&", Builtin_And);
        REG("|", Builtin_Or);
        REG("!", Builtin_Not);
        
        REG("c", Builtin_C);
        REG(":", Builtin_Colon);
        
        REG("seq", Builtin_Seq);
        REG("rep", Builtin_Rep);
        REG("sequence", Builtin_Sequence);
        
        REG("matrix", Builtin_Matrix);
        REG("as.matrix", Builtin_AsMatrix);
        REG("dim", Builtin_Dim);
        REG("nrow", Builtin_NRow);
        REG("ncol", Builtin_NCol);
        REG("t", Builtin_Transpose);
        REG("row", Builtin_Row);
        REG("col", Builtin_Col);
        
        REG("length", Builtin_Length);
        REG("assign", Builtin_Assign);
        REG("names", Builtin_Names);
        REG("class", Builtin_Class);
        REG("levels", Builtin_Levels);
        REG("rev", Builtin_Rev);
        
        REG("sd", Builtin_Sd);
        REG("var", Builtin_Var);
        
        REG("set.seed", Builtin_SetSeed);
        REG("runif", Builtin_Runif);
        REG("rnorm", Builtin_Rnorm);
        REG("sample", Builtin_Sample);
        
        REG("lapply", Builtin_Lapply);
        REG("sapply", Builtin_Sapply);
        
        REG("nchar", Builtin_Nchar);
        REG("substr", Builtin_Substr);
        
        REG("match", Builtin_Match);
        REG("%in%", Builtin_In);
        
        REG("ifelse", Builtin_IfElse);
        REG("unique", Builtin_Unique);
        REG("cumsum", Builtin_Cumsum);
        
        REG("is.numeric", Builtin_IsNumeric);
        REG("is.character", Builtin_IsCharacter);
        REG("is.logical", Builtin_IsLogical);
        REG("is.list", Builtin_IsList);
        REG("is.null", Builtin_IsNull);
        
        REG("numeric", Builtin_Numeric);
        REG("character", Builtin_Character);
        REG("logical", Builtin_Logical);
        
        REG("paste", Builtin_Paste);
        REG("paste0", Builtin_Paste0);
        
        REG("diag", Builtin_Diag);
        
        REG("table", Builtin_Table);
        REG("head", Builtin_Head);
        REG("tail", Builtin_Tail);
        
        REG("list", Builtin_List);
        REG("factor", Builtin_Factor);
        REG("data.frame", Builtin_DataFrame);
        
        REG("sum", Builtin_Sum);
        REG("max", Builtin_Max);
        REG("min", Builtin_Min);
        REG("any", Builtin_Any);
        REG("all", Builtin_All);
        REG("mean", Builtin_Mean);
        
        REG("[", Builtin_Subset);
        REG("[[", Builtin_Subset2);
        
        REG("as.logical", Builtin_AsLogical);
        REG("as.character", Builtin_AsCharacter);
        REG("is.matrix", Builtin_IsMatrix);
        REG("is.vector", Builtin_IsVector);
        REG("is.na", Builtin_IsNA);
        REG("sort", Builtin_Sort);
        REG("order", Builtin_Order);
        REG("rank", Builtin_Rank);
        
        REG("max.col", Builtin_MaxCol);
        
        REG("cbind", Builtin_Cbind);
        REG("rbind", Builtin_Rbind);
        
        REG("which", Builtin_Which);
        REG("which.min", Builtin_WhichMin);
        REG("which.max", Builtin_WhichMax);
        REG("pmax", Builtin_PMax);
        REG("pmin", Builtin_PMin);
        
        REG("rowSums", Builtin_RowSums);
        REG("colSums", Builtin_ColSums);
        REG("rowMeans", Builtin_RowMeans);
        REG("colMeans", Builtin_ColMeans);
        
        REG("$", Builtin_Dollar);
    }

    RValuePtr Eval(RValuePtr exp, RValuePtr env) {
        if (!exp) return RR_Nil();
        
        switch (exp->type) {
            case RType::SYMBOL:
                return Lookup(exp->sym_name, env);
                
            case RType::INTEGER:
            case RType::DOUBLE:
            case RType::LOGICAL:
            case RType::CHARACTER:
                return exp; // Self-evaluating
                
            case RType::LIST: {
                if (exp->l_vec.empty()) return RR_Nil();
                
                RValuePtr head = exp->l_vec[0];
                
                // Handle special forms based on symbol name BEFORE eval ?
                if (head->type == RType::SYMBOL) {
                    if (head->sym_name == "<-" || head->sym_name == "=") {
                         if (exp->l_vec.size() != 3) return RR_Error("Bad assignment");
                         RValuePtr lhs = exp->l_vec[1];
                         RValuePtr val = Eval(exp->l_vec[2], env);
                         if (val->type == RType::ERROR) return val;
                         std::string target_name;
                         RValuePtr x;
                         std::string sub_op;
                         std::vector<RValuePtr> index_vals;
                         if (lhs->type == RType::SYMBOL) {
                             target_name = lhs->sym_name;
                             Define(target_name, val, env);
                             return val;
                         }
                         if (lhs->type == RType::LIST && !lhs->l_vec.empty() && lhs->l_vec[0]->type == RType::SYMBOL) {
                             sub_op = lhs->l_vec[0]->sym_name;
                             if (sub_op == "[" || sub_op == "[[" || sub_op == "$") {
                                 if (lhs->l_vec.size() < 2) return RR_Error("Bad subassignment");
                                 RValuePtr target = lhs->l_vec[1];
                                 if (target->type != RType::SYMBOL) return RR_Error("invalid assignment target");
                                 target_name = target->sym_name;
                                 x = Lookup(target_name, env);
                                 if (!x) return RR_Error("object '" + target_name + "' not found");
                                 for (size_t i = 2; i < lhs->l_vec.size(); ++i) {
                                     if (sub_op == "$" && i == 2 && lhs->l_vec[i]->type == RType::SYMBOL)
                                         index_vals.push_back(lhs->l_vec[i]); // name: use symbol, not evaluated
                                     else {
                                         RValuePtr idx = Eval(lhs->l_vec[i], env);
                                         if (idx->type == RType::ERROR) return idx;
                                         index_vals.push_back(idx);
                                     }
                                 }
                                 std::string err;
                                 RValuePtr modified = SubAssign(x, sub_op, index_vals, val, err);
                                 if (!err.empty()) return RR_Error(err);
                                 if (!modified) return RR_Error("subassignment failed");
                                 Define(target_name, modified, env);
                                 return modified;
                             }
                         }
                         return RR_Error("LHS must be a symbol or subset expression (e.g. x[i], x$name)");
                    }
                    if (head->sym_name == "if") {
                        // (if cond then else)
                        RValuePtr cond = Eval(exp->l_vec[1], env);
                        std::string cerr;
                        bool c = ConditionToBoolOrError(cond, cerr);
                        if (!cerr.empty()) return RR_Error(cerr);
                        if (c) return Eval(exp->l_vec[2], env);
                        if (exp->l_vec.size() > 3) return Eval(exp->l_vec[3], env);
                        return RR_Nil();
                    }
                    if (head->sym_name == "while") {
                        // (while cond body)
                        RValuePtr cond_expr = exp->l_vec[1];
                        RValuePtr body = exp->l_vec[2];
                        RValuePtr last = RR_Nil();
                        while(true) {
                            RValuePtr cond = Eval(cond_expr, env);
                            std::string cerr;
                            bool c = ConditionToBoolOrError(cond, cerr);
                            if (!cerr.empty()) return RR_Error(cerr);
                            if (!c) break;
                            last = Eval(body, env);
                        }
                        return last;
                    }
                    if (head->sym_name == "for") {
                        RValuePtr seq_expr = exp->l_vec[2];
                        RValuePtr body = exp->l_vec[3];
                        RValuePtr seq = Eval(seq_expr, env);
                        if (seq->type == RType::ERROR) return seq;
                        std::string var_name = exp->l_vec[1]->sym_name;
                        RValuePtr last = RR_Nil();
                        int n = seq->Length();
                        for(int i=0; i<n; ++i) {
                            auto val = std::make_shared<RValue>(seq->type);
                            if (seq->type == RType::INTEGER || seq->type == RType::LOGICAL) val->i_vec.push_back(seq->i_vec[i]);
                            else if (seq->type == RType::DOUBLE) val->d_vec.push_back(seq->d_vec[i]);
                            else if (seq->type == RType::CHARACTER) val->s_vec.push_back(seq->s_vec[i]);
                            Define(var_name, val, env);
                            last = Eval(body, env);
                        }
                        return last;
                    }
                    if (head->sym_name == "function") {
                        // (function (args) body)
                        auto closure = std::make_shared<RValue>(RType::CLOSURE);
                        closure->formals = exp->l_vec[1];
                        closure->body = exp->l_vec[2];
                        closure->env = env; // Capture
                        return closure;
                    }
                    if (head->sym_name == "{") {
                        RValuePtr res = RR_Nil();
                        for(size_t i=1; i<exp->l_vec.size(); ++i) {
                            res = Eval(exp->l_vec[i], env);
                        }
                        return res;
                    }
                }
                
                // Function Call
                RValuePtr func = Eval(head, env);
                if (func->type == RType::ERROR) return func;
                
                // Eval Args
                std::vector<RValuePtr> args;
                std::vector<std::string> arg_names;
                
                // Check if call has "names"
                RValuePtr names_attr = nullptr;
                if (exp->attributes.count("names")) names_attr = exp->attributes["names"];
                
                for(size_t i=1; i<exp->l_vec.size(); ++i) {
                    RValuePtr a = Eval(exp->l_vec[i], env);
                    if (a->type == RType::ERROR) return a;
                    args.push_back(a);
                    
                    // Collect name
                    if (names_attr && i < names_attr->s_vec.size()) {
                        arg_names.push_back(names_attr->s_vec[i]);
                    } else {
                        arg_names.push_back("");
                    }
                }
                
                if (func->type == RType::BUILTIN) {
                    return func->builtin(args, arg_names, env);
                }
                
                if (func->type == RType::CLOSURE) {
                    // Create new environment
                    auto new_env = std::make_shared<RValue>(RType::ENV);
                    new_env->parent_env = func->env; // Lexical scoping
                    
                    // Bind Arguments (named + positional via GetArg)
                    RValuePtr formals = func->formals;
                    for (size_t i = 0; i < formals->l_vec.size(); ++i) {
                        RValuePtr sym = formals->l_vec[i];
                        std::string name = sym->sym_name;
                        RValuePtr val = GetArg(args, arg_names, name, (int)i, RR_Nil());
                        Define(name, val, new_env);
                    }
                    
                    // Eval Body
                    return Eval(func->body, new_env);
                }
                
                return RR_Error("Not a function");
            }
            default:
                return exp;
        }
    }


    // --- TO STRING ---
    std::string ToString(RValuePtr v) {
        if (!v) return "NULL";
        if (v->type == RType::NIL) return "NULL";
        if (v->type == RType::ERROR) return "Error: " + v->sym_name;
        
        auto fmtDouble = [](double d) -> std::string {
            if (IsNAReal(d)) return "NA";
            if (std::isnan(d)) return "NaN";
            if (std::isinf(d)) return d > 0 ? "Inf" : "-Inf";
            std::ostringstream oss;
            oss.setf(std::ios::fmtflags(0), std::ios::floatfield);
            oss << std::setprecision(7) << d;
            return oss.str();
        };
        auto fmtInt = [](int x) -> std::string {
            if (x == R_INT_NA) return "NA";
            return std::to_string(x);
        };
        auto fmtLgl = [](int x) -> std::string {
            if (x == R_LOGICAL_NA) return "NA";
            return x ? "TRUE" : "FALSE";
        };

        std::string s = "";
        
        // Prefix with [1] if vector?
        if (v->type == RType::DOUBLE || v->type == RType::INTEGER || v->type == RType::LOGICAL || v->type == RType::CHARACTER) {
            // Simple Print
             s += "[1] ";
             for(int i=0; i<v->Length(); ++i) {
                 if (i > 0) s += " ";
                 if (v->type == RType::DOUBLE) s += fmtDouble(v->d_vec[i]);
                 else if (v->type == RType::INTEGER) s += fmtInt(v->i_vec[i]);
                 else if (v->type == RType::LOGICAL) s += fmtLgl(v->i_vec[i]);
                 else if (v->type == RType::CHARACTER) s += "\"" + v->s_vec[i] + "\"";
             }
             return s;
        }
        
        if (v->type == RType::LIST) {
            for(int i=0; i<v->Length(); ++i) {
                s += "[[" + std::to_string(i+1) + "]]\n";
                s += ToString(v->l_vec[i]) + "\n";
            }
            return s;
        }
        
        if (v->type == RType::CLOSURE) return "<function>";
        if (v->type == RType::BUILTIN) return "<builtin>";
        if (v->type == RType::ENV) return "<environment>";
        
        return "<unknown>";
    }
}
