#define _POSIX_C_SOURCE 200809L
#include "mica_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_REGISTERS 256
#define MAX_FRAMES 64
#define MAX_GLOBALS 256

// array type
typedef struct Array {
    size_t refcount;
    size_t capacity;
    size_t length;
    Value* data;
} Array;

// string type
typedef struct String {
    size_t refcount;
    size_t length;
    uint32_t hash;
    char data[];
} String;

// iterator type
typedef struct Iterator {
    Value source;
    size_t index;
} Iterator;

// upvalue type
typedef struct Upvalue {
    Value* location;
    Value closed;
    bool is_closed;
    struct Upvalue* next;
} Upvalue;

// closure type
typedef struct Closure {
    size_t refcount;
    FunctionProto* proto;
    Upvalue** upvalues;
    uint8_t upvalue_count;
} Closure;

// call frame
typedef struct {
    Closure* closure;
    uint8_t* ip;
    Value* registers;
    int base_register;
    uint8_t return_register;
} CallFrame;

// global entry
typedef struct {
    String* name;
    Value value;
} Global;

// vm state
struct VM {
    Value registers[MAX_REGISTERS];
    CallFrame frames[MAX_FRAMES];
    int frame_count;
    Upvalue* open_upvalues;
    
    Global* globals;
    size_t global_count;
    size_t global_capacity;
    
    struct {
        char* name;
        NativeFunction func;
    } natives[64];
    int native_count;
};

// external array functions
extern Array* array_new(size_t capacity);
extern void array_release(Array* arr);
extern Value array_get(Array* arr, size_t idx);
extern void array_set(Array* arr, size_t idx, Value val);
extern void array_push(Array* arr, Value val);
extern size_t array_len(Array* arr);

// external string functions
extern String* string_intern(const char* str, size_t len);
extern void string_release(String* str);

// external iterator functions
extern Iterator* iter_new(Value iterable);
extern Value iter_next(Iterator* iter);
extern bool iter_has_next(Iterator* iter);
extern void iter_free(Iterator* iter);

// create vm
VM* mica_new(void) {
    VM* vm = malloc(sizeof(VM));
    vm->frame_count = 0;
    vm->open_upvalues = NULL;
    vm->global_count = 0;
    vm->global_capacity = 16;
    vm->globals = calloc(vm->global_capacity, sizeof(Global));
    vm->native_count = 0;
    
    // initialize all registers to none
    for (int i = 0; i < MAX_REGISTERS; i++) {
        memset(&vm->registers[i], 0, sizeof(Value));
        vm->registers[i].type = VAL_NONE;
    }
    
    return vm;
}

// free vm
void mica_free(VM* vm) {
    for (size_t i = 0; i < vm->global_count; i++) {
        string_release(vm->globals[i].name);
        value_free(&vm->globals[i].value);
    }
    free(vm->globals);
    
    for (int i = 0; i < vm->native_count; i++) {
        free(vm->natives[i].name);
    }
    
    free(vm);
}

// upvalue management - initialize captured values
static Upvalue* capture_upvalue(VM* vm, Value* local) {
    Upvalue* prev = NULL;
    Upvalue* upvalue = vm->open_upvalues;
    
    // find the correct position in the linked list (sorted by stack position)
    while (upvalue != NULL && upvalue->location > local) {
        prev = upvalue;
        upvalue = upvalue->next;
    }
    
    // if we found an existing upvalue for this location, return it
    if (upvalue != NULL && upvalue->location == local) {
        return upvalue;
    }
    
    // create a new upvalue
    Upvalue* created = malloc(sizeof(Upvalue));
    created->location = local;
    // initialize closed with zero to ensure clean state
    memset(&created->closed, 0, sizeof(Value));
    created->closed.type = VAL_NONE;
    created->is_closed = false;
    created->next = upvalue;
    
    // insert into the linked list
    if (prev == NULL) {
        vm->open_upvalues = created;
    } else {
        prev->next = created;
    }
    
    return created;
}

static void close_upvalues(VM* vm, Value* last) {
    while (vm->open_upvalues != NULL && vm->open_upvalues->location >= last) {
        Upvalue* upvalue = vm->open_upvalues;
        // copy the value from the stack to the upvalue's closed storage
        upvalue->closed = *upvalue->location;
        // point the location to the closed storage
        upvalue->location = &upvalue->closed;
        upvalue->is_closed = true;
        // remove from the open upvalues list
        vm->open_upvalues = upvalue->next;
    }
}

// global variable management
static Global* find_global(VM* vm, String* name) {
    for (size_t i = 0; i < vm->global_count; i++) {
        if (vm->globals[i].name == name) {
            return &vm->globals[i];
        }
    }
    return NULL;
}

void mica_set_global(VM* vm, const char* name, Value val) {
    String* name_str = string_intern(name, strlen(name));
    
    Global* existing = find_global(vm, name_str);
    if (existing) {
        value_free(&existing->value);
        existing->value = val;
        string_release(name_str);
        return;
    }
    
    if (vm->global_count >= vm->global_capacity) {
        vm->global_capacity *= 2;
        vm->globals = realloc(vm->globals, sizeof(Global) * vm->global_capacity);
    }
    
    vm->globals[vm->global_count].name = name_str;
    vm->globals[vm->global_count].value = val;
    vm->global_count++;
}

Value mica_get_global(VM* vm, const char* name) {
    String* name_str = string_intern(name, strlen(name));
    Global* g = find_global(vm, name_str);
    string_release(name_str);
    
    if (g) {
        return g->value;
    }
    
    Value none;
    memset(&none, 0, sizeof(Value));
    none.type = VAL_NONE;
    return none;
}

// native functions
void mica_register_native(VM* vm, const char* name, NativeFunction fn) {
    if (vm->native_count >= 64) {
        fprintf(stderr, "too many native functions\n");
        return;
    }
    
    vm->natives[vm->native_count].name = strdup(name);
    vm->natives[vm->native_count].func = fn;
    vm->native_count++;
}

static NativeFunction find_native(VM* vm, const char* name) {
    for (int i = 0; i < vm->native_count; i++) {
        if (strcmp(vm->natives[i].name, name) == 0) {
            return vm->natives[i].func;
        }
    }
    return NULL;
}

// read macros
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() (frame->ip += 2, (int16_t)((frame->ip[-2] << 8) | frame->ip[-1]))

// run vm
bool mica_run(VM* vm) {
    if (vm->frame_count == 0) return false;
    
    CallFrame* frame = &vm->frames[vm->frame_count - 1];
    
    for (;;) {
        uint8_t instruction = READ_BYTE();
        
        switch (instruction) {
            case OP_NOP:
                break;
                
            case OP_LOAD_CONST: {
                uint8_t const_idx = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = frame->closure->proto->constants[const_idx];
                break;
            }
            
            case OP_LOAD_LOCAL: {
                uint8_t local_idx = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = vm->registers[frame->base_register + local_idx];
                break;
            }
            
            case OP_STORE_LOCAL: {
                uint8_t local_idx = READ_BYTE();
                uint8_t src = READ_BYTE();
                vm->registers[frame->base_register + local_idx] = vm->registers[frame->base_register + src];
                break;
            }
            
            case OP_LOAD_GLOBAL: {
                uint8_t name_idx = READ_BYTE();
                uint8_t dest = READ_BYTE();
                Value name_val = frame->closure->proto->constants[name_idx];
                String* name = (String*)name_val.as_ptr;
                
                Global* g = find_global(vm, name);
                if (g) {
                    vm->registers[frame->base_register + dest] = g->value;
                } else {
                    // check if it's a native function
                    NativeFunction native = find_native(vm, name->data);
                    if (native) {
                        Value native_val;
                        memset(&native_val, 0, sizeof(Value));
                        native_val.type = VAL_NATIVE;
                        native_val.as_ptr = (void*)native;
                        vm->registers[frame->base_register + dest] = native_val;
                    } else {
                        fprintf(stderr, "undefined variable: %s\n", name->data);
                        memset(&vm->registers[frame->base_register + dest], 0, sizeof(Value));
                        vm->registers[frame->base_register + dest].type = VAL_NONE;
                    }
                }
                break;
            }
            
            case OP_STORE_GLOBAL: {
                uint8_t name_idx = READ_BYTE();
                uint8_t src = READ_BYTE();
                Value name_val = frame->closure->proto->constants[name_idx];
                String* name = (String*)name_val.as_ptr;
                
                Value src_val = vm->registers[frame->base_register + src];
                
                Global* g = find_global(vm, name);
                if (g) {
                    value_free(&g->value);
                    g->value = src_val;
                    // increment refcount for closure types
                    if (g->value.type == VAL_CLOSURE) {
                        Closure* closure = (Closure*)g->value.as_ptr;
                        closure->refcount++;
                    } else if (g->value.type == VAL_ARRAY) {
                        Array* arr = (Array*)g->value.as_ptr;
                        arr->refcount++;
                    } else if (g->value.type == VAL_STRING) {
                        String* str = (String*)g->value.as_ptr;
                        str->refcount++;
                    }
                } else {
                    if (vm->global_count >= vm->global_capacity) {
                        vm->global_capacity *= 2;
                        vm->globals = realloc(vm->globals, sizeof(Global) * vm->global_capacity);
                    }
                    // retain the name string
                    name->refcount++;
                    vm->globals[vm->global_count].name = name;
                    vm->globals[vm->global_count].value = src_val;
                    
                    // increment refcount for heap-allocated types
                    if (src_val.type == VAL_CLOSURE) {
                        Closure* closure = (Closure*)src_val.as_ptr;
                        closure->refcount++;
                    } else if (src_val.type == VAL_ARRAY) {
                        Array* arr = (Array*)src_val.as_ptr;
                        arr->refcount++;
                    } else if (src_val.type == VAL_STRING) {
                        String* str = (String*)src_val.as_ptr;
                        str->refcount++;
                    }
                    
                    vm->global_count++;
                }
                break;
            }
            
            case OP_MOVE: {
                uint8_t src = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = vm->registers[frame->base_register + src];
                break;
            }
            
            case OP_LOAD_UPVAL: {
                uint8_t upval_idx = READ_BYTE();
                uint8_t dest = READ_BYTE();
                Upvalue* upval = frame->closure->upvalues[upval_idx];
                vm->registers[frame->base_register + dest] = *upval->location;
                break;
            }
            
            case OP_STORE_UPVAL: {
                uint8_t upval_idx = READ_BYTE();
                uint8_t src = READ_BYTE();
                Upvalue* upval = frame->closure->upvalues[upval_idx];
                *upval->location = vm->registers[frame->base_register + src];
                break;
            }
            
            case OP_ADD: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_i32(
                        reg_a->as_i32 + reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_f32(fa + fb);
                }
                break;
            }
            
            case OP_SUB: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_i32(
                        reg_a->as_i32 - reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_f32(fa - fb);
                }
                break;
            }
            
            case OP_MUL: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_i32(
                        reg_a->as_i32 * reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_f32(fa * fb);
                }
                break;
            }
            
            case OP_DIV: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_i32(
                        reg_a->as_i32 / reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_f32(fa / fb);
                }
                break;
            }
            
            case OP_MOD: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = value_i32(
                    vm->registers[frame->base_register + a].as_i32 % vm->registers[frame->base_register + b].as_i32);
                break;
            }
            
            case OP_NEG: {
                uint8_t src = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_src = &vm->registers[frame->base_register + src];
                
                if (reg_src->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_i32(-reg_src->as_i32);
                } else {
                    vm->registers[frame->base_register + dest] = value_f32(-reg_src->as_f32);
                }
                break;
            }
            
            case OP_EQ: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = value_bool(
                    value_equal(vm->registers[frame->base_register + a], vm->registers[frame->base_register + b]));
                break;
            }
            
            case OP_NE: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = value_bool(
                    !value_equal(vm->registers[frame->base_register + a], vm->registers[frame->base_register + b]));
                break;
            }
            
            case OP_LT: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_bool(
                        reg_a->as_i32 < reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_bool(fa < fb);
                }
                break;
            }
            
            case OP_LE: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_bool(
                        reg_a->as_i32 <= reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_bool(fa <= fb);
                }
                break;
            }
            
            case OP_GT: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_bool(
                        reg_a->as_i32 > reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_bool(fa > fb);
                }
                break;
            }
            
            case OP_GE: {
                uint8_t a = READ_BYTE();
                uint8_t b = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value* reg_a = &vm->registers[frame->base_register + a];
                Value* reg_b = &vm->registers[frame->base_register + b];
                
                if (reg_a->type == VAL_I32 && reg_b->type == VAL_I32) {
                    vm->registers[frame->base_register + dest] = value_bool(
                        reg_a->as_i32 >= reg_b->as_i32);
                } else {
                    float fa = (reg_a->type == VAL_I32) ? (float)reg_a->as_i32 : reg_a->as_f32;
                    float fb = (reg_b->type == VAL_I32) ? (float)reg_b->as_i32 : reg_b->as_f32;
                    vm->registers[frame->base_register + dest] = value_bool(fa >= fb);
                }
                break;
            }
            
            case OP_JMP: {
                int16_t offset = READ_SHORT();
                frame->ip += offset;
                break;
            }
            
            case OP_JMP_IF: {
                uint8_t reg = READ_BYTE();
                int16_t offset = READ_SHORT();
                if (value_is_truthy(vm->registers[frame->base_register + reg])) {
                    frame->ip += offset;
                }
                break;
            }
            
            case OP_JMP_IF_NOT: {
                uint8_t reg = READ_BYTE();
                int16_t offset = READ_SHORT();
                if (!value_is_truthy(vm->registers[frame->base_register + reg])) {
                    frame->ip += offset;
                }
                break;
            }
            
            case OP_RET: {
                uint8_t nvals = READ_BYTE();
                Value result;
                memset(&result, 0, sizeof(Value));
                result.type = VAL_NONE;
                
                if (nvals > 0) {
                    uint8_t val_reg = READ_BYTE();
                    result = vm->registers[frame->base_register + val_reg];
                }
                
                close_upvalues(vm, &vm->registers[frame->base_register]);
                
                uint8_t return_reg = frame->return_register;
                
                vm->frame_count--;
                if (vm->frame_count == 0) {
                    return true;
                }
                
                frame = &vm->frames[vm->frame_count - 1];
                vm->registers[return_reg] = result;
                break;
            }
            
            case OP_CALL: {
                uint8_t func_reg = READ_BYTE();
                uint8_t nargs = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Value func_val = vm->registers[frame->base_register + func_reg];
                
                // check if it's a native function
                if (func_val.type == VAL_NATIVE) {
                    NativeFunction native = (NativeFunction)func_val.as_ptr;
                    Value args[256];
                    
                    // zero out the entire args array to prevent stack garbage
                    memset(args, 0, sizeof(args));
                    
                    for (uint8_t i = 0; i < nargs; i++) {
                        args[i] = vm->registers[frame->base_register + func_reg + 1 + i];
                    }
                    vm->registers[frame->base_register + dest] = native(args, nargs);
                } 
                // check if it's a closure
                else if (func_val.type == VAL_CLOSURE) {
                    Closure* closure = (Closure*)func_val.as_ptr;
                    
                    if (vm->frame_count >= MAX_FRAMES) {
                        fprintf(stderr, "stack overflow\n");
                        return false;
                    }
                    
                    // the new frame's base register should point to where the first argument is
                    // arguments are at: frame->base_register + func_reg + 1, func_reg + 2, ...
                    // so the new base is: frame->base_register + func_reg + 1
                    int new_base = frame->base_register + func_reg + 1;
                    
                    // initialize the new frame's registers to none to prevent garbage
                    for (int i = new_base; i < new_base + 32 && i < MAX_REGISTERS; i++) {
                        if (i >= new_base + nargs) {
                            memset(&vm->registers[i], 0, sizeof(Value));
                            vm->registers[i].type = VAL_NONE;
                        }
                    }
                    
                    CallFrame* new_frame = &vm->frames[vm->frame_count++];
                    new_frame->closure = closure;
                    new_frame->ip = closure->proto->code;
                    new_frame->registers = vm->registers;
                    new_frame->base_register = new_base;
                    new_frame->return_register = frame->base_register + dest;
                    
                    frame = new_frame;
                } else {
                    fprintf(stderr, "not a function (type=%d, base_reg=%d, func_reg=%d)\n", func_val.type, frame->base_register, func_reg);
                    return false;
                }
                break;
            }
            
            case OP_CLOSURE: {
                uint8_t const_idx = READ_BYTE();
                uint8_t dest = READ_BYTE();
                uint8_t upvalue_count = READ_BYTE();
                
                Value proto_val = frame->closure->proto->constants[const_idx];
                FunctionProto* proto = (FunctionProto*)proto_val.as_ptr;
                
                Closure* closure = malloc(sizeof(Closure));
                closure->refcount = 1;
                closure->proto = proto;
                closure->upvalue_count = upvalue_count;
                
                if (upvalue_count > 0) {
                    closure->upvalues = malloc(sizeof(Upvalue*) * upvalue_count);
                    
                    for (uint8_t i = 0; i < upvalue_count; i++) {
                        uint8_t is_local = READ_BYTE();
                        uint8_t index = READ_BYTE();
                        
                        if (is_local) {
                            closure->upvalues[i] = capture_upvalue(vm, 
                                &vm->registers[frame->base_register + index]);
                        } else {
                            closure->upvalues[i] = frame->closure->upvalues[index];
                        }
                    }
                } else {
                    closure->upvalues = NULL;
                }
                
                Value closure_val;
                memset(&closure_val, 0, sizeof(Value));
                closure_val.type = VAL_CLOSURE;
                closure_val.as_ptr = closure;
                vm->registers[frame->base_register + dest] = closure_val;
                break;
            }
            
            case OP_CLOSE_UPVAL: {
                uint8_t local = READ_BYTE();
                close_upvalues(vm, &vm->registers[frame->base_register + local]);
                break;
            }
            
            case OP_ARRAY_NEW: {
                uint8_t capacity = READ_BYTE();
                uint8_t dest = READ_BYTE();
                vm->registers[frame->base_register + dest] = value_array(capacity);
                break;
            }
            
            case OP_ARRAY_GET: {
                uint8_t arr_reg = READ_BYTE();
                uint8_t idx_reg = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                if (vm->registers[frame->base_register + arr_reg].type != VAL_ARRAY) {
                    fprintf(stderr, "not an array\n");
                    return false;
                }
                if (vm->registers[frame->base_register + idx_reg].type != VAL_I32) {
                    fprintf(stderr, "array index must be an integer\n");
                    return false;
                }
                
                Array* arr = (Array*)vm->registers[frame->base_register + arr_reg].as_ptr;
                int32_t idx = vm->registers[frame->base_register + idx_reg].as_i32;
                
                if (idx < 0 || (size_t)idx >= array_len(arr)) {
                    fprintf(stderr, "array index out of bounds: %d\n", idx);
                    return false;
                }
                
                vm->registers[frame->base_register + dest] = array_get(arr, idx);
                break;
            }
            
            case OP_ARRAY_SET: {
                uint8_t arr_reg = READ_BYTE();
                uint8_t idx_reg = READ_BYTE();
                uint8_t val_reg = READ_BYTE();
                
                if (vm->registers[frame->base_register + arr_reg].type != VAL_ARRAY) {
                    fprintf(stderr, "not an array\n");
                    return false;
                }
                if (vm->registers[frame->base_register + idx_reg].type != VAL_I32) {
                    fprintf(stderr, "array index must be an integer\n");
                    return false;
                }
                
                Array* arr = (Array*)vm->registers[frame->base_register + arr_reg].as_ptr;
                int32_t idx = vm->registers[frame->base_register + idx_reg].as_i32;
                
                if (idx < 0 || (size_t)idx >= array_len(arr)) {
                    fprintf(stderr, "array index out of bounds: %d\n", idx);
                    return false;
                }
                
                array_set(arr, idx, vm->registers[frame->base_register + val_reg]);
                break;
            }
            
            case OP_ARRAY_LEN: {
                uint8_t arr_reg = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Array* arr = (Array*)vm->registers[frame->base_register + arr_reg].as_ptr;
                vm->registers[frame->base_register + dest] = value_i32(array_len(arr));
                break;
            }
            
            case OP_ARRAY_PUSH: {
                uint8_t arr_reg = READ_BYTE();
                uint8_t val_reg = READ_BYTE();
                
                Array* arr = (Array*)vm->registers[frame->base_register + arr_reg].as_ptr;
                array_push(arr, vm->registers[frame->base_register + val_reg]);
                break;
            }
            
            case OP_ITER_NEW: {
                uint8_t iterable_reg = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                Iterator* it = iter_new(vm->registers[frame->base_register + iterable_reg]);
                Value v;
                memset(&v, 0, sizeof(Value));
                v.type = VAL_NATIVE;
                v.as_ptr = it;
                vm->registers[frame->base_register + dest] = v;
                break;
            }
            
            case OP_ITER_NEXT: {
                uint8_t iter_reg = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                if (vm->registers[frame->base_register + iter_reg].type != VAL_NATIVE) {
                    fprintf(stderr, "not an iterator\n");
                    return false;
                }
                
                Iterator* it = (Iterator*)vm->registers[frame->base_register + iter_reg].as_ptr;
                vm->registers[frame->base_register + dest] = iter_next(it);
                break;
            }
            
            case OP_ITER_HAS_NEXT: {
                uint8_t iter_reg = READ_BYTE();
                uint8_t dest = READ_BYTE();
                
                if (vm->registers[frame->base_register + iter_reg].type != VAL_NATIVE) {
                    fprintf(stderr, "not an iterator\n");
                    return false;
                }
                
                Iterator* it = (Iterator*)vm->registers[frame->base_register + iter_reg].as_ptr;
                vm->registers[frame->base_register + dest] = value_bool(iter_has_next(it));
                break;
            }
            
            default:
                fprintf(stderr, "unknown opcode: %d\n", instruction);
                return false;
        }
    }
    
    return true;
}

// compile and run wrapper
extern AstNode* parse(const char* source);
extern FunctionProto* compile(AstNode* ast);

bool mica_compile(VM* vm, const char* source) {
    AstNode* ast = parse(source);
    if (!ast) return false;
    
    FunctionProto* proto = compile(ast);
    if (!proto) return false;
    
    Closure* closure = malloc(sizeof(Closure));
    closure->refcount = 1;
    closure->proto = proto;
    closure->upvalues = NULL;
    closure->upvalue_count = 0;
    
    CallFrame* frame = &vm->frames[vm->frame_count++];
    frame->closure = closure;
    frame->ip = proto->code;
    frame->registers = vm->registers;
    frame->base_register = 0;
    frame->return_register = 0;
    
    return true;
}