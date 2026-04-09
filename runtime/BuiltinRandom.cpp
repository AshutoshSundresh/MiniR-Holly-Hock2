#include "EvaluatorImpl.hpp"
#include <map>
#include <set>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace Evaluator {

    // Tiny custom PRNG suitable for embedded / SH4 targets.
    // Xorshift32 with a non-zero 32-bit state, plus helpers for uniform and normal draws.
    static uint32_t g_rng_state = 1234u;
    static bool g_rng_has_spare = false;
    static double g_rng_spare = 0.0;

    static void TinyRngSeed(uint32_t seed) {
        if (seed == 0) seed = 1u; // avoid zero state
        g_rng_state = seed;
        g_rng_has_spare = false;
    }

    static uint32_t TinyRngNextU32() {
        // xorshift32
        uint32_t x = g_rng_state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        g_rng_state = x;
        return x;
    }

    static double TinyRngUniform01() {
        // 53-bit resolution uniform in [0,1): 27 high bits + 26 low bits
        uint64_t a = TinyRngNextU32() >> 5;
        uint64_t b = TinyRngNextU32() >> 6;
        uint64_t v = (a << 26) | b;
        return (double)v / (double)(1ULL << 53);
    }

    static int TinyRngInt(int max_exclusive) {
        if (max_exclusive <= 0) return 0;
        // Simple modulo reduction; good enough for this MiniR use.
        uint32_t r = TinyRngNextU32();
        return (int)(r % (uint32_t)max_exclusive);
    }

    static double TinyRngNormal() {
        // Box-Muller, cached pair
        if (g_rng_has_spare) {
            g_rng_has_spare = false;
            return g_rng_spare;
        }
        double u1 = 0.0;
        do {
            u1 = TinyRngUniform01();
        } while (u1 <= 0.0);
        double u2 = TinyRngUniform01();
        double mag = std::sqrt(-2.0 * std::log(u1));
        double z0 = mag * std::cos(2.0 * 3.14159265358979323846 * u2);
        double z1 = mag * std::sin(2.0 * 3.14159265358979323846 * u2);
        g_rng_spare = z1;
        g_rng_has_spare = true;
        return z0;
    }
    
    RValuePtr Builtin_SetSeed(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        if (!args.empty()) {
            // Scramble the seed so small seeds (1, 2, 42...) don't start in a low-entropy state
            uint32_t s = (uint32_t)args[0]->GetInt(0) * 2654435761u;
            TinyRngSeed(s ^ (s >> 16));
            for (int i = 0; i < 8; ++i) TinyRngNextU32();
        }
        return RR_Nil();
    }
    
    RValuePtr Builtin_Runif(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        double min = 0, max = 1;
        if (args.size() > 1) min = args[1]->GetDouble(0);
        if (args.size() > 2) max = args[2]->GetDouble(0);

        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<n; ++i) {
            double u = TinyRngUniform01(); // [0,1)
            res->d_vec.push_back(min + (max - min) * u);
        }
        return res;
    }
    
    RValuePtr Builtin_Rnorm(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        int n = (!args.empty()) ? args[0]->GetInt(0) : 0;
        double mean = 0, sd = 1;
        if (args.size() > 1) mean = args[1]->GetDouble(0);
        if (args.size() > 2) sd = args[2]->GetDouble(0);

        auto res = std::make_shared<RValue>(RType::DOUBLE);
        for(int i=0; i<n; ++i) {
            double z = TinyRngNormal(); // N(0,1)
            res->d_vec.push_back(mean + sd * z);
        }
        return res;
    }
    
    RValuePtr Builtin_Sample(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env) {
        // sample(x, size, replace = FALSE)
        if (args.empty()) return RR_Error("sample() requires at least 1 argument");
        RValuePtr x = args[0];

        // If x is a positive scalar integer/double, treat as sample from 1:x
        bool x_is_range = (x->Length() == 1 && x->GetDouble(0) >= 1.0);
        int N = x_is_range ? (int)x->GetDouble(0) : x->Length();
        if (N < 0) return RR_Error("sample: invalid first argument");

        RValuePtr size_arg = GetArg(args, names, "size", 1, nullptr);
        int size = size_arg ? size_arg->GetInt(0) : N;
        if (size < 0) return RR_Error("sample: 'size' must be non-negative");

        RValuePtr rep_arg = GetArg(args, names, "replace", 2, nullptr);
        bool replace = rep_arg ? IsTrue(rep_arg) : false;

        if (!replace && size > N)
            return RR_Error("Cannot take a sample larger than the population when 'replace = FALSE'");

        // Build 0-based index pool
        MiniVector<int> indices;
        for (int i = 0; i < N; ++i) indices.push_back(i);

        auto res = std::make_shared<RValue>(x_is_range ? RType::INTEGER : x->type);

        if (replace) {
            for (int i = 0; i < size; ++i) {
                int idx = TinyRngInt(N);
                if (x_is_range) res->i_vec.push_back(idx + 1); // 1-based
                else if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
                else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[idx]);
                else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[idx]);
            }
        } else {
            // Fisher-Yates partial shuffle
            for (int i = N - 1; i > 0 && (N - i) <= size; --i) {
                int j = TinyRngInt(i + 1);
                int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
            }
            for (int i = 0; i < size; ++i) {
                int idx = indices[N - 1 - i];
                if (x_is_range) res->i_vec.push_back(idx + 1); // 1-based
                else if (x->type == RType::DOUBLE) res->d_vec.push_back(x->d_vec[idx]);
                else if (x->type == RType::INTEGER) res->i_vec.push_back(x->i_vec[idx]);
                else if (x->type == RType::CHARACTER) res->s_vec.push_back(x->s_vec[idx]);
            }
        }
        return res;
    }
    
    // --- LOGIC & SETS ---
    // ifelse(test, yes, no)

} // namespace Evaluator
