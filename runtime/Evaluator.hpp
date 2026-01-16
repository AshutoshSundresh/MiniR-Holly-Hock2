#pragma once
#include "RValue.hpp"
#include <vector>
#include <string>

namespace Evaluator {
    using Environment = RValue; // Alias
    
    RValuePtr Eval(RValuePtr exp, RValuePtr env);
    void InitGlobalEnv(RValuePtr env);
    
    // Helpers
    RValuePtr Lookup(const std::string& name, RValuePtr env);
    void Define(const std::string& name, RValuePtr val, RValuePtr env);
    std::string ToString(RValuePtr v); // Pretty print
}
