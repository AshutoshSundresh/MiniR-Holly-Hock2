#pragma once
#include "Containers.hpp"
 
enum class TokenType {
    eof,
    identifier,
    number,
    string,
    keyword, // if, else, function, while, TRUE, FALSE, NA, NULL
    invalid, // lexer error / unknown token
    
    // Operators
    eq, ne, lt, le, gt, ge, // == != < <= > >=
    assign, // <- or = (treated same or distinct?)
    plus, minus, star, slash, power, // + - * / ^
    mod, div_int, mat_mult, // %% %/% %*%
    infix, // user-defined %op% operators
    bang, amp, pipe, // ! & |
    colon, // :
    dollar, // $
    
    // Punctuation
    lparen, rparen, // ( )
    lbrace, rbrace, // { }
    lbracket, rbracket, // [ ]
    dbl_lbracket, dbl_rbracket, // [[ ]]
    comma, semicolon // , ;
};

struct Token {
    TokenType type;
    MiniString text;
    double num_val = 0.0;
    int line = 0;
};

class Lexer {
public:
    Lexer(const MiniString& src);
    MiniVector<Token> Tokenize();
    
private:
    MiniString src;
    int pos = 0;
    int len = 0;
    int line = 1;
    
    char Current() const;
    char Peek(int offset = 1) const;
    void Advance(int n = 1);
    
    Token ScanToken();
    void SkipWhitespace();
};
