#pragma once
#include "Lexer.hpp"
#include "RValue.hpp"

class Parser {
public:
    Parser(const std::vector<Token>& tokens);
    RValuePtr Parse(); // Parses one expression or a block
    bool IsComplete() const { return pos >= (int)tokens.size() || tokens[pos].type == TokenType::eof; }
    bool HasError() const { return error_state; }
    std::string GetError() const { return error_msg; }

private:
    std::vector<Token> tokens;
    int pos = 0;
    bool error_state = false;
    std::string error_msg;

    Token Current() const;
    void Advance();
    bool Match(TokenType t);
    bool Check(TokenType t) const;
    Token Consume(TokenType t, const std::string& msg);
    
    // Recursive Descent
    RValuePtr ParseExpression(int precedence = 0);
    RValuePtr ParsePrimary();
    RValuePtr ParseBlock();
    RValuePtr ParseCall(RValuePtr callee);
    RValuePtr ParseSubscript(RValuePtr left);
    
    // Helpers
    int GetPrecedence(TokenType t) const;
};
