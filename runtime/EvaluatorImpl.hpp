#pragma once
// Internal shared header for Evaluator split files.
// Provides forward declarations for all helpers used across translation units.
#include "Evaluator.hpp"
#include "RValue.hpp"
#include "Containers.hpp"
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

    // --- Forward declarations for helpers defined in Evaluator.cpp ---
    bool IsTrue(RValuePtr v);
    bool HasClass(RValuePtr v, const MiniString& cls);
    int AsLogicalAt(RValuePtr v, int i);
    MiniString AsStringAt(RValuePtr v, int i); // as.character() of one element; NA -> R_STRING_NA
    bool ConditionToBoolOrError(RValuePtr cond, MiniString& err);
    void WarnRecycle(const char* op, int lenA, int lenB);
    bool AnyDouble(RValuePtr a, RValuePtr b);
    bool IsIntLike(RValuePtr v);
    int32_t IntOpResultOrNA(int64_t x);
    RValuePtr Lookup(const MiniString& name, RValuePtr env);
    void Define(const MiniString& name, RValuePtr val, RValuePtr env);
    RValuePtr GetArg(const MiniVector<RValuePtr>& args,
                     const MiniVector<MiniString>& call_names,
                     const MiniString& target_name,
                     int target_pos,
                     RValuePtr default_val = nullptr);

    // --- ApplyFunction / Eval forward declarations ---
    RValuePtr ApplyFunction(RValuePtr func, const MiniVector<RValuePtr>& args, RValuePtr env);
    RValuePtr CallClosure(RValuePtr func, const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& arg_names);
    RValuePtr Eval(RValuePtr exp, RValuePtr env);

    // --- Builtin function forward declarations (defined in Builtin*.cpp) ---
    RValuePtr Builtin_Add(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Diff(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Mul(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Div(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Pow(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Mod(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IntDiv(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_MatMult(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Lt(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Gt(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Le(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Ge(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Eq(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Ne(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Log(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Exp(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sqrt(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Abs(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Round(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_And(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Or(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Not(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_C(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Colon(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Seq(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Rep(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sequence(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Matrix(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_AsMatrix(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Dim(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_NRow(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_NCol(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Transpose(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Row(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Col(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Length(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Assign(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Names(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Class(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Levels(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Rev(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sd(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Var(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_SetSeed(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Runif(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Rnorm(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sample(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Lapply(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sapply(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Nchar(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Substr(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Match(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_In(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IfElse(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Unique(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Duplicated(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Cumsum(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsNumeric(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsCharacter(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsLogical(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsList(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsNull(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Numeric(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Character(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Logical(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Paste(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Paste0(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Diag(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Table(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Head(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Tail(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_List(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Factor(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_DataFrame(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sum(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Max(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Min(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Any(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_All(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Mean(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Subset(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Subset2(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_AsLogical(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_AsCharacter(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsMatrix(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsVector(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_IsNA(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Sort(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Order(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Rank(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_MaxCol(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Cbind(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Rbind(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Which(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_WhichMin(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_WhichMax(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_PMax(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_PMin(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_RowSums(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_ColSums(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_RowMeans(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_ColMeans(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);
    RValuePtr Builtin_Dollar(const MiniVector<RValuePtr>& args, const MiniVector<MiniString>& names, RValuePtr env);

} // namespace Evaluator
