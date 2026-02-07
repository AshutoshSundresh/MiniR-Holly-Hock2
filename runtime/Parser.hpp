#pragma once
#include "Lexer.hpp"
#include "RValue.hpp"

class Parser {
public:
    Parser(const MiniVector<Token>& tokens);
    RValuePtr Parse(); // Parses one expression or a block
    bool HasError() const { return error_state; }
    MiniString GetError() const { return error_msg; }

private:
    MiniVector<Token> tokens;
    int pos = 0;
    bool error_state = false;
    MiniString error_msg;

    Token Current() const;
    void Advance();
    bool Match(TokenType t);
    bool Check(TokenType t) const;
    Token Consume(TokenType t, const MiniString& msg);
    
    // Recursive Descent
    RValuePtr ParseExpression(int precedence = 0);
    RValuePtr ParsePrimary();
    RValuePtr ParseBlock();
    RValuePtr ParseCall(RValuePtr callee);
    
    // Helpers
    int GetPrecedence(TokenType t) const;
};
