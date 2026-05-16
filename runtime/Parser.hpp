#pragma once
#include "Lexer.hpp"
#include "RValue.hpp"

class Parser {
public:
    Parser(const MiniVector<Token>& tokens);
    // Source lines (1-based, inclusive) that a top-level statement spans.
    struct StatementLines { int first; int last; };

    // Parses every top-level expression (separated by newlines or ';').
    // If lines is given, it receives one entry per returned expression.
    MiniVector<RValuePtr> ParseProgram(MiniVector<StatementLines>* lines = nullptr);
    bool HasError() const { return error_state; }
    // True when the error was hitting end of input (e.g. `x <- 1 +`), so more lines may complete it.
    bool IsIncomplete() const { return error_state && incomplete; }
    MiniString GetError() const { return error_msg; }

private:
    MiniVector<Token> tokens;
    int pos = 0;
    bool error_state = false;
    bool incomplete = false;
    MiniString error_msg;

    void Fail(const MiniString& msg);
    void SkipNewlines();
    bool AtStatementEnd() const;

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
