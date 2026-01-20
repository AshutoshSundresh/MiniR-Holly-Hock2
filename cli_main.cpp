#include <iostream>
#include <string>
#include <vector>

#include "runtime/Evaluator.hpp"
#include "runtime/Parser.hpp"
#include "runtime/Lexer.hpp"

// We need to implement ToString if it wasn't implemented or exposed correctly.
// Assuming it is exposed in Evaluator.hpp as verified.

int main() {
    std::cout << "MiniR CLI (v0.1.0)" << std::endl;
    std::cout << "Type 'exit' or 'quit' to leave." << std::endl;

    // Initialize Environment
    auto env = std::make_shared<RValue>(RType::ENV);
    Evaluator::InitGlobalEnv(env);

    while (true) {
        std::cout << "> ";
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line == "exit" || line == "quit" || line == "q()") break;
        if (line.empty()) continue;

        try {
            // Lex
            Lexer lexer(line);
            auto tokens = lexer.Tokenize();
            
            // Parse
            Parser parser(tokens);
            // Parser::Parse usually returns a single expression or a block.
            RValuePtr ast = parser.Parse();
            
            // Evaluate
            // The AST might be a list of expressions if the parser supports multiple.
            // But Parser::Parse usually returns a single expression or a block.
            // Let's assume it returns one RValuePtr.
            
            if (ast) {
                RValuePtr result = Evaluator::Eval(ast, env);
                
                // Print
                if (result) {
                     // Check for error
                     if (result->type == RType::ERROR) {
                         std::cout << "Error: " << result->sym_name << std::endl;
                     } else {
                         std::string s = Evaluator::ToString(result);
                         // R doesn't print invisible returns usually, but for REPL we print.
                         // Check if result is NOT NULL or we print [1] ...
                         if (!s.empty()) std::cout << s << std::endl;
                     }
                }
            }
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << std::endl;
        }
    }
    return 0;
}
