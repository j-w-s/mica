#define _POSIX_C_SOURCE 200809L
#include "mica_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// local variable
typedef struct {
    char* name;
    int depth;
    bool is_captured;
    bool is_mut;
} Local;

// break patch entry
typedef struct {
    size_t address;
} BreakPatch;

// loop context for break statements
typedef struct LoopContext {
    struct LoopContext* enclosing;
    BreakPatch* patches;
    size_t patch_count;
    size_t patch_capacity;
} LoopContext;

// compiler state
typedef struct Compiler {
    struct Compiler* enclosing;
    FunctionProto* function;
    Local locals[256];
    int local_count;
    int scope_depth;
    int register_count;
    LoopContext* loop;
} Compiler;

// init compiler
static void compiler_init(Compiler* c, Compiler* enclosing, const char* name) {
    c->enclosing = enclosing;
    c->local_count = 0;
    c->scope_depth = 0;
    c->register_count = 0;
    c->loop = NULL;
    
    c->function = malloc(sizeof(FunctionProto));
    c->function->code = NULL;
    c->function->code_count = 0;
    c->function->code_capacity = 0;
    c->function->constants = NULL;
    c->function->const_count = 0;
    c->function->const_capacity = 0;
    c->function->arity = 0;
    c->function->upvalue_count = 0;
    c->function->upvalue_descs = NULL;
    c->function->name = name ? strdup(name) : NULL;
}

// emit byte
static void emit_byte(Compiler* c, uint8_t byte) {
    if (c->function->code_capacity < c->function->code_count + 1) {
        size_t old_cap = c->function->code_capacity;
        c->function->code_capacity = old_cap < 8 ? 8 : old_cap * 2;
        c->function->code = realloc(c->function->code, c->function->code_capacity);
    }
    c->function->code[c->function->code_count++] = byte;
}

// emit opcode
static void emit_op(Compiler* c, Opcode op) {
    emit_byte(c, op);
}

// add constant
static uint8_t add_constant(Compiler* c, Value val) {
    if (c->function->const_capacity < c->function->const_count + 1) {
        size_t old_cap = c->function->const_capacity;
        c->function->const_capacity = old_cap < 8 ? 8 : old_cap * 2;
        c->function->constants = realloc(c->function->constants,
                                        c->function->const_capacity * sizeof(Value));
    }
    c->function->constants[c->function->const_count] = val;
    return c->function->const_count++;
}

// allocate register
static uint8_t alloc_register(Compiler* c) {
    return c->register_count++;
}

// free register
static void free_register(Compiler* c) {
    if (c->register_count > 0) {
        c->register_count--;
    }
}

// loop context management
static void begin_loop(Compiler* c) {
    LoopContext* loop = malloc(sizeof(LoopContext));
    loop->enclosing = c->loop;
    loop->patches = NULL;
    loop->patch_count = 0;
    loop->patch_capacity = 0;
    c->loop = loop;
}

static void end_loop(Compiler* c) {
    if (!c->loop) return;
    
    for (size_t i = 0; i < c->loop->patch_count; i++) {
        size_t patch_addr = c->loop->patches[i].address;
        int16_t offset = (int16_t)(c->function->code_count - (patch_addr + 2));
        c->function->code[patch_addr] = (offset >> 8) & 0xFF;
        c->function->code[patch_addr + 1] = offset & 0xFF;
    }
    
    LoopContext* old = c->loop;
    c->loop = c->loop->enclosing;
    free(old->patches);
    free(old);
}

// scope management
static void begin_scope(Compiler* c) {
    c->scope_depth++;
}

static void end_scope(Compiler* c) {
    c->scope_depth--;
    
    while (c->local_count > 0 &&
           c->locals[c->local_count - 1].depth > c->scope_depth) {
        if (c->locals[c->local_count - 1].is_captured) {
            emit_op(c, OP_CLOSE_UPVAL);
            emit_byte(c, c->local_count - 1);
        }
        free(c->locals[c->local_count - 1].name);
        c->local_count--;
    }
}

// find local variable
static int resolve_local(Compiler* c, const char* name) {
    for (int i = c->local_count - 1; i >= 0; i--) {
        if (strcmp(c->locals[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// add upvalue
static int add_upvalue(Compiler* c, uint8_t index, bool is_local) {
    // check if upvalue already exists
    for (int i = 0; i < c->function->upvalue_count; i++) {
        if (c->function->upvalue_descs[i].index == index && 
            c->function->upvalue_descs[i].is_local == is_local) {
            return i;
        }
    }
    
    if (c->function->upvalue_count >= 256) {
        fprintf(stderr, "too many upvalues\n");
        return -1;
    }
    
    if (c->function->upvalue_descs == NULL) {
        c->function->upvalue_descs = malloc(256 * sizeof(UpvalueDesc));
    }
    
    c->function->upvalue_descs[c->function->upvalue_count].index = index;
    c->function->upvalue_descs[c->function->upvalue_count].is_local = is_local;
    return c->function->upvalue_count++;
}

// resolve upvalue
static int resolve_upvalue(Compiler* c, const char* name) {
    if (c->enclosing == NULL) return -1;
    
    int local = resolve_local(c->enclosing, name);
    if (local != -1) {
        c->enclosing->locals[local].is_captured = true;
        return add_upvalue(c, local, true);
    }
    
    int upvalue = resolve_upvalue(c->enclosing, name);
    if (upvalue != -1) {
        return add_upvalue(c, upvalue, false);
    }
    
    return -1;
}

// declare local
static void declare_local(Compiler* c, const char* name, bool is_mut) {
    if (c->local_count >= 256) {
        fprintf(stderr, "too many local variables\n");
        return;
    }
    
    Local* local = &c->locals[c->local_count++];
    local->name = strdup(name);
    local->depth = c->scope_depth;
    local->is_captured = false;
    local->is_mut = is_mut;
}

// forward declaration
static uint8_t compile_expr(Compiler* c, AstNode* node);
static void compile_stmt(Compiler* c, AstNode* node);

// compile literal
static uint8_t compile_literal(Compiler* c, AstNode* node) {
    uint8_t reg = alloc_register(c);
    Value val;
    
    switch (node->type) {
        case AST_INT:
            val = value_i32(node->int_val);
            break;
        case AST_FLOAT:
            val = value_f32(node->float_val);
            break;
        case AST_BOOL:
            val = value_bool(node->bool_val);
            break;
        case AST_STRING:
            val = value_string(node->str_val);
            break;
        case AST_NONE:
            val = value_none();
            break;
        default:
            val = value_none();
    }
    
    uint8_t const_idx = add_constant(c, val);
    emit_op(c, OP_LOAD_CONST);
    emit_byte(c, const_idx);
    emit_byte(c, reg);
    
    return reg;
}

// compile identifier
static uint8_t compile_ident(Compiler* c, AstNode* node) {
    uint8_t reg = alloc_register(c);
    
    int local = resolve_local(c, node->str_val);
    if (local != -1) {
        emit_op(c, OP_LOAD_LOCAL);
        emit_byte(c, local);
        emit_byte(c, reg);
        return reg;
    }
    
    int upvalue = resolve_upvalue(c, node->str_val);
    if (upvalue != -1) {
        emit_op(c, OP_LOAD_UPVAL);
        emit_byte(c, upvalue);
        emit_byte(c, reg);
        return reg;
    }
    
    Value name_val = value_string(node->str_val);
    uint8_t name_idx = add_constant(c, name_val);
    emit_op(c, OP_LOAD_GLOBAL);
    emit_byte(c, name_idx);
    emit_byte(c, reg);
    
    return reg;
}

// compile binary op
static uint8_t compile_binary(Compiler* c, AstNode* node) {
    uint8_t left = compile_expr(c, node->binary.left);
    uint8_t right = compile_expr(c, node->binary.right);
    uint8_t dest = alloc_register(c);
    
    if (strcmp(node->binary.op, "+") == 0) emit_op(c, OP_ADD);
    else if (strcmp(node->binary.op, "-") == 0) emit_op(c, OP_SUB);
    else if (strcmp(node->binary.op, "*") == 0) emit_op(c, OP_MUL);
    else if (strcmp(node->binary.op, "/") == 0) emit_op(c, OP_DIV);
    else if (strcmp(node->binary.op, "%") == 0) emit_op(c, OP_MOD);
    else if (strcmp(node->binary.op, "==") == 0) emit_op(c, OP_EQ);
    else if (strcmp(node->binary.op, "!=") == 0) emit_op(c, OP_NE);
    else if (strcmp(node->binary.op, "<") == 0) emit_op(c, OP_LT);
    else if (strcmp(node->binary.op, "<=") == 0) emit_op(c, OP_LE);
    else if (strcmp(node->binary.op, ">") == 0) emit_op(c, OP_GT);
    else if (strcmp(node->binary.op, ">=") == 0) emit_op(c, OP_GE);
    
    emit_byte(c, left);
    emit_byte(c, right);
    emit_byte(c, dest);
    
    free_register(c);
    free_register(c);
    
    return dest;
}

// compile unary op
static uint8_t compile_unary(Compiler* c, AstNode* node) {
    uint8_t operand = compile_expr(c, node->unary.operand);
    uint8_t dest = alloc_register(c);
    
    if (strcmp(node->unary.op, "-") == 0) {
        emit_op(c, OP_NEG);
        emit_byte(c, operand);
        emit_byte(c, dest);
    }
    
    free_register(c);
    return dest;
}

// compile array literal
static uint8_t compile_array(Compiler* c, AstNode* node) {
    uint8_t arr_reg = alloc_register(c);
    
    emit_op(c, OP_ARRAY_NEW);
    emit_byte(c, node->array.count);
    emit_byte(c, arr_reg);
    
    for (size_t i = 0; i < node->array.count; i++) {
        uint8_t elem = compile_expr(c, node->array.elements[i]);
        emit_op(c, OP_ARRAY_PUSH);
        emit_byte(c, arr_reg);
        emit_byte(c, elem);
        free_register(c);
    }
    
    return arr_reg;
}

// compile array index
static uint8_t compile_index(Compiler* c, AstNode* node) {
    uint8_t arr_reg = compile_expr(c, node->index.array);
    uint8_t idx_reg = compile_expr(c, node->index.index);
    uint8_t dest = alloc_register(c);
    
    emit_op(c, OP_ARRAY_GET);
    emit_byte(c, arr_reg);
    emit_byte(c, idx_reg);
    emit_byte(c, dest);
    
    free_register(c);
    free_register(c);
    
    return dest;
}

// compile call
static uint8_t compile_call(Compiler* c, AstNode* node) {
    uint8_t func = compile_expr(c, node->call.callee);
    
    uint8_t* arg_regs = malloc(sizeof(uint8_t) * node->call.arg_count);
    for (size_t i = 0; i < node->call.arg_count; i++) {
        arg_regs[i] = compile_expr(c, node->call.args[i]);
    }
    
    uint8_t arg_start = func + 1;
    for (size_t i = 0; i < node->call.arg_count; i++) {
        uint8_t target_reg = arg_start + i;
        if (arg_regs[i] != target_reg) {
            emit_op(c, OP_MOVE);
            emit_byte(c, arg_regs[i]);
            emit_byte(c, target_reg);
        }
    }
    
    free(arg_regs);
    
    uint8_t dest = alloc_register(c);
    
    emit_op(c, OP_CALL);
    emit_byte(c, func);
    emit_byte(c, node->call.arg_count);
    emit_byte(c, dest);
    
    return dest;
}

// compile closure
static uint8_t compile_closure(Compiler* c, AstNode* node) {
    Compiler closure_compiler;
    compiler_init(&closure_compiler, c, NULL);
    
    closure_compiler.function->arity = node->closure.param_count;
    
    begin_scope(&closure_compiler);
    for (size_t i = 0; i < node->closure.param_count; i++) {
        declare_local(&closure_compiler, node->closure.params[i], false);
    }
    // parameters occupy register slots 0, 1, 2, ...
    closure_compiler.register_count = closure_compiler.local_count;

    if (node->closure.body->type == AST_BLOCK) {
        compile_stmt(&closure_compiler, node->closure.body);
    } else {
        uint8_t expr_reg = compile_expr(&closure_compiler, node->closure.body);
        emit_op(&closure_compiler, OP_RET);
        emit_byte(&closure_compiler, 1);
        emit_byte(&closure_compiler, expr_reg);
        free_register(&closure_compiler);
    }
    
    emit_op(&closure_compiler, OP_RET);
    emit_byte(&closure_compiler, 0);
    
    end_scope(&closure_compiler);
    
    Value closure_val;
    memset(&closure_val, 0, sizeof(Value));
    closure_val.type = VAL_CLOSURE;
    closure_val.as_ptr = closure_compiler.function;
    
    uint8_t const_idx = add_constant(c, closure_val);
    uint8_t reg = alloc_register(c);
    
    emit_op(c, OP_CLOSURE);
    emit_byte(c, const_idx);
    emit_byte(c, reg);
    emit_byte(c, closure_compiler.function->upvalue_count);
    
    for (int i = 0; i < closure_compiler.function->upvalue_count; i++) {
        emit_byte(c, closure_compiler.function->upvalue_descs[i].is_local ? 1 : 0);
        emit_byte(c, closure_compiler.function->upvalue_descs[i].index);
    }
    
    return reg;
}

// compile iterator chain
static uint8_t compile_iter_chain(Compiler* c, AstNode* node) {
    return compile_expr(c, node->iter_chain.source);
}

// compile expression
static uint8_t compile_expr(Compiler* c, AstNode* node) {
    switch (node->type) {
        case AST_INT:
        case AST_FLOAT:
        case AST_BOOL:
        case AST_STRING:
        case AST_NONE:
            return compile_literal(c, node);
        case AST_IDENT:
            return compile_ident(c, node);
        case AST_BINARY:
            return compile_binary(c, node);
        case AST_UNARY:
            return compile_unary(c, node);
        case AST_ARRAY:
            return compile_array(c, node);
        case AST_INDEX:
            return compile_index(c, node);
        case AST_CALL:
            return compile_call(c, node);
        case AST_CLOSURE:
            return compile_closure(c, node);
        case AST_ITER_CHAIN:
            return compile_iter_chain(c, node);
        default:
            return alloc_register(c);
    }
}

// compile let - allocate local in register slot
static void compile_let(Compiler* c, AstNode* node) {
    if (c->scope_depth == 0) {
        // global variable
        uint8_t val_reg = compile_expr(c, node->let.init);
        Value name_val = value_string(node->let.name);
        uint8_t name_idx = add_constant(c, name_val);
        emit_op(c, OP_STORE_GLOBAL);
        emit_byte(c, name_idx);
        emit_byte(c, val_reg);
        free_register(c);
    } else {
        // local variable - must occupy the next sequential register slot
        int local_slot = c->local_count;
        
        // declare the local
        declare_local(c, node->let.name, node->let.is_mut);
        
        // compile the initializer
        uint8_t val_reg = compile_expr(c, node->let.init);
        
        // the local MUST be in register slot equal to its index
        // so we need to move the value there if it's not already
        if (val_reg != (uint8_t)local_slot) {
            emit_op(c, OP_MOVE);
            emit_byte(c, val_reg);
            emit_byte(c, local_slot);
        }
        
        // store to local slot
        emit_op(c, OP_STORE_LOCAL);
        emit_byte(c, local_slot);
        emit_byte(c, local_slot);
        
        // ensure register_count accounts for this local
        if (c->register_count <= local_slot) {
            c->register_count = local_slot + 1;
        }
        
        // free the temporary register if we allocated one
        while (c->register_count > c->local_count) {
            free_register(c);
        }
    }
}

// compile assign
static void compile_assign(Compiler* c, AstNode* node) {
    if (node->assign.target) {
        if (node->assign.target->type == AST_INDEX) {
            uint8_t arr_reg = compile_expr(c, node->assign.target->index.array);
            uint8_t idx_reg = compile_expr(c, node->assign.target->index.index);
            uint8_t val_reg = compile_expr(c, node->assign.value);
            
            emit_op(c, OP_ARRAY_SET);
            emit_byte(c, arr_reg);
            emit_byte(c, idx_reg);
            emit_byte(c, val_reg);
            
            free_register(c);
            free_register(c);
            free_register(c);
            return;
        }
    }
    
    uint8_t val_reg = compile_expr(c, node->assign.value);
    
    int local = resolve_local(c, node->assign.name);
    if (local != -1) {
        if (!c->locals[local].is_mut) {
            fprintf(stderr, "cannot assign to immutable variable: %s\n", 
                   node->assign.name);
        }
        emit_op(c, OP_STORE_LOCAL);
        emit_byte(c, local);
        emit_byte(c, val_reg);
    } else {
        int upvalue = resolve_upvalue(c, node->assign.name);
        if (upvalue != -1) {
            emit_op(c, OP_STORE_UPVAL);
            emit_byte(c, upvalue);
            emit_byte(c, val_reg);
        } else {
            Value name_val = value_string(node->assign.name);
            uint8_t name_idx = add_constant(c, name_val);
            emit_op(c, OP_STORE_GLOBAL);
            emit_byte(c, name_idx);
            emit_byte(c, val_reg);
        }
    }
    
    free_register(c);
}

// compile if
static void compile_if(Compiler* c, AstNode* node) {
    uint8_t cond_reg = compile_expr(c, node->if_stmt.cond);
    
    emit_op(c, OP_JMP_IF_NOT);
    emit_byte(c, cond_reg);
    size_t else_jmp = c->function->code_count;
    emit_byte(c, 0);
    emit_byte(c, 0);
    
    free_register(c);
    
    compile_stmt(c, node->if_stmt.then_branch);
    
    if (node->if_stmt.else_branch) {
        emit_op(c, OP_JMP);
        size_t end_jmp = c->function->code_count;
        emit_byte(c, 0);
        emit_byte(c, 0);
        
        int16_t else_offset = c->function->code_count - else_jmp - 2;
        c->function->code[else_jmp] = (else_offset >> 8) & 0xFF;
        c->function->code[else_jmp + 1] = else_offset & 0xFF;
        
        compile_stmt(c, node->if_stmt.else_branch);
        
        int16_t end_offset = c->function->code_count - end_jmp - 2;
        c->function->code[end_jmp] = (end_offset >> 8) & 0xFF;
        c->function->code[end_jmp + 1] = end_offset & 0xFF;
    } else {
        int16_t else_offset = c->function->code_count - else_jmp - 2;
        c->function->code[else_jmp] = (else_offset >> 8) & 0xFF;
        c->function->code[else_jmp + 1] = else_offset & 0xFF;
    }
}

// compile while
static void compile_while(Compiler* c, AstNode* node) {
    begin_loop(c);
    
    size_t loop_start = c->function->code_count;
    
    uint8_t cond_reg = compile_expr(c, node->while_stmt.cond);
    
    emit_op(c, OP_JMP_IF_NOT);
    emit_byte(c, cond_reg);
    size_t exit_jmp = c->function->code_count;
    emit_byte(c, 0);
    emit_byte(c, 0);
    
    free_register(c);
    
    compile_stmt(c, node->while_stmt.body);
    
    emit_op(c, OP_JMP);
    int16_t offset = -(int16_t)(c->function->code_count - loop_start + 2);
    emit_byte(c, (offset >> 8) & 0xFF);
    emit_byte(c, offset & 0xFF);
    
    int16_t exit_offset = c->function->code_count - exit_jmp - 2;
    c->function->code[exit_jmp] = (exit_offset >> 8) & 0xFF;
    c->function->code[exit_jmp + 1] = exit_offset & 0xFF;
    
    end_loop(c);
}

// compile for loop
static void compile_for(Compiler* c, AstNode* node) {
    begin_loop(c);
    begin_scope(c);
    
    uint8_t iterable_reg = compile_expr(c, node->for_stmt.iterable);
    
    uint8_t iter_reg = alloc_register(c);
    emit_op(c, OP_ITER_NEW);
    emit_byte(c, iterable_reg);
    emit_byte(c, iter_reg);
    free_register(c);
    
    declare_local(c, ".iter", false);
    int iter_local = c->local_count - 1;
    emit_op(c, OP_STORE_LOCAL);
    emit_byte(c, iter_local);
    emit_byte(c, iter_reg);
    free_register(c);
    
    declare_local(c, node->for_stmt.var, false);
    int var_local = c->local_count - 1;
    
    size_t loop_start = c->function->code_count;
    
    uint8_t loaded_iter_reg = alloc_register(c);
    emit_op(c, OP_LOAD_LOCAL);
    emit_byte(c, iter_local);
    emit_byte(c, loaded_iter_reg);
    
    uint8_t has_next_reg = alloc_register(c);
    emit_op(c, OP_ITER_HAS_NEXT);
    emit_byte(c, loaded_iter_reg);
    emit_byte(c, has_next_reg);
    
    emit_op(c, OP_JMP_IF_NOT);
    emit_byte(c, has_next_reg);
    size_t exit_jmp = c->function->code_count;
    emit_byte(c, 0);
    emit_byte(c, 0);
    
    free_register(c);
    
    uint8_t val_reg = alloc_register(c);
    emit_op(c, OP_ITER_NEXT);
    emit_byte(c, loaded_iter_reg);
    emit_byte(c, val_reg);
    
    free_register(c);
    
    emit_op(c, OP_STORE_LOCAL);
    emit_byte(c, var_local);
    emit_byte(c, val_reg);
    
    free_register(c);
    
    compile_stmt(c, node->for_stmt.body);
    
    emit_op(c, OP_JMP);
    int16_t offset = -(int16_t)(c->function->code_count - loop_start + 2);
    emit_byte(c, (offset >> 8) & 0xFF);
    emit_byte(c, offset & 0xFF);
    
    int16_t exit_offset = c->function->code_count - exit_jmp - 2;
    c->function->code[exit_jmp] = (exit_offset >> 8) & 0xFF;
    c->function->code[exit_jmp + 1] = exit_offset & 0xFF;
    
    end_scope(c);
    end_loop(c);
}

// compile loop
static void compile_loop(Compiler* c, AstNode* node) {
    begin_loop(c);
    
    size_t loop_start = c->function->code_count;
    
    compile_stmt(c, node->loop_stmt.body);
    
    emit_op(c, OP_JMP);
    int16_t offset = -(int16_t)(c->function->code_count - loop_start + 2);
    emit_byte(c, (offset >> 8) & 0xFF);
    emit_byte(c, offset & 0xFF);
    
    end_loop(c);
}

// compile break
static void compile_break(Compiler* c) {
    if (!c->loop) {
        fprintf(stderr, "break outside of loop\n");
        return;
    }
    
    emit_op(c, OP_JMP);
    
    if (!c->loop) return;
    
    if (c->loop->patch_count >= c->loop->patch_capacity) {
        size_t old_cap = c->loop->patch_capacity;
        c->loop->patch_capacity = old_cap < 4 ? 4 : old_cap * 2;
        c->loop->patches = realloc(c->loop->patches,
                                   c->loop->patch_capacity * sizeof(BreakPatch));
    }
    
    c->loop->patches[c->loop->patch_count].address = c->function->code_count;
    c->loop->patch_count++;
    
    emit_byte(c, 0);
    emit_byte(c, 0);
}

// compile return
static void compile_return(Compiler* c, AstNode* node) {
    if (node->return_stmt.value && node->return_stmt.value->type != AST_NONE) {
        uint8_t val_reg = compile_expr(c, node->return_stmt.value);
        emit_op(c, OP_RET);
        emit_byte(c, 1);
        emit_byte(c, val_reg);
        free_register(c);
    } else {
        emit_op(c, OP_RET);
        emit_byte(c, 0);
    }
}

// compile block
static void compile_block(Compiler* c, AstNode* node) {
    begin_scope(c);
    for (size_t i = 0; i < node->block.count; i++) {
        compile_stmt(c, node->block.stmts[i]);
    }
    end_scope(c);
}

// compile function
static void compile_fn(Compiler* c, AstNode* node) {
    uint8_t fn_reg = alloc_register(c);
    
    if (c->scope_depth > 0) {
        declare_local(c, node->fn.name, false);
    }
    
    Compiler fn_compiler;
    compiler_init(&fn_compiler, c, node->fn.name);
    
    fn_compiler.function->arity = node->fn.param_count;
    
    begin_scope(&fn_compiler);
    for (size_t i = 0; i < node->fn.param_count; i++) {
        declare_local(&fn_compiler, node->fn.params[i], false);
    }
    fn_compiler.register_count = fn_compiler.local_count;
    
    compile_stmt(&fn_compiler, node->fn.body);
    emit_op(&fn_compiler, OP_RET);
    emit_byte(&fn_compiler, 0);
    
    end_scope(&fn_compiler);
    
    Value fn_val;
    memset(&fn_val, 0, sizeof(Value));
    fn_val.type = VAL_CLOSURE;
    fn_val.as_ptr = fn_compiler.function;
    
    uint8_t const_idx = add_constant(c, fn_val);
    
    emit_op(c, OP_CLOSURE);
    emit_byte(c, const_idx);
    emit_byte(c, fn_reg);
    emit_byte(c, fn_compiler.function->upvalue_count);
    
    for (int i = 0; i < fn_compiler.function->upvalue_count; i++) {
        emit_byte(c, fn_compiler.function->upvalue_descs[i].is_local ? 1 : 0);
        emit_byte(c, fn_compiler.function->upvalue_descs[i].index);
    }
    
    if (c->scope_depth == 0) {
        Value name_val = value_string(node->fn.name);
        uint8_t name_idx = add_constant(c, name_val);
        emit_op(c, OP_STORE_GLOBAL);
        emit_byte(c, name_idx);
        emit_byte(c, fn_reg);
    } else {
        int local_idx = c->local_count - 1;
        emit_op(c, OP_STORE_LOCAL);
        emit_byte(c, local_idx);
        emit_byte(c, fn_reg);
    }
    
    free_register(c);
}

// compile statement
static void compile_stmt(Compiler* c, AstNode* node) {
    switch (node->type) {
        case AST_LET:
            compile_let(c, node);
            break;
        case AST_ASSIGN:
            compile_assign(c, node);
            break;
        case AST_IF:
            compile_if(c, node);
            break;
        case AST_WHILE:
            compile_while(c, node);
            break;
        case AST_FOR:
            compile_for(c, node);
            break;
        case AST_LOOP:
            compile_loop(c, node);
            break;
        case AST_BREAK:
            compile_break(c);
            break;
        case AST_RETURN:
            compile_return(c, node);
            break;
        case AST_BLOCK:
            compile_block(c, node);
            break;
        case AST_FN:
            compile_fn(c, node);
            break;
        default: {
            uint8_t reg = compile_expr(c, node);
            free_register(c);
            (void)reg;
            break;
        }
    }
}

// public compile function
FunctionProto* compile(AstNode* ast) {
    Compiler compiler;
    compiler_init(&compiler, NULL, "<main>");
    
    if (ast && ast->type == AST_BLOCK) {
        for (size_t i = 0; i < ast->block.count; i++) {
            compile_stmt(&compiler, ast->block.stmts[i]);
        }
    }
    
    emit_op(&compiler, OP_RET);
    emit_byte(&compiler, 0);
    
    return compiler.function;
}

// free proto
void proto_free(FunctionProto* proto) {
    if (!proto) return;
    free(proto->code);
    free(proto->constants);
    free(proto->upvalue_descs);
    free(proto->name);
    free(proto);
}