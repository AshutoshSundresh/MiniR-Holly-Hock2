#include "Lexer.hpp"
#include <cctype>

Lexer::Lexer(const std::string& src) : src(src) {
    len = src.length();
}

char Lexer::Current() const {
    if (pos >= len) return '\0';
    return src[pos];
}

char Lexer::Peek(int offset) const {
    if (pos + offset >= len) return '\0';
    return src[pos + offset];
}

void Lexer::Advance(int n) {
    pos += n;
}

void Lexer::SkipWhitespace() {
    while (true) {
        char c = Current();
        if (c == ' ' || c == '\t' || c == '\r') {
            Advance();
        } else if (c == '\n') {
            line++;
            Advance();
        } else if (c == '#') {
            // Comment
            while (Current() != '\n' && Current() != '\0') Advance();
        } else {
            break;
        }
    }
}

std::vector<Token> Lexer::Tokenize() {
    std::vector<Token> tokens;
    pos = 0;
    line = 1;
    
    while (pos < len) {
        SkipWhitespace();
        if (pos >= len) break;
        tokens.push_back(ScanToken());
    }
    
    tokens.push_back({TokenType::eof, "", 0, line});
    return tokens;
}

Token Lexer::ScanToken() {
    char c = Current();
    Token t;
    t.line = line;
    
    // Identifier or Keyword
    if (isalpha(c) || c == '.') {
        std::string text;
        while (isalnum(Current()) || Current() == '.' || Current() == '_') {
            text += Current();
            Advance();
        }
        t.text = text;
        
        // Keywords
        if (text == "if") t.type = TokenType::keyword;
        else if (text == "else") t.type = TokenType::keyword;
        else if (text == "function") t.type = TokenType::keyword;
        else if (text == "while") t.type = TokenType::keyword;
        else if (text == "TRUE" || text == "FALSE" || text == "NA" || text == "NULL") t.type = TokenType::keyword;
        else t.type = TokenType::identifier;
        
        return t;
    }
    
    // Number
    if (isdigit(c)) {
        std::string text;
        while (isdigit(Current()) || Current() == '.') {
            text += Current();
            Advance();
        }
        t.type = TokenType::number;
        t.text = text;
        t.num_val = std::stod(text);
        return t;
    }
    
    // String
    if (c == '"' || c == '\'') {
        char quote = c;
        Advance();
        std::string text;
        while (Current() != quote && Current() != '\0') {
            text += Current();
            Advance();
        }
        if (Current() == quote) Advance();
        t.type = TokenType::string;
        t.text = text;
        return t;
    }
    
    // Operators
    Advance(); // Default advance for single chars
    t.text = std::string(1, c);
    
    switch (c) {
        case '+': t.type = TokenType::plus; break;
        case '-': t.type = TokenType::minus; break; // Check for -> ?
        case '*': t.type = TokenType::star; break; 
        case '/': t.type = TokenType::slash; break;
        case '^': t.type = TokenType::power; break;
        case '(': t.type = TokenType::lparen; break;
        case ')': t.type = TokenType::rparen; break;
        case '{': t.type = TokenType::lbrace; break;
        case '}': t.type = TokenType::rbrace; break;
        case '[': 
            if (Current() == '[') { 
                Advance(); t.type = TokenType::dbl_lbracket; t.text = "[["; 
            } else t.type = TokenType::lbracket; 
            break;
        case ']': 
            if (Current() == ']') { 
                Advance(); t.type = TokenType::dbl_rbracket; t.text = "]]"; 
            } else t.type = TokenType::rbracket; 
            break;
        case ',': t.type = TokenType::comma; break;
        case ';': t.type = TokenType::semicolon; break;
        case ':': t.type = TokenType::colon; break;
        case '$': t.type = TokenType::dollar; break;
        
        case '<': 
            if (Current() == '-') { Advance(); t.type = TokenType::assign; t.text = "<-"; }
            else if (Current() == '=') { Advance(); t.type = TokenType::le; t.text = "<="; }
            else t.type = TokenType::lt;
            break;
        case '>': 
            if (Current() == '=') { Advance(); t.type = TokenType::ge; t.text = ">="; }
            else t.type = TokenType::gt;
            break;
        case '=':
             if (Current() == '=') { Advance(); t.type = TokenType::eq; t.text = "=="; }
             else { t.type = TokenType::assign; } // = is assign in R mostly
             break;
        case '!':
             if (Current() == '=') { Advance(); t.type = TokenType::ne; t.text = "!="; }
             // else logical not (todo)
             break;
        case '%':
             // Special operators %...%
             {
                 std::string op = "%";
                 while (Current() != '%' && Current() != '\0') {
                     op += Current();
                     Advance();
                 }
                 if (Current() == '%') {
                     op += '%';
                     Advance();
                     t.text = op;
                     if (op == "%%") t.type = TokenType::mod;
                     else if (op == "%/%") t.type = TokenType::div_int;
                     else if (op == "%*%") t.type = TokenType::mat_mult;
                     else t.type = TokenType::keyword; // Unknown %op% treat as infix?
                 }
             }
             break;
    }
    
    return t;
}
