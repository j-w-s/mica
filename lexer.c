#include "mica_internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void lexer_init(Lexer* lex, const char* source) {
    lex->start = source;
    lex->current = source;
    lex->line = 1;
}

// helpers
static bool is_at_end(Lexer* lex) {
    return *lex->current == '\0';
}

static char advance(Lexer* lex) {
    return *lex->current++;
}

static char peek(Lexer* lex) {
    return *lex->current;
}

static char peek_next(Lexer* lex) {
    if (is_at_end(lex)) return '\0';
    return lex->current[1];
}

static bool match(Lexer* lex, char expected) {
    if (is_at_end(lex)) return false;
    if (*lex->current != expected) return false;
    lex->current++;
    return true;
}

static Token make_token(Lexer* lex, TokenType type) {
    Token tok;
    tok.type = type;
    tok.start = lex->start;
    tok.length = (size_t)(lex->current - lex->start);
    tok.line = lex->line;
    return tok;
}

static Token error_token(Lexer* lex, const char* message) {
    Token tok;
    tok.type = TOK_ERROR;
    tok.start = message;
    tok.length = strlen(message);
    tok.line = lex->line;
    return tok;
}

static void skip_whitespace(Lexer* lex) {
    for (;;) {
        char c = peek(lex);
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance(lex);
                break;
            case '\n':
                lex->line++;
                advance(lex);
                break;
            case '/':
                if (peek_next(lex) == '/') {
                    while (peek(lex) != '\n' && !is_at_end(lex)) {
                        advance(lex);
                    }
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

static Token read_number(Lexer* lex) {
    while (isdigit(peek(lex))) advance(lex);
    
    if (peek(lex) == '.' && isdigit(peek_next(lex))) {
        advance(lex);
        while (isdigit(peek(lex))) advance(lex);
        return make_token(lex, TOK_FLOAT);
    }
    
    return make_token(lex, TOK_INT);
}

static Token read_string(Lexer* lex) {
    while (peek(lex) != '"' && !is_at_end(lex)) {
        if (peek(lex) == '\n') lex->line++;
        advance(lex);
    }
    
    if (is_at_end(lex)) {
        return error_token(lex, "unterminated string");
    }
    
    advance(lex);
    return make_token(lex, TOK_STRING);
}

static TokenType check_keyword(Lexer* lex, int start, int length,
                               const char* rest, TokenType type) {
    if (lex->current - lex->start == start + length &&
        memcmp(lex->start + start, rest, length) == 0) {
        return type;
    }
    return TOK_IDENT;
}

static TokenType ident_type(Lexer* lex) {
    switch (lex->start[0]) {
        case 'b': return check_keyword(lex, 1, 4, "reak", TOK_BREAK);
        case 'e': return check_keyword(lex, 1, 3, "lse", TOK_ELSE);
        case 'f':
            if (lex->current - lex->start > 1) {
                switch (lex->start[1]) {
                    case 'a': return check_keyword(lex, 2, 3, "lse", TOK_FALSE);
                    case 'n': return TOK_FN;
                    case 'o': return check_keyword(lex, 2, 1, "r", TOK_FOR);
                }
            }
            break;
        case 'i':
            if (lex->current - lex->start > 1) {
                switch (lex->start[1]) {
                    case 'f': return TOK_IF;
                    case 'n': return TOK_IN;
                }
            }
            break;
        case 'l':
            if (lex->current - lex->start > 1) {
                switch (lex->start[1]) {
                    case 'e': return check_keyword(lex, 2, 1, "t", TOK_LET);
                    case 'o': return check_keyword(lex, 2, 2, "op", TOK_LOOP);
                }
            }
            break;
        case 'm':
            if (lex->current - lex->start > 1) {
                switch (lex->start[1]) {
                    case 'a': return check_keyword(lex, 2, 3, "tch", TOK_MATCH);
                    case 'u': return check_keyword(lex, 2, 1, "t", TOK_MUT);
                }
            }
            break;
        case 'r': return check_keyword(lex, 1, 5, "eturn", TOK_RETURN);
        case 't': return check_keyword(lex, 1, 3, "rue", TOK_TRUE);
        case 'w': return check_keyword(lex, 1, 4, "hile", TOK_WHILE);
        case 'N': return check_keyword(lex, 1, 3, "one", TOK_NONE);
    }
    return TOK_IDENT;
}

static Token read_ident(Lexer* lex) {
    while (isalnum(peek(lex)) || peek(lex) == '_') {
        advance(lex);
    }
    return make_token(lex, ident_type(lex));
}

Token next_token(Lexer* lex) {
    skip_whitespace(lex);
    lex->start = lex->current;
    
    if (is_at_end(lex)) return make_token(lex, TOK_EOF);
    
    char c = advance(lex);
    
    if (isdigit(c)) return read_number(lex);
    if (isalpha(c) || c == '_') return read_ident(lex);
    
    switch (c) {
        case '(': return make_token(lex, TOK_LPAREN);
        case ')': return make_token(lex, TOK_RPAREN);
        case '{': return make_token(lex, TOK_LBRACE);
        case '}': return make_token(lex, TOK_RBRACE);
        case '[': return make_token(lex, TOK_LBRACKET);
        case ']': return make_token(lex, TOK_RBRACKET);
        case ',': return make_token(lex, TOK_COMMA);
        case '.': return make_token(lex, TOK_DOT);
        case ':': return make_token(lex, TOK_COLON);
        case ';': return make_token(lex, TOK_SEMICOLON);
        case '+': return make_token(lex, TOK_PLUS);
        case '*': return make_token(lex, TOK_STAR);
        case '%': return make_token(lex, TOK_PERCENT);
        case '|': return make_token(lex, TOK_PIPE);
        case '-':
            return make_token(lex, match(lex, '>') ? TOK_ARROW : TOK_MINUS);
        case '/': return make_token(lex, TOK_SLASH);
        case '=':
            if (match(lex, '=')) return make_token(lex, TOK_EQ);
            if (match(lex, '>')) return make_token(lex, TOK_RARROW);
            return make_token(lex, TOK_ASSIGN);
        case '!':
            return make_token(lex, match(lex, '=') ? TOK_NE : TOK_ERROR);
        case '<':
            return make_token(lex, match(lex, '=') ? TOK_LE : TOK_LT);
        case '>':
            return make_token(lex, match(lex, '=') ? TOK_GE : TOK_GT);
        case '"': return read_string(lex);
    }
    
    return error_token(lex, "unexpected character");
}