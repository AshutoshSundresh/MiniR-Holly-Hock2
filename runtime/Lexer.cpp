#include "Lexer.hpp"
#include <cctype>

// Simple decimal parser for tokens the lexer accepts (digits and at most one '.').
// Supports optional leading sign, no exponent notation.
static bool ParseSimpleDecimal(const MiniString& text, double& out) {
    if (text.empty()) return false;
    size_t i = 0;
    bool neg = false;
    if (text[0] == '+' || text[0] == '-') {
        neg = (text[0] == '-');
        i = 1;
    }
    bool seen_dot = false;
    bool seen_digit = false;
    long long int_part = 0;
    double frac_part = 0.0;
    double frac_scale = 1.0;

    for (; i < text.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '.') {
            if (seen_dot) return false;
            seen_dot = true;
            continue;
        }
        if (!std::isdigit(ch)) return false;
        seen_digit = true;
        int digit = ch - '0';
        if (!seen_dot) {
            int_part = int_part * 10 + digit;
        } else {
            frac_scale *= 0.1;
            frac_part += digit * frac_scale;
        }
    }
    if (!seen_digit) return false;
    double val = static_cast<double>(int_part) + frac_part;
    out = neg ? -val : val;
    return true;
}

Lexer::Lexer(const MiniString& src) : src(src) {
    // MiniString is null-terminated, but we track logical length.
    len = static_cast<int>(src.size());
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

MiniVector<Token> Lexer::Tokenize() {
    MiniVector<Token> tokens;
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
    t.type = TokenType::invalid;
    
    // Identifier or Keyword
    if (isalpha(c) || c == '.') {
        // R allows leading-dot numbers like `.5`
        if (c == '.' && isdigit(Peek())) {
            MiniString text(".");
            Advance();
            while (isdigit(Current()) || Current() == '.') {
                text.push_back(Current());
                Advance();
            }
            t.type = TokenType::number;
            t.text = text;
            double val = 0.0;
            if (!ParseSimpleDecimal(text, val)) {
                t.type = TokenType::invalid;
            } else {
                t.num_val = val;
            }
            return t;
        }

        MiniString text;
        while (isalnum(Current()) || Current() == '.' || Current() == '_') {
            text.push_back(Current());
            Advance();
        }
        t.text = text;
        
        // Keywords
        if (text == "if") t.type = TokenType::keyword;
        else if (text == "else") t.type = TokenType::keyword;
        else if (text == "function") t.type = TokenType::keyword;
        else if (text == "while") t.type = TokenType::keyword;
        else if (text == "for") t.type = TokenType::keyword;
        else if (text == "in") t.type = TokenType::keyword;
        else if (text == "TRUE" || text == "FALSE" || text == "NA" || text == "NULL") t.type = TokenType::keyword;
        else t.type = TokenType::identifier;
        
        return t;
    }
    
    // Number
    if (isdigit(c)) {
        MiniString text;
        while (isdigit(Current()) || Current() == '.') {
            text.push_back(Current());
            Advance();
        }
        t.type = TokenType::number;
        t.text = text;
        double val = 0.0;
        if (!ParseSimpleDecimal(text, val)) {
            t.type = TokenType::invalid;
        } else {
            t.num_val = val;
        }
        return t;
    }
    
    // String
    if (c == '"' || c == '\'') {
        char quote = c;
        Advance();
        MiniString text;
        while (Current() != quote && Current() != '\0') {
            text.push_back(Current());
            Advance();
        }
        if (Current() == quote) Advance();
        t.type = TokenType::string;
        t.text = text;
        return t;
    }
    
    // Operators
    Advance(); // Default advance for single chars
    char tmp[2] = { c, '\0' };
    t.text = MiniString(tmp);
    
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
             else { t.type = TokenType::bang; t.text = "!"; }
             break;
        case '&': t.type = TokenType::amp; t.text = "&"; break;
        case '|': t.type = TokenType::pipe; t.text = "|"; break;
        case '%':
             // Special operators %...%
             {
                 MiniString op("%");
                 while (Current() != '%' && Current() != '\0') {
                     op.push_back(Current());
                     Advance();
                 }
                 if (Current() == '%') {
                     op.push_back('%');
                     Advance();
                     t.text = op;
                     if (op == "%%") t.type = TokenType::mod;
                     else if (op == "%/%") t.type = TokenType::div_int;
                     else if (op == "%*%") t.type = TokenType::mat_mult;
                     else t.type = TokenType::infix; // User-defined %op% infix operator
                 } else {
                     // Malformed %op% without a closing '%'
                     t.type = TokenType::invalid;
                     t.text = op;
                 }
             }
             break;
        default:
            // Keep TokenType::invalid for unknown characters
            break;
    }
    
    return t;
}
