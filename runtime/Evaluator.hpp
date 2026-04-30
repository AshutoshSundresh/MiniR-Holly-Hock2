#pragma once
#include "RValue.hpp"
#include "Containers.hpp"

namespace Evaluator {
    RValuePtr Eval(RValuePtr exp, RValuePtr env);
    void InitGlobalEnv(RValuePtr env);
    
    // Helpers
    RValuePtr Lookup(const MiniString& name, RValuePtr env);
    void Define(const MiniString& name, RValuePtr val, RValuePtr env);
    MiniString ToString(RValuePtr v); // Pretty print

    // Whether the value from the last Eval should be auto-printed at the prompt.
    // Assignments, loops and functions like set.seed() return invisibly, as in R.
    extern bool R_Visible;
}
