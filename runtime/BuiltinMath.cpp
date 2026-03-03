#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    RValuePtr Builtin_Assign(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("assign() requires at least 2 arguments");
        MiniString name_str;
        if (args[0]->type == RType::CHARACTER && args[0]->Length() > 0) name_str = args[0]->s_vec[0];
        else if (args[0]->type == RType::SYMBOL) name_str = args[0]->sym_name;
        else return RR_Error("assign() first argument must be a character name");
        RValuePtr val = args[1];
        Define(name_str, val, env);
        return val;
    }
    
    RValuePtr Builtin_C(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
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
                        if (arg->i_vec[i] == R_INT_NA) res->s_vec.push_back("NA");
                        else {
                            MiniString tmp = MiniToString(arg->i_vec[i]);
                            res->s_vec.push_back(tmp);
                        }
                } else if (arg->type == RType::LOGICAL) {
                    for (int i = 0; i < arg->Length(); ++i) {
                        int v = arg->i_vec[i];
                        if (v == R_LOGICAL_NA) res->s_vec.push_back("NA");
                        else if (v) res->s_vec.push_back("TRUE");
                        else res->s_vec.push_back("FALSE");
                    }
                } else if (arg->type == RType::DOUBLE) {
                    for (int i = 0; i < arg->Length(); ++i) {
                        double d = arg->d_vec[i];
                        if (std::isnan(d)) res->s_vec.push_back("NA");
                        else if (d == std::floor(d) && d >= INT32_MIN && d <= INT32_MAX) {
                            MiniString tmp = MiniToString(static_cast<int>(d));
                            res->s_vec.push_back(tmp);
                        } else {
                            MiniString tmp = MiniToString(d);
                            res->s_vec.push_back(tmp);
                        }
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

    // Shared setup for binary arithmetic: check lengths, warn recycle, decide type.
    struct BinOpInfo { int N, lenA, lenB; bool use_double; };
    static BinOpInfo PrepareBinOp(RValuePtr a, RValuePtr b, const char* op_name) {
        int lenA = a->Length(), lenB = b->Length();
        int N = std::max(lenA, lenB);
        if (lenA > 0 && lenB > 0) WarnRecycle(op_name, lenA, lenB);
        return {N, lenA, lenB, AnyDouble(a, b)};
    }

    RValuePtr Builtin_Add(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("Need 1 or 2 args for +");
        if (args.size() == 1) return args[0]; // unary + is identity
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "+");
        if (lenA == 0 || lenB == 0)
            return use_double ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
        if (use_double) {
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            res->d_vec.resize(N);
            if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
            for (int i = 0; i < N; ++i)
                res->d_vec[i] = args[0]->GetDouble(i % lenA) + args[1]->GetDouble(i % lenB);
            return res;
        }
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i) {
            int a = args[0]->GetInt(i % lenA), b = args[1]->GetInt(i % lenB);
            res->i_vec[i] = (a == R_INT_NA || b == R_INT_NA) ? R_INT_NA : IntOpResultOrNA((int64_t)a + b);
        }
        return res;
    }

    RValuePtr Builtin_Diff(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.empty()) return RR_Error("Need 1 or 2 args for -");
        if (args.size() == 1) {
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
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "-");
        if (lenA == 0 || lenB == 0)
            return use_double ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
        if (use_double) {
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            res->d_vec.resize(N);
            if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
            for (int i = 0; i < N; ++i)
                res->d_vec[i] = args[0]->GetDouble(i % lenA) - args[1]->GetDouble(i % lenB);
            return res;
        }
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i) {
            int a = args[0]->GetInt(i % lenA), b = args[1]->GetInt(i % lenB);
            res->i_vec[i] = (a == R_INT_NA || b == R_INT_NA) ? R_INT_NA : IntOpResultOrNA((int64_t)a - b);
        }
        return res;
    }

    RValuePtr Builtin_Mul(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for *");
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "*");
        if (lenA == 0 || lenB == 0)
            return use_double ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
        if (use_double) {
            auto res = std::make_shared<RValue>(RType::DOUBLE);
            res->d_vec.resize(N);
            if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
            for (int i = 0; i < N; ++i)
                res->d_vec[i] = args[0]->GetDouble(i % lenA) * args[1]->GetDouble(i % lenB);
            return res;
        }
        auto res = std::make_shared<RValue>(RType::INTEGER);
        res->i_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i) {
            int a = args[0]->GetInt(i % lenA), b = args[1]->GetInt(i % lenB);
            res->i_vec[i] = (a == R_INT_NA || b == R_INT_NA) ? R_INT_NA : IntOpResultOrNA((int64_t)a * b);
        }
        return res;
    }

    RValuePtr Builtin_Div(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for /");
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "/");
        if (lenA == 0 || lenB == 0) return std::make_shared<RValue>(RType::DOUBLE);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i)
            res->d_vec[i] = args[0]->GetDouble(i % lenA) / args[1]->GetDouble(i % lenB);
        return res;
    }

    RValuePtr Builtin_Pow(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for ^");
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "^");
        if (lenA == 0 || lenB == 0) return std::make_shared<RValue>(RType::DOUBLE);
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i)
            res->d_vec[i] = std::pow(args[0]->GetDouble(i % lenA), args[1]->GetDouble(i % lenB));
        return res;
    }

    // Comparison ops: < > <= >= == !=  (elementwise, recycle, return LOGICAL; NA propagates)
    enum class CmpOp { LT, GT, LE, GE, EQ, NE };
    static RValuePtr Builtin_Compare(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env, CmpOp op) {
        if (args.size() < 2) return RR_Error("Need 2 args for comparison");
        RValuePtr a = args[0], b = args[1];
        int lenA = a->Length(), lenB = b->Length();
        if (lenA == 0 || lenB == 0) return std::make_shared<RValue>(RType::LOGICAL);
        WarnRecycle(op == CmpOp::LT ? "<" : op == CmpOp::GT ? ">" : op == CmpOp::LE ? "<=" : op == CmpOp::GE ? ">=" : op == CmpOp::EQ ? "==" : "!=", lenA, lenB);
        int N = std::max(lenA, lenB);
        auto res = std::make_shared<RValue>(RType::LOGICAL);
        // Preserve dim attribute from first argument (if matrix, result is also matrix)
        if (a->attributes.count("dim")) {
            res->attributes["dim"] = a->attributes["dim"];
        }
        bool both_char = (a->type == RType::CHARACTER && b->type == RType::CHARACTER);
        for (int i = 0; i < N; ++i) {
            int ia = i % lenA, ib = i % lenB;
            if (both_char) {
                const MiniString& sa = a->s_vec[ia];
                const MiniString& sb = b->s_vec[ib];
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
    RValuePtr Builtin_Lt(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::LT); }
    RValuePtr Builtin_Gt(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::GT); }
    RValuePtr Builtin_Le(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::LE); }
    RValuePtr Builtin_Ge(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::GE); }
    RValuePtr Builtin_Eq(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::EQ); }
    RValuePtr Builtin_Ne(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) { return Builtin_Compare(args, names, env, CmpOp::NE); }

    RValuePtr Builtin_Mod(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for %%");
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "%%");
        if (lenA == 0 || lenB == 0)
            return use_double ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
        if (!use_double && IsIntLike(args[0]) && IsIntLike(args[1])) {
            auto res = std::make_shared<RValue>(RType::INTEGER);
            res->i_vec.resize(N);
            if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
            for (int i = 0; i < N; ++i) {
                int a = args[0]->GetInt(i % lenA), b = args[1]->GetInt(i % lenB);
                res->i_vec[i] = (a == R_INT_NA || b == R_INT_NA || b == 0) ? R_INT_NA : a % b;
            }
            return res;
        }
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i)
            res->d_vec[i] = std::fmod(args[0]->GetDouble(i % lenA), args[1]->GetDouble(i % lenB));
        return res;
    }

    RValuePtr Builtin_IntDiv(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (args.size() < 2) return RR_Error("Need 2 args for %/%");
        auto [N, lenA, lenB, use_double] = PrepareBinOp(args[0], args[1], "%/%");
        if (lenA == 0 || lenB == 0)
            return use_double ? std::make_shared<RValue>(RType::DOUBLE) : std::make_shared<RValue>(RType::INTEGER);
        if (!use_double && IsIntLike(args[0]) && IsIntLike(args[1])) {
            auto res = std::make_shared<RValue>(RType::INTEGER);
            res->i_vec.resize(N);
            if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
            for (int i = 0; i < N; ++i) {
                int a = args[0]->GetInt(i % lenA), b = args[1]->GetInt(i % lenB);
                res->i_vec[i] = (a == R_INT_NA || b == R_INT_NA || b == 0) ? R_INT_NA : (int)std::floor((double)a / b);
            }
            return res;
        }
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        res->d_vec.resize(N);
        if (args[0]->attributes.count("dim")) res->attributes["dim"] = args[0]->attributes["dim"];
        for (int i = 0; i < N; ++i)
            res->d_vec[i] = std::floor(args[0]->GetDouble(i % lenA) / args[1]->GetDouble(i % lenB));
        return res;
    }
    
    // --- STANDARD MATH ---
    RValuePtr Builtin_Log(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("log() requires at least 1 argument");
        RValuePtr x = args[0];
        // base? default e
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) res->d_vec.push_back(std::log(x->GetDouble(i)));
        return res;
    }
    RValuePtr Builtin_Exp(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("exp() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) res->d_vec.push_back(std::exp(x->GetDouble(i)));
        return res;
    }
    RValuePtr Builtin_Sqrt(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("sqrt() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) {
            double v = x->GetDouble(i);
            // R: sqrt(negative) -> NaN with warning (we don't implement warnings yet)
            res->d_vec.push_back(v < 0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(v));
        }
        return res;
    }
    RValuePtr Builtin_Abs(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("abs() requires at least 1 argument");
        RValuePtr x = args[0];
        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<x->Length(); ++i) res->d_vec.push_back(std::abs(x->GetDouble(i)));
        return res;
    }
    RValuePtr Builtin_Round(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if(args.empty()) return RR_Error("round() requires at least 1 argument");
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

} // namespace Evaluator
