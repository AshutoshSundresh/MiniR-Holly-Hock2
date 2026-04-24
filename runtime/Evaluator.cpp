#include "Evaluator.hpp"
#include "EvaluatorImpl.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <set>
#include <map>
#include <cstdint>
#include <cstring>
#include <cctype>
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

    // Helper: Does object have a given S3 class?
    bool HasClass(RValuePtr v, const MiniString& cls) {
        if (!v) return false;
        auto it = v->attributes.find("class");
        if (it == v->attributes.end()) return false;
        RValuePtr c = it->second;
        if (!c || c->type != RType::CHARACTER) return false;
        for (const auto& s : c->s_vec) {
            if (s == cls) return true;
        }
        return false;
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
        if (v->type == RType::CHARACTER) {
            if (i < 0 || i >= (int)v->s_vec.size()) return 0;
            if (IsNAString(v->s_vec[i])) return R_LOGICAL_NA;
            MiniString s = v->s_vec[i];
            for (size_t idx= 0; idx < s.size(); ++idx) {
                char ch = s[idx];
                s[idx] = (char)std::toupper((unsigned char)ch);
            }
            if (s == "TRUE" || s == "T") return 1;
            if (s == "FALSE" || s == "F") return 0;
            if (s == "NA") return R_LOGICAL_NA;
            // R: unrecognized strings -> NA
            return R_LOGICAL_NA;
        }
        // For this MiniR subset, treat other types as FALSE.
        return 0;
    }

    MiniString AsStringAt(RValuePtr v, int i) {
        if (!v || i < 0 || i >= v->Length()) return R_STRING_NA;
        switch (v->type) {
            case RType::CHARACTER: return v->s_vec[i];
            case RType::DOUBLE: {
                double d = v->d_vec[i];
                if (IsNAReal(d)) return R_STRING_NA;
                if (std::isnan(d)) return "NaN";
                if (std::isinf(d)) return d > 0 ? "Inf" : "-Inf";
                return MiniToString(d);
            }
            case RType::INTEGER:
                return v->i_vec[i] == R_INT_NA ? MiniString(R_STRING_NA) : MiniToString(v->i_vec[i]);
            case RType::LOGICAL:
                if (v->i_vec[i] == R_LOGICAL_NA) return R_STRING_NA;
                return v->i_vec[i] ? "TRUE" : "FALSE";
            default:
                return R_STRING_NA;
        }
    }

    bool ConditionToBoolOrError(RValuePtr cond, MiniString& err) {
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
            std::fprintf(stderr,
                         "Warning: longer object length is not a multiple of shorter object length in '%s'\n",
                         op);
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

    RValuePtr Lookup(const MiniString& name, RValuePtr env) {
        // Simple search up parent chain
        RValuePtr cur = env;
        while (cur) {
            auto it = cur->frame.find(name);
            if (it != cur->frame.end()) return it->second;
            cur = cur->parent_env;
        }
        MiniString msg("Object '");
        msg += name;
        msg += "' not found";
        return RR_Error(msg);
    }

    void Define(const MiniString& name, RValuePtr val, RValuePtr env) {
        if (env) env->frame[name] = val;
    }

    // --- ARGUMENT MATCHING ---
    // Returns the value if found, or default_val.
    // Logic: Look for exact name match in arg_names.
    // If not found, look for positional (index pos) IF that position hasn't been named in the call.
    // (Simplified: if arg_names[pos] is empty, it's a candidate for positional)
    RValuePtr GetArg(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& call_names,
                     const MiniString& target_name, int target_pos, RValuePtr default_val) {
                         
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

    // Call a closure: match args to formals R-style (exact names first, then the remaining
    // positional args fill unmatched formals left to right), bind them in a fresh frame whose
    // parent is the closure's defining environment, fill in defaults, and evaluate the body.
    RValuePtr CallClosure(RValuePtr func, const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& arg_names) {
        RValuePtr formals = func->formals;
        size_t nf = formals->l_vec.size();
        MiniVector<RValuePtr> bound(nf);
        MiniVector<int> used(args.size());

        for (size_t a = 0; a < args.size(); ++a) {
            if (a >= arg_names.size() || arg_names[a].empty()) continue;
            size_t f = 0;
            while (f < nf && formals->l_vec[f]->sym_name != arg_names[a]) ++f;
            if (f == nf) return RR_Error("unused argument (" + arg_names[a] + " = ...)");
            if (bound[f]) return RR_Error("formal argument \"" + arg_names[a] + "\" matched by multiple actual arguments");
            bound[f] = args[a];
            used[a] = 1;
        }
        size_t f = 0;
        for (size_t a = 0; a < args.size(); ++a) {
            if (used[a]) continue;
            while (f < nf && bound[f]) ++f;
            if (f == nf) return RR_Error("unused argument");
            bound[f] = args[a];
        }

        auto new_env = std::make_shared<RValue>(RType::ENV);
        new_env->parent_env = func->env;

        // Supplied args first so defaults can refer to them, e.g. function(n, m = n * 2)
        for (size_t i = 0; i < nf; ++i) {
            if (bound[i]) Define(formals->l_vec[i]->sym_name, bound[i], new_env);
        }
        RValuePtr defaults_list = func->attributes.count("defaults") ? func->attributes["defaults"] : nullptr;
        for (size_t i = 0; i < nf; ++i) {
            if (bound[i]) continue;
            RValuePtr val = RR_Nil();
            if (defaults_list && i < defaults_list->l_vec.size()) {
                RValuePtr def_expr = defaults_list->l_vec[i];
                if (def_expr && def_expr->type != RType::NIL) {
                    val = Eval(def_expr, new_env);
                    if (val->type == RType::ERROR) return val;
                }
            }
            Define(formals->l_vec[i]->sym_name, val, new_env);
        }

        return Eval(func->body, new_env);
    }

    // Frame that `name <<- value` writes to: the nearest enclosing frame (above env)
    // that already defines name, otherwise the global environment.
    static RValuePtr SuperAssignEnv(const MiniString& name, RValuePtr env) {
        RValuePtr last = env;
        for (RValuePtr cur = env ? env->parent_env : nullptr; cur; cur = cur->parent_env) {
            if (cur->frame.count(name)) return cur;
            last = cur;
        }
        return last;
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
    static RValuePtr SubAssign(RValuePtr x, const MiniString& op, const MiniVector<RValuePtr>& index_vals, RValuePtr value, MiniString& err) {
        err.clear();
        if (!x) { err = "object not found"; return nullptr; }
        RValuePtr res = CloneForAssign(x);
        if (op == "$") {
            if (x->type != RType::LIST || index_vals.empty()) { err = "invalid $ assignment"; return nullptr; }
            MiniString key = index_vals[0]->type == RType::CHARACTER ? index_vals[0]->s_vec[0] : index_vals[0]->sym_name;
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
            else if (res->type == RType::CHARACTER) res->s_vec[i] = AsStringAt(value, 0);
            return res;
        }
        if (op == "[") {
            if (index_vals.empty()) { err = "need index for [ assign"; return nullptr; }
            if (res->attributes.count("dim") && index_vals.size() >= 2) {
                int nr = res->attributes["dim"]->GetInt(0);
                int nc = res->attributes["dim"]->GetInt(1);
                int r_raw = index_vals[0]->GetInt(0);
                int c_raw = index_vals[1]->GetInt(0);
                if (r_raw >= 1 && r_raw <= nr && c_raw >= 1 && c_raw <= nc) {
                    int r = r_raw - 1;
                    int c = c_raw - 1;
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
                else if (res->type == RType::CHARACTER) res->s_vec[i] = AsStringAt(value, k % vlen);
            }
            return res;
        }
        err = "invalid subassignment op"; return nullptr;
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
        REG("duplicated", Builtin_Duplicated);
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
                    if (head->sym_name == "<-" || head->sym_name == "=" || head->sym_name == "<<-") {
                         if (exp->l_vec.size() != 3) return RR_Error("Bad assignment");
                         bool super_assign = (head->sym_name == "<<-");
                         RValuePtr lhs = exp->l_vec[1];
                         RValuePtr val = Eval(exp->l_vec[2], env);
                        if (val->type == RType::ERROR) return val;
                        MiniString target_name;
                        RValuePtr x;
                        MiniString sub_op;
                        MiniVector<RValuePtr> index_vals;
                         if (lhs->type == RType::SYMBOL) {
                             target_name = lhs->sym_name;
                             Define(target_name, val, super_assign ? SuperAssignEnv(target_name, env) : env);
                             return val;
                         }
                         if (lhs->type == RType::LIST && !lhs->l_vec.empty() && lhs->l_vec[0]->type == RType::SYMBOL) {
                             sub_op = lhs->l_vec[0]->sym_name;
                             if (sub_op == "[" || sub_op == "[[" || sub_op == "$") {
                                 if (lhs->l_vec.size() < 2) return RR_Error("Bad subassignment");
                                 RValuePtr target = lhs->l_vec[1];
                                 if (target->type != RType::SYMBOL) return RR_Error("invalid assignment target");
                                 target_name = target->sym_name;
                                 RValuePtr target_env = super_assign ? SuperAssignEnv(target_name, env) : env;
                                 x = Lookup(target_name, target_env);
                                 if (x->type == RType::ERROR) return x;
                                 for (size_t i = 2; i < lhs->l_vec.size(); ++i) {
                                     if (sub_op == "$" && i == 2 && lhs->l_vec[i]->type == RType::SYMBOL)
                                         index_vals.push_back(lhs->l_vec[i]); // name: use symbol, not evaluated
                                     else {
                                         RValuePtr idx = Eval(lhs->l_vec[i], env);
                                         if (idx->type == RType::ERROR) return idx;
                                         index_vals.push_back(idx);
                                     }
                                 }
                                 MiniString err;
                                 RValuePtr modified = SubAssign(x, sub_op, index_vals, val, err);
                                 if (!err.empty()) return RR_Error(err);
                                 if (!modified) return RR_Error("subassignment failed");
                                 Define(target_name, modified, target_env);
                                 return val;
                             }
                         }
                         return RR_Error("LHS must be a symbol or subset expression (e.g. x[i], x$name)");
                    }
                    if (head->sym_name == "if") {
                        // (if cond then else)
                        RValuePtr cond = Eval(exp->l_vec[1], env);
                        if (cond->type == RType::ERROR) return cond;
                        MiniString cerr;
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
                            if (cond->type == RType::ERROR) return cond;
                            MiniString cerr;
                            bool c = ConditionToBoolOrError(cond, cerr);
                            if (!cerr.empty()) return RR_Error(cerr);
                            if (!c) break;
                            last = Eval(body, env);
                            if (last->type == RType::ERROR) return last;
                        }
                        return last;
                    }
                    if (head->sym_name == "for") {
                        RValuePtr seq_expr = exp->l_vec[2];
                        RValuePtr body = exp->l_vec[3];
                        RValuePtr seq = Eval(seq_expr, env);
                        if (seq->type == RType::ERROR) return seq;
                        MiniString var_name = exp->l_vec[1]->sym_name;
                        RValuePtr last = RR_Nil();
                        int n = seq->Length();
                        for(int i=0; i<n; ++i) {
                            RValuePtr val;
                            if (seq->type == RType::LIST) {
                                val = seq->l_vec[i];
                            } else {
                                val = std::make_shared<RValue>(seq->type);
                                if (seq->type == RType::INTEGER || seq->type == RType::LOGICAL) val->i_vec.push_back(seq->i_vec[i]);
                                else if (seq->type == RType::DOUBLE) val->d_vec.push_back(seq->d_vec[i]);
                                else if (seq->type == RType::CHARACTER) val->s_vec.push_back(seq->s_vec[i]);
                            }
                            Define(var_name, val, env);
                            last = Eval(body, env);
                            if (last->type == RType::ERROR) return last;
                        }
                        return last;
                    }
                    if (head->sym_name == "{") {
                        RValuePtr res = RR_Nil();
                        for(size_t i=1; i<exp->l_vec.size(); ++i) {
                            res = Eval(exp->l_vec[i], env);
                            if (res->type == RType::ERROR) return res; // an error aborts the block
                        }
                        return res;
                    }
                }
                
                // Function Call
                RValuePtr func = Eval(head, env);
                if (func->type == RType::ERROR) return func;
                
                // Eval Args
                MiniVector<RValuePtr> args;
                MiniVector<MiniString> arg_names;
                
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
                    return CallClosure(func, args, arg_names);
                }

                return RR_Error("Not a function");
            }
            case RType::CLOSURE: {
                // A parsed `function(...)` expression evaluates to a new closure that
                // captures the environment it was created in (lexical scoping).
                auto clo = std::make_shared<RValue>(RType::CLOSURE);
                clo->formals = exp->formals;
                clo->body = exp->body;
                clo->attributes = exp->attributes;
                clo->env = env;
                return clo;
            }
            default:
                return exp;
        }
    }


    // --- TO STRING ---
    MiniString ToString(RValuePtr v) {
        if (!v) return "NULL";
        if (v->type == RType::NIL) return "NULL";
        if (v->type == RType::ERROR) return "Error: " + v->sym_name;
        
        auto fmtDouble = [](double d) -> MiniString {
            if (IsNAReal(d)) return "NA";
            if (std::isnan(d)) return "NaN";
            if (std::isinf(d)) return d > 0 ? "Inf" : "-Inf";
            return MiniToString(d);
        };
        auto fmtInt = [](int x) -> MiniString {
            if (x == R_INT_NA) return "NA";
            return MiniToString(x);
        };
        auto fmtLgl = [](int x) -> MiniString {
            if (x == R_LOGICAL_NA) return "NA";
            return x ? "TRUE" : "FALSE";
        };
        auto fmtStr = [](const MiniString& x) -> MiniString {
            if (IsNAString(x)) return "NA";
            return "\"" + x + "\"";
        };

        MiniString s = "";
        
        // data.frame: pretty print like a simple 2D table
        if (v->type == RType::LIST && HasClass(v, "data.frame")) {
            int ncol = (int)v->l_vec.size();
            int nrow = ncol > 0 ? v->l_vec[0]->Length() : 0;
            RValuePtr names_attr = v->attributes.count("names") ? v->attributes["names"] : RR_Nil();

            // Header
            s += "  ";
            for (int j = 0; j < ncol; ++j) {
                if (j) s += " ";
                if (names_attr && names_attr->type == RType::CHARACTER && j < names_attr->Length())
                    s += names_attr->s_vec[j];
                else
                    s += "V";
                    s += MiniToString(j+1);
            }
            s += "\n";

            // Rows
            for (int i = 0; i < nrow; ++i) {
                s += MiniToString(i+1);
                s += " ";
                for (int j = 0; j < ncol; ++j) {
                    if (j) s += " ";
                    RValuePtr col = v->l_vec[j];
                    if (col->type == RType::DOUBLE) s += fmtDouble(col->GetDouble(i));
                    else if (col->type == RType::INTEGER) s += fmtInt(col->GetInt(i));
                    else if (col->type == RType::LOGICAL) s += fmtLgl(col->GetInt(i));
                    else if (col->type == RType::CHARACTER && i < col->Length()) s += fmtStr(col->s_vec[i]);
                    else s += "NA";
                }
                s += "\n";
            }
            return s;
        }
        
        // Matrix: print as rows x cols when dim is 2D
        if ((v->type == RType::DOUBLE || v->type == RType::INTEGER || v->type == RType::LOGICAL || v->type == RType::CHARACTER)
            && v->attributes.count("dim")) {
            RValuePtr dim = v->attributes["dim"];
            if (dim->Length() >= 2) {
                int nr = dim->GetInt(0), nc = dim->GetInt(1);
                if (nr > 0 && nc > 0 && nr * nc == v->Length()) {
                    for (int r = 0; r < nr; ++r) {
                        s += "[";
                        s += MiniToString(r+1);
                        s += ",] ";
                        for (int c = 0; c < nc; ++c) {
                            int flat = c * nr + r;
                            if (c > 0) s += " ";
                            if (v->type == RType::DOUBLE) s += fmtDouble(v->d_vec[flat]);
                            else if (v->type == RType::INTEGER) s += fmtInt(v->i_vec[flat]);
                            else if (v->type == RType::LOGICAL) s += fmtLgl(v->i_vec[flat]);
                            else if (v->type == RType::CHARACTER) s += fmtStr(v->s_vec[flat]);
                        }
                        s += "\n";
                    }
                    // Header row: [,1] [,2] ...
                    MiniString header = "     ";
                    for (int c = 0; c < nc; ++c) { if (c) header += " "; header += "[,"; header += MiniToString(c+1); header += "]"; }
                    s = header + "\n" + s;
                    return s;
                }
            }
        }
        
        // Empty vectors print their type, as in R
        if (v->Length() == 0) {
            if (v->type == RType::DOUBLE) return "numeric(0)";
            if (v->type == RType::INTEGER) return HasClass(v, "factor") ? "factor(0)" : "integer(0)";
            if (v->type == RType::LOGICAL) return "logical(0)";
            if (v->type == RType::CHARACTER) return "character(0)";
            if (v->type == RType::LIST) return "list()";
        }

        // Vector: prefix [1] and space-separated
        if (v->type == RType::DOUBLE || v->type == RType::INTEGER || v->type == RType::LOGICAL || v->type == RType::CHARACTER) {
             s += "[1] ";
             for(int i=0; i<v->Length(); ++i) {
                 if (i > 0) s += " ";
                 if (v->type == RType::DOUBLE) s += fmtDouble(v->d_vec[i]);
                 else if (v->type == RType::INTEGER) s += fmtInt(v->i_vec[i]);
                 else if (v->type == RType::LOGICAL) s += fmtLgl(v->i_vec[i]);
                 else if (v->type == RType::CHARACTER) s += fmtStr(v->s_vec[i]);
             }
             return s;
        }
        
        if (v->type == RType::LIST) {
            for(int i=0; i<v->Length(); ++i) {
                s += "[[";
                s += MiniToString(i+1);
                s += "]]\n";
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
