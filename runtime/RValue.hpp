#pragma once
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <cmath>
#include <limits>

enum class RType {
    NIL,
    LOGICAL,
    INTEGER,
    DOUBLE,
    CHARACTER,
    LIST,
    SYMBOL,
    CLOSURE,
    BUILTIN,
    ENV,
    ERROR // Special type for error propagation
};

struct RValue;
using RValuePtr = std::shared_ptr<RValue>;

// Forward decl for Builtin function pointer
using BuiltinFunc = RValuePtr (*)(const std::vector<RValuePtr>& args, const std::vector<std::string>& arg_names, RValuePtr env);

struct RValue {
    RType type;
    
    // Data storage (using vectors for typical R vectorized types)
    std::vector<int> i_vec; // Stores LOGICAL (0=FALSE, 1=TRUE, -1=NA) and INTEGER
    std::vector<double> d_vec;
    std::vector<std::string> s_vec;
    std::vector<RValuePtr> l_vec; // For generic lists
    
    // Attributes (names, class, dim, etc.)
    std::map<std::string, RValuePtr> attributes;
    
    // Specific fields
    std::string sym_name; // For SYMBOL or ERROR message
    
    // Closure fields
    RValuePtr formals;
    RValuePtr body;
    RValuePtr env; // Pointer to defining environment
    
    // Builtin
    BuiltinFunc builtin = nullptr;
    
    // Environment specific
    std::map<std::string, RValuePtr> frame;
    RValuePtr parent_env;

    // Generic helpers
    int Length() const {
        if (type == RType::DOUBLE) return d_vec.size();
        if (type == RType::INTEGER || type == RType::LOGICAL) return i_vec.size();
        if (type == RType::CHARACTER) return s_vec.size();
        if (type == RType::LIST) return l_vec.size();
        return 0; // NULL or single?
    }
    
    double GetDouble(int i) const {
        if (i < 0 || i >= Length()) return std::numeric_limits<double>::quiet_NaN();
        if (type == RType::DOUBLE) return d_vec[i];
        if (type == RType::INTEGER || type == RType::LOGICAL) {
            int v = i_vec[i];
            if (v == R_INT_NA || v == R_LOGICAL_NA) return std::numeric_limits<double>::quiet_NaN();
            return (double)v;
        }
        return 0.0;
    }

    int GetInt(int i) const {
         if (i < 0 || i >= Length()) return R_INT_NA;
         if (type == RType::INTEGER || type == RType::LOGICAL) return i_vec[i];
         if (type == RType::DOUBLE) {
             double d = d_vec[i];
             if (std::isnan(d)) return R_INT_NA;
             return (int)d;
         }
         return 0;
    }
    
    RValue(RType t) : type(t) {}
};

// Helpers for creation
inline RValuePtr RR_Nil() { return std::make_shared<RValue>(RType::NIL); }
inline RValuePtr RR_Error(const std::string& msg) { 
    auto r = std::make_shared<RValue>(RType::ERROR); 
    r->sym_name = msg; 
    return r; 
}

// NA constants
const int R_INT_NA = -2147483648; // Standard R-ish NA? Or just use min int.
const int R_LOGICAL_NA = -1;
