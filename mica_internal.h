#ifndef MICA_INTERNAL_H
#define MICA_INTERNAL_H

#include "mica.h"

// token types
typedef enum {
    // literals
    TOK_INT, TOK_FLOAT, TOK_STRING, TOK_TRUE, TOK_FALSE, TOK_NONE,
    // identifiers
    TOK_IDENT,
    // keywords
    TOK_LET, TOK_MUT, TOK_FN, TOK_RETURN, TOK_IF, TOK_ELSE,
    TOK_WHILE, TOK_FOR, TOK_IN, TOK_LOOP, TOK_BREAK, TOK_MATCH,
    // operators
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_EQ, TOK_NE, TOK_LT, TOK_LE, TOK_GT, TOK_GE,
    TOK_ASSIGN, TOK_ARROW, TOK_PIPE,
    // delimiters
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_DOT, TOK_COLON, TOK_SEMICOLON,
    TOK_RARROW,
    // special
    TOK_EOF, TOK_ERROR
} TokenType;

// token
typedef struct {
    TokenType type;
    const char* start;
    size_t length;
    int line;
} Token;

// lexer state
typedef struct {
    const char* start;
    const char* current;
    int line;
} Lexer;

// ast node types
typedef enum {
    AST_INT, AST_FLOAT, AST_STRING, AST_BOOL, AST_NONE,
    AST_IDENT, AST_ARRAY, AST_BINARY, AST_UNARY, AST_CALL,
    AST_INDEX, AST_LET, AST_ASSIGN, AST_BLOCK, AST_IF,
    AST_WHILE, AST_FOR, AST_LOOP, AST_BREAK, AST_RETURN,
    AST_FN, AST_CLOSURE, AST_MATCH, AST_MATCH_ARM,
    AST_ITER_CHAIN
} AstType;

// ast node
typedef struct AstNode {
    AstType type;
    union {
        int32_t int_val;
        float float_val;
        bool bool_val;
        char* str_val;
        struct {
            struct AstNode** elements;
            size_t count;
        } array;
        struct {
            char* op;
            struct AstNode* left;
            struct AstNode* right;
        } binary;
        struct {
            char* op;
            struct AstNode* operand;
        } unary;
        struct {
            struct AstNode* callee;
            struct AstNode** args;
            size_t arg_count;
        } call;
        struct {
            struct AstNode* array;
            struct AstNode* index;
        } index;
        struct {
            char* name;
            bool is_mut;
            struct AstNode* init;
        } let;
        struct {
            char* name;
            struct AstNode* target;
            struct AstNode* value;
        } assign;
        struct {
            struct AstNode** stmts;
            size_t count;
        } block;
        struct {
            struct AstNode* cond;
            struct AstNode* then_branch;
            struct AstNode* else_branch;
        } if_stmt;
        struct {
            struct AstNode* cond;
            struct AstNode* body;
        } while_stmt;
        struct {
            char* var;
            struct AstNode* iterable;
            struct AstNode* body;
        } for_stmt;
        struct {
            struct AstNode* body;
        } loop_stmt;
        struct {
            struct AstNode* value;
        } return_stmt;
        struct {
            char* name;
            char** params;
            size_t param_count;
            struct AstNode* body;
        } fn;
        struct {
            char** params;
            size_t param_count;
            struct AstNode* body;
        } closure;
        struct {
            struct AstNode* value;
            struct AstNode** arms;
            size_t arm_count;
        } match;
        struct {
            char* pattern;
            struct AstNode* body;
        } match_arm;
        struct {
            struct AstNode* source;
            char** methods;
            struct AstNode** closures;
            size_t method_count;
        } iter_chain;
    };
} AstNode;

// upvalue descriptor
typedef struct {
    uint8_t index;
    bool is_local;
} UpvalueDesc;

// opcodes
typedef enum {
    OP_NOP,
    OP_LOAD_CONST, OP_LOAD_LOCAL, OP_STORE_LOCAL,
    OP_MOVE, OP_LOAD_UPVAL, OP_STORE_UPVAL, OP_LOAD_GLOBAL, OP_STORE_GLOBAL,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_JMP, OP_JMP_IF, OP_JMP_IF_NOT, OP_RET,
    OP_CALL, OP_CLOSURE, OP_CLOSE_UPVAL, OP_CALL_NATIVE,
    OP_ARRAY_NEW, OP_ARRAY_GET, OP_ARRAY_SET, OP_ARRAY_LEN, OP_ARRAY_PUSH,
    OP_ITER_NEW, OP_ITER_NEXT, OP_ITER_HAS_NEXT
} Opcode;

// function prototype
typedef struct FunctionProto {
    uint8_t* code;
    size_t code_count;
    size_t code_capacity;
    Value* constants;
    size_t const_count;
    size_t const_capacity;
    int arity;
    int upvalue_count;
    UpvalueDesc* upvalue_descs; 
    char* name;
} FunctionProto;

// lexer functions
void lexer_init(Lexer* lex, const char* source);
Token next_token(Lexer* lex);

// parser functions
AstNode* parse(const char* source);

// compiler functions
FunctionProto* compile(AstNode* ast);

#endif // MICA_INTERNAL_H