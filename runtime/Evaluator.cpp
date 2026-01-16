#include "Evaluator.hpp"
#include <iostream>
#include <cmath>

namespace Evaluator {

    // Helper: Is a value TRUE?
    bool IsTrue(RValuePtr v) {
        if (!v) return false;
        if (v->type == RType::LOGICAL || v->type == RType::INTEGER) {
            return !v->i_vec.empty() && v->i_vec[0] != 0 && v->i_vec[0] != -1; // -1 is NA?
        }
        return false;
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

    // Builtin Implementations
    RValuePtr Builtin_Assign(const std::vector<RValuePtr>& args, RValuePtr env) {
        // args[0] should be symbol (unevaluated usually, but if Parser produced SYMBOL, Eval might have evaluated it?)
        // Wait, standard Eval evaluates args before call?
        // Special form needed for <-.
        // We'll handle <- in Eval specially or assume we passed raw args if flag set?
        // Let's handle <- in Eval loop for simplicity of special forms.
        return RR_Nil(); 
    }
    
    // Arithmetic +
    RValuePtr Builtin_Add(const std::vector<RValuePtr>& args, RValuePtr env) {
        // Vectorized add
        if (args.size() < 2) return RR_Error("Need 2 args for +");
        // Simplified: coerce to double and add
        // Recycling rule: max length
        int lenA = args[0]->Length();
        int lenB = args[1]->Length();
        int N = std::max(lenA, lenB);
        
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        
        for(int i=0; i<N; ++i) {
            double a = (lenA > 0) ? args[0]->d_vec[i % lenA] : 0; // Unsafe access if not DOUBLE?
            // Need wrappers GetDouble(arg, idx)
            // Assuming parser/lexer made numbers DOUBLE
            double b = (lenB > 0) ? args[1]->d_vec[i % lenB] : 0;
            res->d_vec[i] = a + b;
        }
        return res;
    }
    
    RValuePtr Builtin_Diff(const std::vector<RValuePtr>& args, RValuePtr env) {
         if (args.size() < 2) return RR_Error("Need 2 args for -");
         int lenA = args[0]->Length();
         int lenB = args[1]->Length();
         int N = std::max(lenA, lenB);
         auto res = std::make_shared<RValue>(RType::DOUBLE);
         res->d_vec.resize(N);
         for(int i=0; i<N; ++i) {
             double a = lenA ? args[0]->d_vec[i%lenA] : 0;
             double b = lenB ? args[1]->d_vec[i%lenB] : 0;
             res->d_vec[i] = a - b;
         }
         return res;
    }
    
    RValuePtr Builtin_C(const std::vector<RValuePtr>& args, RValuePtr env) {
        // Combine. Flatten?
        // Simple double implementation
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(auto& arg : args) {
            if (arg->type == RType::DOUBLE) {
                res->d_vec.insert(res->d_vec.end(), arg->d_vec.begin(), arg->d_vec.end());
            } else if (arg->type == RType::INTEGER || arg->type == RType::LOGICAL) {
                for(int v : arg->i_vec) res->d_vec.push_back((double)v);
            }
        }
        return res;
    }

    void InitGlobalEnv(RValuePtr env) {
        // Register builtins
        auto add = std::make_shared<RValue>(RType::BUILTIN); add->builtin = Builtin_Add;
        Define("+", add, env);
        
        auto sub = std::make_shared<RValue>(RType::BUILTIN); sub->builtin = Builtin_Diff;
        Define("-", sub, env);
        
        auto c = std::make_shared<RValue>(RType::BUILTIN); c->builtin = Builtin_C;
        Define("c", c, env);
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
                         // Assignment: (<- sym val)
                         if (exp->l_vec.size() != 3) return RR_Error("Bad assignment");
                         RValuePtr sym = exp->l_vec[1];
                         if (sym->type != RType::SYMBOL) return RR_Error("LHS must be symbol");
                         RValuePtr val = Eval(exp->l_vec[2], env);
                         Define(sym->sym_name, val, env);
                         return val;
                    }
                    if (head->sym_name == "if") {
                        // (if cond then else)
                        RValuePtr cond = Eval(exp->l_vec[1], env);
                        if (IsTrue(cond)) return Eval(exp->l_vec[2], env);
                        if (exp->l_vec.size() > 3) return Eval(exp->l_vec[3], env);
                        return RR_Nil();
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
                for(size_t i=1; i<exp->l_vec.size(); ++i) {
                    RValuePtr a = Eval(exp->l_vec[i], env);
                    if (a->type == RType::ERROR) return a;
                    args.push_back(a);
                }
                
                if (func->type == RType::BUILTIN) {
                    return func->builtin(args, env);
                }
                
                // Closure support todo
                return RR_Error("Not a function");
            }
            default:
                return exp;
        }
    }
}
