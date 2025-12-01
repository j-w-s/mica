#include "mica_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// parser state
typedef struct {
    Token current;
    Token previous;
    Lexer* lexer;
    bool had_error;
    bool panic_mode;
} Parser;

// forward declarations
static AstNode* parse_expr(Parser* p);
static AstNode* parse_stmt(Parser* p);
static AstNode* parse_block(Parser* p);

// error reporting
static void error_at(Parser* p, Token* tok, const char* message) {
    if (p->panic_mode) return;
    p->panic_mode = true;
    p->had_error = true;
    
    fprintf(stderr, "[line %d] error", tok->line);
    if (tok->type == TOK_EOF) {
        fprintf(stderr, " at end");
    } else if (tok->type == TOK_ERROR) {
        // nothing
    } else {
        fprintf(stderr, " at '%.*s'", (int)tok->length, tok->start);
    }
    fprintf(stderr, ": %s\n", message);
}

static void error(Parser* p, const char* message) {
    error_at(p, &p->previous, message);
}

static void error_current(Parser* p, const char* message) {
    error_at(p, &p->current, message);
}

// token management
static void advance(Parser* p) {
    p->previous = p->current;
    
    for (;;) {
        p->current = next_token(p->lexer);
        if (p->current.type != TOK_ERROR) break;
        error_current(p, p->current.start);
    }
}

static bool check(Parser* p, TokenType type) {
    return p->current.type == type;
}

static bool match(Parser* p, TokenType type) {
    if (!check(p, type)) return false;
    advance(p);
    return true;
}

static void consume(Parser* p, TokenType type, const char* message) {
    if (p->current.type == type) {
        advance(p);
        return;
    }
    error_current(p, message);
}

// ast node creation helpers
static AstNode* new_node(AstType type) {
    AstNode* node = malloc(sizeof(AstNode));
    node->type = type;
    return node;
}

static AstNode* new_int(int32_t val) {
    AstNode* node = new_node(AST_INT);
    node->int_val = val;
    return node;
}

static AstNode* new_float(float val) {
    AstNode* node = new_node(AST_FLOAT);
    node->float_val = val;
    return node;
}

static AstNode* new_bool(bool val) {
    AstNode* node = new_node(AST_BOOL);
    node->bool_val = val;
    return node;
}

static AstNode* new_string(const char* str, size_t len) {
    AstNode* node = new_node(AST_STRING);
    node->str_val = malloc(len + 1);
    memcpy(node->str_val, str, len);
    node->str_val[len] = '\0';
    return node;
}

static AstNode* new_ident(const char* name, size_t len) {
    AstNode* node = new_node(AST_IDENT);
    node->str_val = malloc(len + 1);
    memcpy(node->str_val, name, len);
    node->str_val[len] = '\0';
    return node;
}

// pratt parser precedence
typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,
    PREC_OR,
    PREC_AND,
    PREC_EQUALITY,
    PREC_COMPARISON,
    PREC_TERM,
    PREC_FACTOR,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

// parse primary expressions
static AstNode* parse_primary(Parser* p) {
    if (match(p, TOK_INT)) {
        int32_t val = strtol(p->previous.start, NULL, 10);
        return new_int(val);
    }
    
    if (match(p, TOK_FLOAT)) {
        float val = strtof(p->previous.start, NULL);
        return new_float(val);
    }
    
    if (match(p, TOK_TRUE)) return new_bool(true);
    if (match(p, TOK_FALSE)) return new_bool(false);
    if (match(p, TOK_NONE)) return new_node(AST_NONE);
    
    if (match(p, TOK_STRING)) {
        return new_string(p->previous.start + 1, p->previous.length - 2);
    }
    
    if (match(p, TOK_IDENT)) {
        return new_ident(p->previous.start, p->previous.length);
    }
    
    if (match(p, TOK_LPAREN)) {
        AstNode* expr = parse_expr(p);
        consume(p, TOK_RPAREN, "expected ')' after expression");
        return expr;
    }
    
    if (match(p, TOK_LBRACKET)) {
        AstNode* node = new_node(AST_ARRAY);
        node->array.elements = NULL;
        node->array.count = 0;
        
        if (!check(p, TOK_RBRACKET)) {
            size_t capacity = 8;
            node->array.elements = malloc(sizeof(AstNode*) * capacity);
            
            do {
                if (node->array.count >= capacity) {
                    capacity *= 2;
                    node->array.elements = realloc(node->array.elements, 
                                                  sizeof(AstNode*) * capacity);
                }
                node->array.elements[node->array.count++] = parse_expr(p);
            } while (match(p, TOK_COMMA));
        }
        
        consume(p, TOK_RBRACKET, "expected ']' after array elements");
        return node;
    }
    
    if (match(p, TOK_PIPE)) {
        AstNode* node = new_node(AST_CLOSURE);
        node->closure.params = NULL;
        node->closure.param_count = 0;
        
        if (!check(p, TOK_PIPE)) {
            size_t capacity = 4;
            node->closure.params = malloc(sizeof(char*) * capacity);
            
            do {
                consume(p, TOK_IDENT, "expected parameter name");
                if (node->closure.param_count >= capacity) {
                    capacity *= 2;
                    node->closure.params = realloc(node->closure.params,
                                                  sizeof(char*) * capacity);
                }
                char* param = malloc(p->previous.length + 1);
                memcpy(param, p->previous.start, p->previous.length);
                param[p->previous.length] = '\0';
                node->closure.params[node->closure.param_count++] = param;
            } while (match(p, TOK_COMMA));
        }
        
        consume(p, TOK_PIPE, "expected '|' after parameters");
        
        if (match(p, TOK_LBRACE)) {
            node->closure.body = parse_block(p);
        } else {
            node->closure.body = parse_expr(p);
        }
        
        return node;
    }
    
    error(p, "expected expression");
    return new_node(AST_NONE);
}

// parse call and index
static AstNode* parse_postfix(Parser* p) {
    AstNode* expr = parse_primary(p);
    
    for (;;) {
        if (match(p, TOK_LPAREN)) {
            AstNode* call = new_node(AST_CALL);
            call->call.callee = expr;
            call->call.args = NULL;
            call->call.arg_count = 0;
            
            if (!check(p, TOK_RPAREN)) {
                size_t capacity = 4;
                call->call.args = malloc(sizeof(AstNode*) * capacity);
                
                do {
                    if (call->call.arg_count >= capacity) {
                        capacity *= 2;
                        call->call.args = realloc(call->call.args,
                                                 sizeof(AstNode*) * capacity);
                    }
                    call->call.args[call->call.arg_count++] = parse_expr(p);
                } while (match(p, TOK_COMMA));
            }
            
            consume(p, TOK_RPAREN, "expected ')' after arguments");
            expr = call;
        } else if (match(p, TOK_LBRACKET)) {
            AstNode* index = new_node(AST_INDEX);
            index->index.array = expr;
            index->index.index = parse_expr(p);
            consume(p, TOK_RBRACKET, "expected ']' after index");
            expr = index;
        } else if (match(p, TOK_DOT)) {
            consume(p, TOK_IDENT, "expected method name after '.'");
            
            char* method = malloc(p->previous.length + 1);
            memcpy(method, p->previous.start, p->previous.length);
            method[p->previous.length] = '\0';
            
            if (strcmp(method, "iter") == 0) {
                consume(p, TOK_LPAREN, "expected '(' after 'iter'");
                consume(p, TOK_RPAREN, "expected ')' after 'iter'");
                
                AstNode* chain = new_node(AST_ITER_CHAIN);
                chain->iter_chain.source = expr;
                chain->iter_chain.methods = NULL;
                chain->iter_chain.closures = NULL;
                chain->iter_chain.method_count = 0;
                
                while (match(p, TOK_DOT)) {
                    consume(p, TOK_IDENT, "expected method name");
                    
                    size_t idx = chain->iter_chain.method_count++;
                    chain->iter_chain.methods = realloc(chain->iter_chain.methods,
                                                       sizeof(char*) * chain->iter_chain.method_count);
                    chain->iter_chain.closures = realloc(chain->iter_chain.closures,
                                                        sizeof(AstNode*) * chain->iter_chain.method_count);
                    
                    chain->iter_chain.methods[idx] = malloc(p->previous.length + 1);
                    memcpy(chain->iter_chain.methods[idx], p->previous.start, p->previous.length);
                    chain->iter_chain.methods[idx][p->previous.length] = '\0';
                    
                    consume(p, TOK_LPAREN, "expected '(' after method");
                    
                    if (check(p, TOK_PIPE)) {
                        chain->iter_chain.closures[idx] = parse_primary(p);
                    } else {
                        chain->iter_chain.closures[idx] = parse_expr(p);
                    }
                    
                    if (strcmp(chain->iter_chain.methods[idx], "fold") == 0) {
                        consume(p, TOK_COMMA, "expected second argument to fold");
                        parse_primary(p); 
                    }
                    
                    consume(p, TOK_RPAREN, "expected ')' after arguments");
                }
                
                free(method);
                expr = chain;
            } else {
                free(method);
                error(p, "unknown method");
            }
        } else {
            break;
        }
    }
    
    return expr;
}

// parse unary
static AstNode* parse_unary(Parser* p) {
    if (match(p, TOK_MINUS)) {
        AstNode* node = new_node(AST_UNARY);
        node->unary.op = "-";
        node->unary.operand = parse_unary(p);
        return node;
    }
    
    return parse_postfix(p);
}

// parse binary with precedence
static AstNode* parse_binary(Parser* p, Precedence prec) {
    if (prec >= PREC_UNARY) return parse_unary(p);
    
    AstNode* left = parse_binary(p, prec + 1);
    
    for (;;) {
        char* op = NULL;
        Precedence next_prec = PREC_NONE;
        
        if (prec <= PREC_FACTOR && (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT))) {
            advance(p);
            if (p->previous.type == TOK_STAR) op = "*";
            else if (p->previous.type == TOK_SLASH) op = "/";
            else op = "%";
            next_prec = PREC_FACTOR;
        } else if (prec <= PREC_TERM && (check(p, TOK_PLUS) || check(p, TOK_MINUS))) {
            advance(p);
            op = (p->previous.type == TOK_PLUS) ? "+" : "-";
            next_prec = PREC_TERM;
        } else if (prec <= PREC_COMPARISON && (check(p, TOK_LT) || check(p, TOK_LE) || 
                                               check(p, TOK_GT) || check(p, TOK_GE))) {
            advance(p);
            if (p->previous.type == TOK_LT) op = "<";
            else if (p->previous.type == TOK_LE) op = "<=";
            else if (p->previous.type == TOK_GT) op = ">";
            else op = ">=";
            next_prec = PREC_COMPARISON;
        } else if (prec <= PREC_EQUALITY && (check(p, TOK_EQ) || check(p, TOK_NE))) {
            advance(p);
            op = (p->previous.type == TOK_EQ) ? "==" : "!=";
            next_prec = PREC_EQUALITY;
        } else {
            break;
        }
        
        AstNode* node = new_node(AST_BINARY);
        node->binary.op = op;
        node->binary.left = left;
        node->binary.right = parse_binary(p, next_prec + 1);
        left = node;
    }
    
    return left;
}

// parse expression
static AstNode* parse_expr(Parser* p) {
    return parse_binary(p, PREC_ASSIGNMENT);
}

// parse block
static AstNode* parse_block(Parser* p) {
    AstNode* node = new_node(AST_BLOCK);
    node->block.stmts = NULL;
    node->block.count = 0;
    size_t capacity = 8;
    node->block.stmts = malloc(sizeof(AstNode*) * capacity);
    
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (node->block.count >= capacity) {
            capacity *= 2;
            node->block.stmts = realloc(node->block.stmts, sizeof(AstNode*) * capacity);
        }
        node->block.stmts[node->block.count++] = parse_stmt(p);
    }
    
    consume(p, TOK_RBRACE, "expected '}' after block");
    return node;
}

// parse statement
static AstNode* parse_stmt(Parser* p) {
    if (match(p, TOK_LET)) {
        AstNode* node = new_node(AST_LET);
        node->let.is_mut = match(p, TOK_MUT);
        
        consume(p, TOK_IDENT, "expected variable name");
        node->let.name = malloc(p->previous.length + 1);
        memcpy(node->let.name, p->previous.start, p->previous.length);
        node->let.name[p->previous.length] = '\0';
        
        consume(p, TOK_ASSIGN, "expected '=' after variable name");
        node->let.init = parse_expr(p);
        
        return node;
    }

    if (match(p, TOK_LBRACE)) {
        return parse_block(p);
    }
    
    if (match(p, TOK_FN)) {
        AstNode* node = new_node(AST_FN);
        
        consume(p, TOK_IDENT, "expected function name");
        node->fn.name = malloc(p->previous.length + 1);
        memcpy(node->fn.name, p->previous.start, p->previous.length);
        node->fn.name[p->previous.length] = '\0';
        
        consume(p, TOK_LPAREN, "expected '(' after function name");
        
        node->fn.params = NULL;
        node->fn.param_count = 0;
        
        if (!check(p, TOK_RPAREN)) {
            size_t capacity = 4;
            node->fn.params = malloc(sizeof(char*) * capacity);
            
            do {
                consume(p, TOK_IDENT, "expected parameter name");
                if (node->fn.param_count >= capacity) {
                    capacity *= 2;
                    node->fn.params = realloc(node->fn.params, sizeof(char*) * capacity);
                }
                char* param = malloc(p->previous.length + 1);
                memcpy(param, p->previous.start, p->previous.length);
                param[p->previous.length] = '\0';
                node->fn.params[node->fn.param_count++] = param;
            } while (match(p, TOK_COMMA));
        }
        
        consume(p, TOK_RPAREN, "expected ')' after parameters");
        consume(p, TOK_LBRACE, "expected '{' before function body");
        
        node->fn.body = parse_block(p);
        return node;
    }
    
    if (match(p, TOK_IF)) {
        AstNode* node = new_node(AST_IF);
        node->if_stmt.cond = parse_expr(p);
        consume(p, TOK_LBRACE, "expected '{' after if condition");
        node->if_stmt.then_branch = parse_block(p);
        
        if (match(p, TOK_ELSE)) {
            consume(p, TOK_LBRACE, "expected '{' after else");
            node->if_stmt.else_branch = parse_block(p);
        } else {
            node->if_stmt.else_branch = NULL;
        }
        
        return node;
    }
    
    if (match(p, TOK_WHILE)) {
        AstNode* node = new_node(AST_WHILE);
        node->while_stmt.cond = parse_expr(p);
        consume(p, TOK_LBRACE, "expected '{' after while condition");
        node->while_stmt.body = parse_block(p);
        return node;
    }
    
    if (match(p, TOK_FOR)) {
        AstNode* node = new_node(AST_FOR);
        
        consume(p, TOK_IDENT, "expected variable name");
        node->for_stmt.var = malloc(p->previous.length + 1);
        memcpy(node->for_stmt.var, p->previous.start, p->previous.length);
        node->for_stmt.var[p->previous.length] = '\0';
        
        consume(p, TOK_IN, "expected 'in' after for variable");
        node->for_stmt.iterable = parse_expr(p);
        consume(p, TOK_LBRACE, "expected '{' after for iterable");
        node->for_stmt.body = parse_block(p);
        
        return node;
    }
    
    if (match(p, TOK_LOOP)) {
        AstNode* node = new_node(AST_LOOP);
        consume(p, TOK_LBRACE, "expected '{' after loop");
        node->loop_stmt.body = parse_block(p);
        return node;
    }
    
    if (match(p, TOK_BREAK)) {
        return new_node(AST_BREAK);
    }
    
    if (match(p, TOK_RETURN)) {
        AstNode* node = new_node(AST_RETURN);
        if (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            node->return_stmt.value = parse_expr(p);
        } else {
            node->return_stmt.value = new_node(AST_NONE);
        }
        return node;
    }
    
    AstNode* expr = parse_expr(p);
    
    if (match(p, TOK_ASSIGN)) {
        if (expr->type != AST_IDENT && expr->type != AST_INDEX) {
            error(p, "invalid assignment target");
            return expr;
        }
        
        AstNode* node = new_node(AST_ASSIGN);
        if (expr->type == AST_IDENT) {
            node->assign.name = expr->str_val;
            node->assign.target = NULL;
            free(expr);
        } else {
            node->assign.name = NULL;
            node->assign.target = expr;
        }
        node->assign.value = parse_expr(p);
        return node;
    }
    
    return expr;
}

// public parse function
AstNode* parse(const char* source) {
    Lexer lexer;
    lexer_init(&lexer, source);
    
    Parser parser;
    parser.lexer = &lexer;
    parser.had_error = false;
    parser.panic_mode = false;
    
    advance(&parser);
    
    AstNode* program = new_node(AST_BLOCK);
    program->block.stmts = NULL;
    program->block.count = 0;
    size_t capacity = 16;
    program->block.stmts = malloc(sizeof(AstNode*) * capacity);
    
    while (!match(&parser, TOK_EOF)) {
        if (program->block.count >= capacity) {
            capacity *= 2;
            program->block.stmts = realloc(program->block.stmts, 
                                          sizeof(AstNode*) * capacity);
        }
        program->block.stmts[program->block.count++] = parse_stmt(&parser);
        
        if (parser.panic_mode) {
            while (parser.current.type != TOK_EOF) {
                if (parser.previous.type == TOK_SEMICOLON) break;
                
                switch (parser.current.type) {
                    case TOK_FN:
                    case TOK_LET:
                    case TOK_IF:
                    case TOK_WHILE:
                    case TOK_FOR:
                    case TOK_RETURN:
                        parser.panic_mode = false;
                        return program;
                    default:
                        break;
                }
                
                advance(&parser);
            }
        }
    }
    
    if (parser.had_error) {
        return NULL;
    }
    
    return program;
}

// free ast
void ast_free(AstNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_STRING:
        case AST_IDENT:
            free(node->str_val);
            break;
        case AST_ARRAY:
            for (size_t i = 0; i < node->array.count; i++) {
                ast_free(node->array.elements[i]);
            }
            free(node->array.elements);
            break;
        case AST_BINARY:
            ast_free(node->binary.left);
            ast_free(node->binary.right);
            break;
        case AST_UNARY:
            ast_free(node->unary.operand);
            break;
        case AST_CALL:
            ast_free(node->call.callee);
            for (size_t i = 0; i < node->call.arg_count; i++) {
                ast_free(node->call.args[i]);
            }
            free(node->call.args);
            break;
        case AST_INDEX:
            ast_free(node->index.array);
            ast_free(node->index.index);
            break;
        case AST_LET:
            free(node->let.name);
            ast_free(node->let.init);
            break;
        case AST_ASSIGN:
            free(node->assign.name);
            ast_free(node->assign.target);
            ast_free(node->assign.value);
            break;
        case AST_BLOCK:
            for (size_t i = 0; i < node->block.count; i++) {
                ast_free(node->block.stmts[i]);
            }
            free(node->block.stmts);
            break;
        case AST_IF:
            ast_free(node->if_stmt.cond);
            ast_free(node->if_stmt.then_branch);
            ast_free(node->if_stmt.else_branch);
            break;
        case AST_WHILE:
            ast_free(node->while_stmt.cond);
            ast_free(node->while_stmt.body);
            break;
        case AST_FOR:
            free(node->for_stmt.var);
            ast_free(node->for_stmt.iterable);
            ast_free(node->for_stmt.body);
            break;
        case AST_LOOP:
            ast_free(node->loop_stmt.body);
            break;
        case AST_RETURN:
            ast_free(node->return_stmt.value);
            break;
        case AST_FN:
            free(node->fn.name);
            for (size_t i = 0; i < node->fn.param_count; i++) {
                free(node->fn.params[i]);
            }
            free(node->fn.params);
            ast_free(node->fn.body);
            break;
        case AST_CLOSURE:
            for (size_t i = 0; i < node->closure.param_count; i++) {
                free(node->closure.params[i]);
            }
            free(node->closure.params);
            ast_free(node->closure.body);
            break;
        default:
            break;
    }
    
    free(node);
}