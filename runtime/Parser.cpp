#include "Parser.hpp"

// Precedence levels, lowest to highest (matches R's ?Syntax)
enum Precedence {
    PREC_NONE = 0,
    PREC_ASSIGNMENT, // <- =   (right-associative)
    PREC_OR,         // |
    PREC_AND,        // &
    PREC_NOT,        // unary !
    PREC_COMPARISON, // == != < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_SPECIAL,    // %% %/% %*% %in% %any%
    PREC_COLON,      // :
    PREC_UNARY,      // unary + -
    PREC_EXPONENT,   // ^      (right-associative)
    PREC_CALL,       // ( [ [[ $
};

Parser::Parser(const MiniVector<Token>& tokens) : tokens(tokens) {
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

Token Parser::Consume(TokenType t, const MiniString& msg) {
    if (Check(t)) {
        Token tok = Current();
        Advance();
        return tok;
    }
    Fail(msg);
    return Token{TokenType::eof, "", 0, 0};
}

void Parser::Fail(const MiniString& msg) {
    if (error_state) return; // keep the first error
    error_state = true;
    incomplete = Check(TokenType::eof);
    error_msg = msg;
    if (incomplete) {
        error_msg += " (unexpected end of input)";
    } else {
        error_msg += " at line ";
        error_msg += MiniToString(Current().line);
    }
}

void Parser::SkipNewlines() {
    while (Check(TokenType::newline)) Advance();
}

bool Parser::AtStatementEnd() const {
    return Check(TokenType::newline) || Check(TokenType::semicolon) || Check(TokenType::eof);
}

int Parser::GetPrecedence(TokenType t) const {
    switch (t) {
        case TokenType::assign: return PREC_ASSIGNMENT;
        case TokenType::lt: 
        case TokenType::le:
        case TokenType::gt:
        case TokenType::ge:
        case TokenType::eq:
        case TokenType::ne: return PREC_COMPARISON;
        case TokenType::pipe: return PREC_OR;
        case TokenType::amp: return PREC_AND;
        case TokenType::plus:
        case TokenType::minus: return PREC_TERM;
        case TokenType::star:
        case TokenType::slash: return PREC_FACTOR;
        case TokenType::mod:
        case TokenType::div_int:
        case TokenType::mat_mult:
        case TokenType::infix: return PREC_SPECIAL;
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

MiniVector<RValuePtr> Parser::ParseProgram() {
    MiniVector<RValuePtr> exprs;
    while (true) {
        while (Match(TokenType::newline) || Match(TokenType::semicolon));
        if (Check(TokenType::eof)) break;
        RValuePtr e = ParseExpression();
        if (error_state) break;
        if (!AtStatementEnd()) {
            Fail("Unexpected '" + Current().text + "'");
            break;
        }
        exprs.push_back(e);
    }
    return exprs;
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
            
            // Parse indices: allow leading comma M[,2] and trailing comma M[1,]
            TokenType closing = (op.type == TokenType::lbracket) ? TokenType::rbracket : TokenType::dbl_rbracket;
            while (!Check(closing) && !Check(TokenType::eof)) {
                if (Check(TokenType::comma)) {
                    // Leading comma: push NIL for missing arg, advance past comma
                    call->l_vec.push_back(RR_Nil());
                    Advance();
                    // If next is closing bracket, we're done (trailing comma case handled below)
                    if (Check(closing)) break;
                    // Otherwise continue loop to parse next expression
                } else {
                    // Parse expression
                    call->l_vec.push_back(ParseExpression());
                    // Check for comma after expression
                    if (!Match(TokenType::comma)) break;
                    // If comma followed by closing bracket, push NIL for trailing arg
                    if (Check(closing)) { call->l_vec.push_back(RR_Nil()); break; }
                }
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
            // Binary Operator. ^ and assignment are right-associative in R (2^3^2 = 2^(3^2),
            // a <- b <- 1), so parse their RHS one level lower to let the same operator nest.
            bool right_assoc = (op.type == TokenType::power || op.type == TokenType::assign);
            int rhs_prec = right_assoc ? GetPrecedence(op.type) - 1 : GetPrecedence(op.type);
            SkipNewlines(); // a trailing operator continues the expression on the next line
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
        Fail("Invalid token: '" + Current().text + "'");
        return RR_Nil();
    }
    if (Match(TokenType::bang)) {
        RValuePtr rhs = ParseExpression(PREC_NOT);
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
        if (t.is_int) {
            auto r = std::make_shared<RValue>(RType::INTEGER);
            r->i_vec.push_back((int)t.num_val);
            return r;
        }
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
             auto formals = std::make_shared<RValue>(RType::LIST);
             auto defaults = std::make_shared<RValue>(RType::LIST);
             if (!Check(TokenType::rparen)) {
                 do {
                     Token arg = Consume(TokenType::identifier, "Expect argument name");
                     auto sym = std::make_shared<RValue>(RType::SYMBOL);
                     sym->sym_name = arg.text;
                     formals->l_vec.push_back(sym);
                     if (Check(TokenType::assign)) {
                         Advance();
                         defaults->l_vec.push_back(ParseExpression());
                     } else {
                         defaults->l_vec.push_back(RR_Nil()); // no default
                     }
                 } while (Match(TokenType::comma));
             }
             Consume(TokenType::rparen, "Expect ')' after args");
             SkipNewlines();

             RValuePtr body = ParseExpression();

             auto closure = std::make_shared<RValue>(RType::CLOSURE);
             closure->formals = formals;
             closure->body = body;
             closure->attributes["defaults"] = defaults;
             return closure;
        }
        if (t.text == "if") {
            Consume(TokenType::lparen, "Expect '('");
            RValuePtr cond = ParseExpression();
            Consume(TokenType::rparen, "Expect ')'");
            SkipNewlines();
            RValuePtr then_branch = ParseExpression();
            RValuePtr else_branch = nullptr;
            // `else` may follow on a later line (as it does inside braces)
            int look = pos;
            while (look < (int)tokens.size() && tokens[look].type == TokenType::newline) look++;
            if (look < (int)tokens.size() && tokens[look].type == TokenType::keyword && tokens[look].text == "else") {
                pos = look + 1;
                SkipNewlines();
                else_branch = ParseExpression();
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
            SkipNewlines();
            RValuePtr body = ParseExpression();
            auto call = std::make_shared<RValue>(RType::LIST);
            auto func = std::make_shared<RValue>(RType::SYMBOL); func->sym_name = "while";
            call->l_vec.push_back(func);
            call->l_vec.push_back(cond);
            call->l_vec.push_back(body);
            return call;
        }
        if (t.text == "for") {
            // Parse R-style for loop: for (i in expr) body
            Consume(TokenType::lparen, "Expect '(' after for");
            Token var_tok = Consume(TokenType::identifier, "Expect variable name in for");

            // Expect the 'in' keyword
            if (!(Match(TokenType::keyword) && tokens[pos-1].text == "in")) {
                Fail("Expect 'in' in for");
                return RR_Nil();
            }

            RValuePtr seq_expr = ParseExpression();
            Consume(TokenType::rparen, "Expect ')'");
            SkipNewlines();
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
        RValuePtr rhs = ParseExpression(PREC_UNARY);
        if (error_state) return rhs;
        auto call = std::make_shared<RValue>(RType::LIST);
        auto func = std::make_shared<RValue>(RType::SYMBOL);
        func->sym_name = "-";
        call->l_vec.push_back(func);
        call->l_vec.push_back(rhs);
        return call;
    }
    if (Match(TokenType::plus)) {
        RValuePtr rhs = ParseExpression(PREC_UNARY);
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
    
    if (Check(TokenType::newline)) Fail("Unexpected newline");
    else Fail("Unexpected token: " + Current().text);
    return RR_Nil();
}

RValuePtr Parser::ParseBlock() {
    auto block_call = std::make_shared<RValue>(RType::LIST);
    auto func = std::make_shared<RValue>(RType::SYMBOL); func->sym_name = "{";
    block_call->l_vec.push_back(func);
    
    while (true) {
        while (Match(TokenType::newline) || Match(TokenType::semicolon));
        if (Check(TokenType::rbrace) || Check(TokenType::eof)) break;
        block_call->l_vec.push_back(ParseExpression());
        if (error_state) return block_call;
        if (!AtStatementEnd() && !Check(TokenType::rbrace)) {
            Fail("Unexpected '" + Current().text + "'");
            return block_call;
        }
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
