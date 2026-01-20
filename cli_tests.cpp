#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "runtime/Evaluator.hpp"
#include "runtime/Lexer.hpp"
#include "runtime/Parser.hpp"

struct Case {
    const char* expr;
    const char* expected;
};

static std::string evalToString(const std::string& src, RValuePtr env) {
    Lexer lex(src);
    auto tokens = lex.Tokenize();
    Parser parser(tokens);
    RValuePtr ast = parser.Parse();
    if (parser.HasError()) return std::string("Error: ") + parser.GetError();
    RValuePtr res = Evaluator::Eval(ast, env);
    return Evaluator::ToString(res);
}

int main() {
    auto env = std::make_shared<RValue>(RType::ENV);
    Evaluator::InitGlobalEnv(env);

    // Subset of R 4.4 expectations for currently implemented features.
    // Note: output formatting is “R-ish”, not byte-for-byte identical to R.
    const std::vector<Case> cases = {
        {"!c()", "[1] "},                 // logical(0) equivalent prints empty after [1]
        {"!c(TRUE, FALSE)", "[1] FALSE TRUE"},
        {"c(TRUE, NA) & FALSE", "[1] FALSE FALSE"},
        {"c(TRUE, NA) & TRUE", "[1] TRUE NA"},
        {"c(FALSE, NA) | FALSE", "[1] FALSE NA"},
        {"c(FALSE, NA) | TRUE", "[1] TRUE TRUE"},
        {".5 + 1", "[1] 1.5"},
    };

    int failed = 0;
    for (const auto& tc : cases) {
        std::string out = evalToString(tc.expr, env);
        if (out != tc.expected) {
            failed++;
            std::cout << "FAIL: " << tc.expr << "\n";
            std::cout << "  expected: " << tc.expected << "\n";
            std::cout << "  got     : " << out << "\n";
        }
    }

    if (failed == 0) {
        std::cout << "All tests passed (" << cases.size() << " cases)\n";
        return 0;
    }
    std::cout << failed << " tests failed\n";
    return 1;
}

