#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    // --- STRING OPS ---
    RValuePtr Builtin_Nchar(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("nchar() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::INTEGER);
        for(int i=0; i<x->Length(); ++i) {
            if (x->type == RType::CHARACTER) res->i_vec.push_back(x->s_vec[i].length());
            else res->i_vec.push_back(0); // Coerce?
        }
        return res;
    }
    
    RValuePtr Builtin_Substr(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        // substr(x, start, stop)
        if (args.size() < 3) return RR_Error("substr(x, start, stop)");
        RValuePtr x = args[0];
        int start = args[1]->GetInt(0);
        int stop = args[2]->GetInt(0);
        
        auto res = std::make_shared<RValue>(RType::CHARACTER);
        for(int i=0; i<x->Length(); ++i) {
            MiniString s = "";
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
    RValuePtr Builtin_Match(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
                bool x_chr = (x->type == RType::CHARACTER), t_chr = (table->type == RType::CHARACTER);
                if (x_chr && t_chr) eq = (x->s_vec[i] == table->s_vec[k]);
                else if (!x_chr && !t_chr) {
                    // Numeric/logical: NA matches NA and NaN matches NaN, as in R
                    double a = x->GetDouble(i), b = table->GetDouble(k);
                    if (std::isnan(a) || std::isnan(b))
                        eq = std::isnan(a) && std::isnan(b) && IsNAReal(a) == IsNAReal(b);
                    else
                        eq = (a == b);
                }
                
                if (eq) { found = k + 1; break; } // 1-based index
            }
            res->i_vec.push_back(found != -1 ? found : R_INT_NA);
        }
        return res;
    }

    RValuePtr Builtin_In(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        // x %in% table -> match(x, table, nomatch=0) > 0
        RValuePtr m = Builtin_Match(args, names, env);
        if (m->type == RType::ERROR) return m;
        
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        // %in% is never NA: no match is simply FALSE
        for(int v : m->i_vec) res->i_vec.push_back((v != R_INT_NA && v > 0) ? 1 : 0);
        return res;
    }

    // --- LOGIC OPS ---

    // --- RANDOMNESS ---

} // namespace Evaluator
