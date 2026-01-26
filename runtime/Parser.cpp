#include "Parser.hpp"
#include <iostream>

// Precedence levels
enum Precedence {
    PREC_NONE = 0,
    PREC_ASSIGNMENT, // <- =
    PREC_OR,         // |
    PREC_AND,        // &
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * / %
    PREC_EXPONENT,   // ^
    PREC_COLON,      // :
    PREC_CALL,       // ( [ $
};

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens) {
    pos = 0;
}

Token Parser::Current() const {
    if (tokens.empty() || pos >= (int)tokens.size()) return Token{TokenType::eof, "", 0, 0};
    return tokens[pos];
}

void Parser::Advance() {
    if (pos < (int)tokens.size()) pos++;
}

bool Parser::Check(TokenType t) const {
    return Current().type == t;
}

bool Parser::Match(TokenType t) {
    if (Check(t)) {
        Advance();
        return true;
    }
    return false;
}

Token Parser::Consume(TokenType t, const std::string& msg) {
    if (Check(t)) {
        Token tok = Current();
        Advance();
        return tok;
    }
    error_state = true;
    error_msg = msg + " at line " + std::to_string(Current().line);
    return Token{TokenType::eof, "", 0, 0};
}

int Parser::GetPrecedence(TokenType t) const {
    switch (t) {
        case TokenType::assign: return PREC_ASSIGNMENT;
        case TokenType::lt: 
        case TokenType::le:
        case TokenType::gt:
        case TokenType::ge: return PREC_COMPARISON; // R: < > <= >=
        case TokenType::eq:
        case TokenType::ne: return PREC_EQUALITY;
        case TokenType::pipe: return PREC_OR;
        case TokenType::amp: return PREC_AND;
        case TokenType::plus:
        case TokenType::minus: return PREC_TERM;
        case TokenType::star:
        case TokenType::slash:
        case TokenType::mod:
        case TokenType::div_int:
        case TokenType::mat_mult:
        case TokenType::infix: return PREC_FACTOR;
        case TokenType::power: return PREC_EXPONENT;
        case TokenType::colon: return PREC_COLON;
        case TokenType::lparen: 
        case TokenType::lbracket:
        case TokenType::dbl_lbracket:
        case TokenType::dollar: return PREC_CALL;
        case TokenType::bang: return PREC_NONE; // unary, handled in ParsePrimary
        default: return PREC_NONE;
    }
}

RValuePtr Parser::Parse() {
    if (Check(TokenType::eof)) return RR_Nil();
    return ParseExpression();
}

RValuePtr Parser::ParseExpression(int precedence) {
    RValuePtr left = ParsePrimary();
    if (error_state) return left;

    while (precedence < GetPrecedence(Current().type)) {
        Token op = Current();
        Advance();
        
        // Handle Call / Subscript / Field access
        if (op.type == TokenType::lparen) {
            left = ParseCall(left);
        } else if (op.type == TokenType::lbracket || op.type == TokenType::dbl_lbracket) {
            // Backtrack slightly or pass op info?
            // Actually usually ParseSubscript would handle the index parsing
            // Re-use logic for [ and [[
            // Simplified:
            auto call = std::make_shared<RValue>(RType::LIST);
            call->l_vec.push_back(std::make_shared<RValue>(RType::SYMBOL));
            call->l_vec.back()->sym_name = (op.type == TokenType::lbracket) ? "[" : "[[";
            call->l_vec.push_back(left);
            
            // Parse indices
            while (!Check(op.type == TokenType::lbracket ? TokenType::rbracket : TokenType::dbl_rbracket) && !Check(TokenType::eof)) {
                if (Check(TokenType::comma)) {
                    // Empty argument (missing index) -> Symbol("") or implicit?
                    // R: [1, ] is 1, missing.
                    call->l_vec.push_back(std::make_shared<RValue>(RType::NIL)); // Gap
                } else {
                    call->l_vec.push_back(ParseExpression());
                }
                if (!Match(TokenType::comma)) break;
            }
            Consume(op.type == TokenType::lbracket ? TokenType::rbracket : TokenType::dbl_rbracket, "Expect closing bracket");
            left = call;
        } else if (op.type == TokenType::dollar) {
             Token field = Consume(TokenType::identifier, "Expect field name after $");
             // Turn into call: `$`(left, "field")
             auto call = std::make_shared<RValue>(RType::LIST);
             call->l_vec.push_back(std::make_shared<RValue>(RType::SYMBOL));
             call->l_vec.back()->sym_name = "$";
             call->l_vec.push_back(left);
             
             auto s = std::make_shared<RValue>(RType::CHARACTER);
             s->s_vec.push_back(field.text);
             call->l_vec.push_back(s);
             left = call;
        } else {
            // Binary Operator. ^ is right-associative in R (2^3^2 = 2^(3^2)), so parse RHS with lower precedence.
            int rhs_prec = (op.type == TokenType::power) ? GetPrecedence(op.type) - 1 : GetPrecedence(op.type);
            RValuePtr right = ParseExpression(rhs_prec);
            
            auto call = std::make_shared<RValue>(RType::LIST);
            // Function name is the operator
            auto func = std::make_shared<RValue>(RType::SYMBOL);
            func->sym_name = op.text; 
            call->l_vec.push_back(func);
            call->l_vec.push_back(left);
            call->l_vec.push_back(right);
            
            left = call;
        }
    }
    return left;
}

RValuePtr Parser::ParsePrimary() {
    if (Check(TokenType::invalid)) {
        error_state = true;
        error_msg = "Invalid token: '" + Current().text + "' at line " + std::to_string(Current().line);
        return RR_Nil();
    }
    if (Match(TokenType::bang)) {
        RValuePtr rhs = ParseExpression(PREC_AND);
        auto call = std::make_shared<RValue>(RType::LIST);
        auto func = std::make_shared<RValue>(RType::SYMBOL);
        func->sym_name = "!";
        call->l_vec.push_back(func);
        call->l_vec.push_back(rhs);
        return call;
    }
    if (Match(TokenType::identifier)) {
        Token t = tokens[pos-1];
        auto r = std::make_shared<RValue>(RType::SYMBOL);
        r->sym_name = t.text;
        return r;
    }
    if (Match(TokenType::number)) {
        Token t = tokens[pos-1];
        auto r = std::make_shared<RValue>(RType::DOUBLE);
        r->d_vec.push_back(t.num_val);
        return r;
    }
    if (Match(TokenType::string)) {
        Token t = tokens[pos-1];
        auto r = std::make_shared<RValue>(RType::CHARACTER);
        r->s_vec.push_back(t.text);
        return r;
    }
    if (Match(TokenType::keyword)) {
        Token t = tokens[pos-1];
        if (t.text == "TRUE") {
             auto r = std::make_shared<RValue>(RType::LOGICAL); r->i_vec.push_back(1); return r;
        }
        if (t.text == "FALSE") {
             auto r = std::make_shared<RValue>(RType::LOGICAL); r->i_vec.push_back(0); return r;
        }
        if (t.text == "NA") {
             auto r = std::make_shared<RValue>(RType::LOGICAL); r->i_vec.push_back(R_LOGICAL_NA); return r;
        }
        if (t.text == "NULL") {
             return RR_Nil();
        }
        if (t.text == "function") {
             Consume(TokenType::lparen, "Expect '(' after function");
             // Parse Formals (simplified: just list of symbols)
             auto formals = std::make_shared<RValue>(RType::LIST);
             if (!Check(TokenType::rparen)) {
                 do {
                     Token arg = Consume(TokenType::identifier, "Expect argument name");
                     auto sym = std::make_shared<RValue>(RType::SYMBOL);
                     sym->sym_name = arg.text;
                     formals->l_vec.push_back(sym);
                     // Default values? todo
                     if (Check(TokenType::assign)) {
                         Advance();
                         ParseExpression(); // Ignore default value for now in AST or store it?
                     }
                 } while (Match(TokenType::comma));
             }
             Consume(TokenType::rparen, "Expect ')' after args");
             
             RValuePtr body = ParseExpression();
             
             auto closure = std::make_shared<RValue>(RType::CLOSURE);
             closure->formals = formals;
             closure->body = body;
             return closure;
        }
        if (t.text == "if") {
            Consume(TokenType::lparen, "Expect '('");
            RValuePtr cond = ParseExpression();
            Consume(TokenType::rparen, "Expect ')'");
            RValuePtr then_branch = ParseExpression();
            RValuePtr else_branch = nullptr;
            if (Match(TokenType::keyword)) { // Basic check for else? Tokenizer needs to handle else
                 if (tokens[pos-1].text == "else") {
                     else_branch = ParseExpression();
                 } else {
                     pos--; // Backtrack if not else
                 }
            }
            // Construct IF call: `if`(cond, then, else)
            auto call = std::make_shared<RValue>(RType::LIST);
            auto func = std::make_shared<RValue>(RType::SYMBOL); func->sym_name = "if";
            call->l_vec.push_back(func);
            call->l_vec.push_back(cond);
            call->l_vec.push_back(then_branch);
            if(else_branch) call->l_vec.push_back(else_branch);
            return call;
        }
        if (t.text == "while") {
            Consume(TokenType::lparen, "Expect '(' after while");
            RValuePtr cond = ParseExpression();
            Consume(TokenType::rparen, "Expect ')'");
            RValuePtr body = ParseExpression();
            auto call = std::make_shared<RValue>(RType::LIST);
            auto func = std::make_shared<RValue>(RType::SYMBOL); func->sym_name = "while";
            call->l_vec.push_back(func);
            call->l_vec.push_back(cond);
            call->l_vec.push_back(body);
            return call;
        }
        if (t.text == "for") {
            Consume(TokenType::lparen, "Expect '(' after for");
            Token var_tok = Consume(TokenType::identifier, "Expect variable name in for");
            Consume(TokenType::comma, "Expect ',' in for");
            RValuePtr seq_expr = ParseExpression();
            Consume(TokenType::rparen, "Expect ')'");
            RValuePtr body = ParseExpression();
            auto var_sym = std::make_shared<RValue>(RType::SYMBOL);
            var_sym->sym_name = var_tok.text;
            auto call = std::make_shared<RValue>(RType::LIST);
            auto func = std::make_shared<RValue>(RType::SYMBOL); func->sym_name = "for";
            call->l_vec.push_back(func);
            call->l_vec.push_back(var_sym);
            call->l_vec.push_back(seq_expr);
            call->l_vec.push_back(body);
            return call;
        }
    }
    
    // Unary + and - (e.g. -2, +1 in c(-2,-1,0,1,2))
    if (Match(TokenType::minus)) {
        RValuePtr rhs = ParseExpression(PREC_EXPONENT);
        if (error_state) return rhs;
        auto call = std::make_shared<RValue>(RType::LIST);
        auto func = std::make_shared<RValue>(RType::SYMBOL);
        func->sym_name = "-";
        call->l_vec.push_back(func);
        call->l_vec.push_back(rhs);
        return call;
    }
    if (Match(TokenType::plus)) {
        RValuePtr rhs = ParseExpression(PREC_EXPONENT);
        if (error_state) return rhs;
        auto call = std::make_shared<RValue>(RType::LIST);
        auto func = std::make_shared<RValue>(RType::SYMBOL);
        func->sym_name = "+";
        call->l_vec.push_back(func);
        call->l_vec.push_back(rhs);
        return call;
    }
    
    if (Match(TokenType::lparen)) {
        RValuePtr expr = ParseExpression();
        Consume(TokenType::rparen, "Expect ')'");
        return expr;
    }
    
    if (Match(TokenType::lbrace)) {
        return ParseBlock();
    }
    
    error_state = true;
    error_msg = "Unexpected token: " + Current().text;
    return RR_Nil();
}

RValuePtr Parser::ParseBlock() {
    auto block_call = std::make_shared<RValue>(RType::LIST);
    auto func = std::make_shared<RValue>(RType::SYMBOL); func->sym_name = "{";
    block_call->l_vec.push_back(func);
    
    while (!Check(TokenType::rbrace) && !Check(TokenType::eof)) {
        block_call->l_vec.push_back(ParseExpression());
        // Optional semicolons or newlines?
        while (Match(TokenType::semicolon)); 
    }
    Consume(TokenType::rbrace, "Expect '}'");
    return block_call;
}

RValuePtr Parser::ParseCall(RValuePtr callee) {
    auto call = std::make_shared<RValue>(RType::LIST);
    call->l_vec.push_back(callee);
    
    // Attribute for names
    auto names = std::make_shared<RValue>(RType::CHARACTER);
    names->s_vec.push_back(""); // Name for callee (empty)
    bool has_names = false;

    if (!Check(TokenType::rparen)) {
        do {
            // Named args? name=val (R uses single = for named arguments)
             if (pos + 1 < (int)tokens.size() && tokens[pos].type == TokenType::identifier && tokens[pos+1].type == TokenType::assign) {
                 Token name = tokens[pos];
                 Advance(); // Eat name
                 Advance(); // Eat =
                 
                 names->s_vec.push_back(name.text);
                 has_names = true;
                 
                 call->l_vec.push_back(ParseExpression());
             } else {
                 names->s_vec.push_back("");
                 call->l_vec.push_back(ParseExpression()); 
             }
        } while (Match(TokenType::comma));
    }
    Consume(TokenType::rparen, "Expect ')'");
    
    if (has_names) {
        call->attributes["names"] = names;
    }
    return call;
}
