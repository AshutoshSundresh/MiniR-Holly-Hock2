#include <iostream>
#include <cstdio>
#include <cstring>

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

    bool ReadFile(const char* path, MiniString& out) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
            for (size_t i = 0; i < n; ++i) {
                if (buf[i] != '\r') out.push_back(buf[i]);
            }
        }
        std::fclose(f);
        return true;
    }

    MiniVector<MiniString> SplitLines(const MiniString& s) {
        MiniVector<MiniString> lines;
        MiniString cur;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\n') { lines.push_back(cur); cur.clear(); }
            else cur.push_back(s[i]);
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

    bool IsBlank(const MiniString& s) {
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != ' ' && s[i] != '\t') return false;
        }
        return true;
    }

    // Run a script like Rscript: print visible results and stop at the first error.
    // With echo, each statement is shown before its output with "> " / "+ " prompts and
    // comments are kept, producing an R-style transcript.
    int RunScript(const char* path, bool echo) {
        MiniString src;
        if (!ReadFile(path, src)) {
            std::cout << "Error: cannot open file '" << path << "'" << std::endl;
            return 1;
        }
        auto env = std::make_shared<RValue>(RType::ENV);
        Evaluator::InitGlobalEnv(env);

        Lexer lexer(src);
        auto tokens = lexer.Tokenize();
        Parser parser(tokens);
        MiniVector<Parser::StatementLines> spans;
        MiniVector<RValuePtr> exprs = parser.ParseProgram(&spans);
        if (parser.HasError()) {
            std::cout << "Error: " << parser.GetError().c_str() << std::endl;
            return 1;
        }

        MiniVector<MiniString> lines = SplitLines(src);
        int echoed = 0; // last source line already shown
        auto echo_through = [&](int last, int stmt_first) {
            for (int ln = echoed + 1; ln <= last && ln <= (int)lines.size(); ++ln) {
                const MiniString& text = lines[ln - 1];
                if (ln < stmt_first) {
                    // Between statements: keep comments, keep blank lines as spacing
                    if (IsBlank(text)) std::cout << std::endl;
                    else std::cout << "> " << text.c_str() << std::endl;
                } else {
                    std::cout << (ln == stmt_first ? "> " : "+ ") << text.c_str() << std::endl;
                }
            }
            if (last > echoed) echoed = last;
        };

        for (size_t i = 0; i < exprs.size(); ++i) {
            if (echo) echo_through(spans[i].last, spans[i].first);
            RValuePtr result = Evaluator::Eval(exprs[i], env);
            if (!result) continue;
            if (result->type == RType::ERROR) {
                std::cout << "Error: " << result->sym_name.c_str() << std::endl;
                return 1;
            }
            if (!Evaluator::R_Visible) continue;
            MiniString s = Evaluator::ToString(result);
            if (!s.empty()) std::cout << s.c_str() << std::endl;
        }
        if (echo) echo_through((int)lines.size(), (int)lines.size() + 1); // trailing comments
        return 0;
    }
}

int main(int argc, char** argv) {
    // Script mode: minir [--echo] file.R
    bool echo = false;
    const char* script = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--echo") == 0) echo = true;
        else script = argv[i];
    }
    if (script) return RunScript(script, echo);

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
                if (!Evaluator::R_Visible) continue; // e.g. assignments
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
