#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    RValuePtr Builtin_And(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
    RValuePtr Builtin_Or(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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

    RValuePtr Builtin_Not(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
    RValuePtr ApplyFunction(RValuePtr func, const MiniVector<RValuePtr>& args, RValuePtr env) {
          if (func->type == RType::BUILTIN) {
              MiniVector<MiniString> names; names.resize(args.size()); // No names for now
              return func->builtin(args, names, env);
          }
          if (func->type == RType::CLOSURE) {
               MiniVector<MiniString> names; names.resize(args.size());
               return CallClosure(func, args, names);
          }
          return RR_Error("Not a function");
    }

    // --- FUNCTIONAL OPS ---
    RValuePtr Builtin_Lapply(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
             
             MiniVector<RValuePtr> fargs = { elem };
             // Additional args?
             RValuePtr val = ApplyFunction(FUN, fargs, env);
             if (val->type == RType::ERROR) return val;
             res->l_vec.push_back(val);
        }
        return res;
    }
    
    RValuePtr Builtin_Sapply(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
    

} // namespace Evaluator
