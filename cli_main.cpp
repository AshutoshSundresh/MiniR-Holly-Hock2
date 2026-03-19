#include <iostream>

#include "runtime/Evaluator.hpp"
#include "runtime/Parser.hpp"
#include "runtime/Lexer.hpp"

// We need to implement ToString if it wasn't implemented or exposed correctly.
// Assuming it is exposed in Evaluator.hpp as verified.

namespace {
    int BalanceDelims(const MiniString& s) {
        int paren = 0, brace = 0, bracket = 0;
        bool in_single = false, in_double = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (!in_double && c == '\'') { in_single = !in_single; continue; }
            if (!in_single && c == '"') { in_double = !in_double; continue; }
            if (in_single || in_double) continue;
            switch (c) {
                case '(': paren++; break;
                case ')': paren--; break;
                case '{': brace++; break;
                case '}': brace--; break;
                case '[': bracket++; break;
                case ']': bracket--; break;
                default: break;
            }
        }
        return (paren > 0) + (brace > 0) + (bracket > 0);
    }
}

int main() {
    std::cout << "MiniR CLI (v0.1.0)" << std::endl;
    std::cout << "Type 'exit' or 'quit' to leave." << std::endl;

    // Initialize Environment
    auto env = std::make_shared<RValue>(RType::ENV);
    Evaluator::InitGlobalEnv(env);

    MiniString acc;

    while (true) {
        std::cout << (acc.empty() ? "> " : "+ ");
        MiniString line;
        while (true) {
            int ch = std::cin.get();
            if (!std::cin) break;
            if (ch == '\n') break;
            line.push_back(static_cast<char>(ch));
        }
        if (!std::cin && line.empty()) break;
        if (line == "exit" || line == "quit" || line == "q()") break;
        if (line.empty() && acc.empty()) continue;

        try {
            acc += line;
            acc += "\n";

            if (BalanceDelims(acc) > 0) {
                continue; // keep reading lines
            }

            // Lex
            Lexer lexer(acc);
            auto tokens = lexer.Tokenize();
            
            // Parse every statement on the line(s): `x <- 1; y <- 2` or a multi-line block
            Parser parser(tokens);
            MiniVector<RValuePtr> exprs = parser.ParseProgram();

            if (parser.IsIncomplete()) {
                continue; // e.g. `x <- 1 +` -- keep reading lines
            }
            if (parser.HasError()) {
                std::cout << "Error: " << parser.GetError().c_str() << std::endl;
                acc.clear();
                continue;
            }

            // Evaluate and print each statement; stop at the first error
            for (auto& ast : exprs) {
                RValuePtr result = Evaluator::Eval(ast, env);
                if (!result) continue;
                if (result->type == RType::ERROR) {
                    std::cout << "Error: " << result->sym_name.c_str() << std::endl;
                    break;
                }
                MiniString s = Evaluator::ToString(result);
                if (!s.empty()) std::cout << s.c_str() << std::endl;
            }
            acc.clear();
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << std::endl;
            acc.clear();
        }
    }
    return 0;
}
