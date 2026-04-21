#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    RValuePtr Builtin_IfElse(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 3) return RR_Error("ifelse needs 3 args");
        RValuePtr test = args[0];
        RValuePtr yes = args[1];
        RValuePtr no = args[2];

        int n = test->Length();
        if (n == 0) return std::make_shared<RValue>(RType::LOGICAL);

        int yes_len = yes->Length();
        int no_len = no->Length();
        if (yes_len == 0) return RR_Error("ifelse 'yes' has length zero");
        if (no_len == 0) return RR_Error("ifelse 'no' has length zero");

        // Output type: character > double > integer > logical
        RType outType = RType::LOGICAL;
        if (yes->type == RType::CHARACTER || no->type == RType::CHARACTER) outType = RType::CHARACTER;
        else if (yes->type == RType::DOUBLE || no->type == RType::DOUBLE) outType = RType::DOUBLE;
        else if (yes->type == RType::INTEGER || no->type == RType::INTEGER) outType = RType::INTEGER;

        auto res = std::make_shared<RValue>(outType);

        for (int i = 0; i < n; ++i) {
            int lv = AsLogicalAt(test, i);
            if (lv == R_LOGICAL_NA) {
                // NA test -> NA in result
                if (outType == RType::DOUBLE) res->d_vec.push_back(NAReal());
                else if (outType == RType::CHARACTER) res->s_vec.push_back(R_STRING_NA);
                else res->i_vec.push_back(R_LOGICAL_NA);
                continue;
            }
            RValuePtr src = lv ? yes : no;
            int src_len = lv ? yes_len : no_len;
            int src_idx = i % src_len;
            if (outType == RType::DOUBLE) res->d_vec.push_back(src->GetDouble(src_idx));
            else if (outType == RType::CHARACTER) res->s_vec.push_back(AsStringAt(src, src_idx));else res->i_vec.push_back(src->GetInt(src_idx));
        }
        return res;
    }
    
    // unique(x)
    RValuePtr Builtin_Unique(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("unique() requires at least 1 argument");
        RValuePtr x = args[0];
        
        auto res = std::make_shared<RValue>(x->type);
        std::set<double> seen_d;
        std::set<int> seen_i;
        std::set<MiniString> seen_s;
        
        bool seen_na_double = false;
        for(int i=0; i<x->Length(); ++i) {
            if (x->type == RType::DOUBLE) {
                double v = x->d_vec[i];
                if (std::isnan(v)) {
                    if (!seen_na_double) { seen_na_double = true; res->d_vec.push_back(v); }
                } else if (seen_d.find(v) == seen_d.end()) { seen_d.insert(v); res->d_vec.push_back(v); }
            } else if (x->type == RType::CHARACTER) {
                MiniString v = x->s_vec[i];
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
    
    // duplicated(x, fromLast = FALSE) – logical vector, TRUE for elements that have appeared before
    RValuePtr Builtin_Duplicated(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("duplicated() requires at least 1 argument");
        RValuePtr x = args[0];
        RValuePtr fromLast_arg = GetArg(args, names, "fromLast", 1, nullptr);
        bool from_last = fromLast_arg ? IsTrue(fromLast_arg) : false;

        auto res = std::make_shared<RValue>(RType::LOGICAL);
        int n = x->Length();
        res->i_vec.resize(n);

        std::set<double> seen_d;
        std::set<int> seen_i;
        std::set<MiniString> seen_s;
        bool seen_na_double = false;
        bool seen_na_int = false;

        auto mark = [&](int i) {
            if (x->type == RType::DOUBLE) {
                double v = x->d_vec[i];
                if (std::isnan(v)) {
                    if (seen_na_double) res->i_vec[i] = 1;
                    else { seen_na_double = true; res->i_vec[i] = 0; }
                } else {
                    if (seen_d.find(v) != seen_d.end()) res->i_vec[i] = 1;
                    else { seen_d.insert(v); res->i_vec[i] = 0; }
                }
            } else if (x->type == RType::CHARACTER) {
                const MiniString& v = x->s_vec[i];
                if (seen_s.find(v) != seen_s.end()) res->i_vec[i] = 1;
                else { seen_s.insert(v); res->i_vec[i] = 0; }
            } else { // INTEGER or LOGICAL
                int v = x->i_vec[i];
                bool is_na = (v == R_INT_NA || v == R_LOGICAL_NA);
                if (is_na) {
                    if (seen_na_int) res->i_vec[i] = 1;
                    else { seen_na_int = true; res->i_vec[i] = 0; }
                } else {
                    if (seen_i.find(v) != seen_i.end()) res->i_vec[i] = 1;
                    else { seen_i.insert(v); res->i_vec[i] = 0; }
                }
            }
        };

        if (!from_last) {
            for (int i = 0; i < n; ++i) mark(i);
        } else {
            for (int i = n - 1; i >= 0; --i) mark(i);
        }
        return res;
    }
    
    // cumsum(x)
    RValuePtr Builtin_Cumsum(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("cumsum() requires at least 1 argument");
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
    RValuePtr Builtin_Seq(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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

        double eps = (std::abs(d_to) + std::abs(d_from)) * 1e-10 + 1e-15;
        if (d_by > 0) {
            for(double d = d_from; d <= d_to + eps; d += d_by) res->d_vec.push_back(d);
        } else {
            for(double d = d_from; d >= d_to - eps; d += d_by) res->d_vec.push_back(d);
        }
        return res;
    }
    
    // rep(x, times = 1, length.out = NA, each = 1) - preserve type; support length.out
    RValuePtr Builtin_Rep(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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

    RValuePtr Builtin_Colon(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
    RValuePtr Builtin_Sequence(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
    RValuePtr Builtin_Length(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("length() requires at least 1 argument");
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.push_back(args[0]->Length());
        return res;
    }
    
    RValuePtr Builtin_Names(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        if (args[0]->attributes.count("names")) return args[0]->attributes["names"];
        return RR_Nil();
    }
    
    RValuePtr Builtin_Class(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("class() requires at least 1 argument");
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
    
    RValuePtr Builtin_Levels(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Nil();
        if (args[0]->attributes.count("levels")) return args[0]->attributes["levels"];
        return RR_Nil();
    }
    
    // rev(x)
    RValuePtr Builtin_Rev(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("rev() requires at least 1 argument");
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
    RValuePtr Builtin_Var(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("var() requires at least 1 argument");
        RValuePtr narm_arg = GetArg(args, names, "na.rm", 1, nullptr);
        bool na_rm = narm_arg ? IsTrue(narm_arg) : false;
        RValuePtr x = nullptr;
        for (size_t i = 0; i < args.size(); ++i) { if (args[i] != narm_arg) { x = args[i]; break; } }
        if (!x || x->Length() == 0) {
            auto r = std::make_shared<RValue>(RType::DOUBLE); r->d_vec.push_back(NAReal()); return r;
        }
        int n = 0;
        double sum = 0;
        MiniVector<double> vals;
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
    
    RValuePtr Builtin_Sd(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        RValuePtr v = Builtin_Var(args, names, env);
        if (v->type == RType::ERROR) return v;
        if (!v->d_vec.empty() && !std::isnan(v->d_vec[0])) {
            v->d_vec[0] = std::sqrt(v->d_vec[0]);
        }
        return v;
    }

    // --- TYPE CHECKERS & CONSTRUCTORS ---
    RValuePtr Builtin_IsNumeric(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.numeric() requires at least 1 argument");
        bool is = (args[0]->type == RType::DOUBLE || args[0]->type == RType::INTEGER);
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(is ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsCharacter(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.character() requires at least 1 argument");
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(args[0]->type == RType::CHARACTER ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsLogical(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.logical() requires at least 1 argument");
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(args[0]->type == RType::LOGICAL ? 1 : 0);
        return res;
    }
    RValuePtr Builtin_IsList(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.list() requires at least 1 argument");
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
    RValuePtr Builtin_IsNull(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("is.null() requires at least 1 argument");
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.push_back(args[0]->type == RType::NIL ? 1 : 0);
        return res;
    }
    
    // Internal helper: allocate vector of given length (used by numeric/character/logical via their own REG)
    RValuePtr Builtin_AllocVector(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
         int n = 0;
         if (!args.empty()) n = args[0]->GetInt(0);
         if (n < 0) n = 0;
         auto res = std::make_shared<RValue>(RType::DOUBLE);
         res->d_vec.resize(n, 0.0);
         return res;
    }
    
    RValuePtr Builtin_Numeric(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(n, 0.0);
        return res;
    }
    RValuePtr Builtin_Character(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        res->s_vec.resize(n, "");
        return res;
    }
    RValuePtr Builtin_Logical(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        res->i_vec.resize(n, 0); // FALSE default
        return res;
    }

    // --- STRING OPS ---
    // One element as paste() renders it: like as.character(), but NA becomes the text "NA"
    static MiniString PasteElem(RValuePtr v, int idx) {
        MiniString s = AsStringAt(v, idx);
        return IsNAString(s) ? MiniString("NA") : s;
    }

    // paste(..., sep=" ", collapse=NULL)
    RValuePtr Builtin_Paste(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        RValuePtr sep_arg = GetArg(args, names, "sep", -1);
        MiniString sep = " ";
        if (sep_arg && sep_arg->Length() > 0) sep = sep_arg->s_vec[0];
        
        // Collect args that are NOT sep/collapse
        MiniVector<RValuePtr> inputs;
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
            MiniString s;
            for(size_t k=0; k<inputs.size(); ++k) {
                if (k > 0) s += sep;
                int idx = i % inputs[k]->Length(); // Recycle
                // Convert to string
                s += PasteElem(inputs[k], idx);
            }
            res->s_vec.push_back(s);
        }
        
        // collapse?
        RValuePtr col_arg = GetArg(args, names, "collapse", -1);
        if (col_arg) {
             MiniString col_sep = col_arg->s_vec.empty() ? "" : col_arg->s_vec[0];
             MiniString final_s;
             for(size_t i=0; i<res->s_vec.size(); ++i) {
                 if (i > 0) final_s += col_sep;
                 final_s += res->s_vec[i];
             }
             res->s_vec.clear();
             res->s_vec.push_back(final_s);
        }
        
        return res;
    }
    
    RValuePtr Builtin_Paste0(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        // paste(..., sep="")
        // Inject sep="" logic or just copy paste
        // For simplicity, just force sep="" in logic.
        // Or cleaner: modify args? No, args are RValues.
        // Just duplicate logic simplified:
        MiniVector<RValuePtr> inputs;
        int max_len = 0;
        for(auto& a : args) {
            inputs.push_back(a);
            if (a->Length() > max_len) max_len = a->Length();
        }
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<max_len; ++i) {
            MiniString s;
            for(size_t k=0; k<inputs.size(); ++k) {
                int idx = i % inputs[k]->Length();
                s += PasteElem(inputs[k], idx);
            }
            res->s_vec.push_back(s);
        }
        return res;
    }
    
    // --- MATRIX: DIAG ---
    // diag(x) -> if scalar, Identity(n). If matrix, extract diag. If vector, make diag matrix.
    RValuePtr Builtin_Diag(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("diag() requires at least 1 argument");
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
    RValuePtr Builtin_Table(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        // Simplified table: 1 arg only
         if (args.empty()) return RR_Nil();
         RValuePtr x = args[0];
         std::map<MiniString, int> counts;
         for(int i=0; i<x->Length(); ++i) {
             MiniString s = AsStringAt(x, i); // Key
             if (IsNAString(s)) continue; // table() drops NA by default
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
    

} // namespace Evaluator
