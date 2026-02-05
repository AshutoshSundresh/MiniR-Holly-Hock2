#pragma once
#include "RValue.hpp"
#include "Containers.hpp"

namespace Evaluator {
    using Environment = RValue; // Alias
    
    RValuePtr Eval(RValuePtr exp, RValuePtr env);
    void InitGlobalEnv(RValuePtr env);
    
    // Helpers
    RValuePtr Lookup(const MiniString& name, RValuePtr env);
    void Define(const MiniString& name, RValuePtr val, RValuePtr env);
    MiniString ToString(RValuePtr v); // Pretty print
}
