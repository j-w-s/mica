#include "mica.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// forward declarations
typedef struct Array Array;
extern size_t array_len(Array* arr);

// print
Value builtin_print(Value* args, size_t nargs) {
    for (size_t i = 0; i < nargs; i++) {
        if (i > 0) printf(" ");
        value_print(args[i]);
    }
    printf("\n");
    return value_none();
}

// len
Value builtin_len(Value* args, size_t nargs) {
    if (nargs < 1) return value_i32(0);
    
    Value val = args[0];
    if (val.type == VAL_ARRAY) {
        Array* arr = (Array*)val.as_ptr;
        return value_i32(array_len(arr));
    }
    
    return value_i32(0);
}

// assert
Value builtin_assert(Value* args, size_t nargs) {
    if (nargs < 1) {
        fprintf(stderr, "assertion failed\n");
        exit(1);
    }
    
    if (!value_is_truthy(args[0])) {
        fprintf(stderr, "assertion failed");
        if (nargs > 1 && args[1].type == VAL_STRING) {
            fprintf(stderr, ": ");
            value_print(args[1]);
        }
        fprintf(stderr, "\n");
        exit(1);
    }
    
    return value_none();
}

// type_of
Value builtin_type_of(Value* args, size_t nargs) {
    if (nargs < 1) return value_string("none");
    
    switch (args[0].type) {
        case VAL_I32: return value_string("i32");
        case VAL_F32: return value_string("f32");
        case VAL_BOOL: return value_string("bool");
        case VAL_ARRAY: return value_string("array");
        case VAL_STRING: return value_string("string");
        case VAL_CLOSURE: return value_string("function");
        case VAL_NATIVE: return value_string("function");
        case VAL_NONE: return value_string("none");
        default: return value_string("unknown");
    }
}

// str (convert to string)
Value builtin_str(Value* args, size_t nargs) {
    if (nargs < 1) return value_string("");
    
    char buf[64];
    Value val = args[0];
    
    switch (val.type) {
        case VAL_I32:
            snprintf(buf, sizeof(buf), "%d", val.as_i32);
            return value_string(buf);
        case VAL_F32:
            snprintf(buf, sizeof(buf), "%g", val.as_f32);
            return value_string(buf);
        case VAL_BOOL:
            return value_string(val.as_bool ? "true" : "false");
        case VAL_NONE:
            return value_string("None");
        default:
            return value_string("<object>");
    }
}

// parse_int
Value builtin_parse_int(Value* args, size_t nargs) {
    if (nargs < 1 || args[0].type != VAL_STRING) {
        return value_none();
    }
    
    // would need to extract string data
    return value_i32(0);
}

// abs
Value builtin_abs(Value* args, size_t nargs) {
    if (nargs < 1) return value_i32(0);
    
    Value val = args[0];
    if (val.type == VAL_I32) {
        return value_i32(abs(val.as_i32));
    } else if (val.type == VAL_F32) {
        return value_f32(fabsf(val.as_f32));
    }
    
    return value_i32(0);
}

// sqrt
Value builtin_sqrt(Value* args, size_t nargs) {
    if (nargs < 1) return value_f32(0.0f);
    
    Value val = args[0];
    if (val.type == VAL_I32) {
        return value_f32(sqrtf((float)val.as_i32));
    } else if (val.type == VAL_F32) {
        return value_f32(sqrtf(val.as_f32));
    }
    
    return value_f32(0.0f);
}

// floor
Value builtin_floor(Value* args, size_t nargs) {
    if (nargs < 1) return value_i32(0);
    
    Value val = args[0];
    if (val.type == VAL_F32) {
        return value_i32((int32_t)floorf(val.as_f32));
    } else if (val.type == VAL_I32) {
        return val;
    }
    
    return value_i32(0);
}

// register all builtins
void register_builtins(VM* vm) {
    mica_register_native(vm, "print", builtin_print);
    mica_register_native(vm, "len", builtin_len);
    mica_register_native(vm, "assert", builtin_assert);
    mica_register_native(vm, "type_of", builtin_type_of);
    mica_register_native(vm, "str", builtin_str);
    mica_register_native(vm, "parse_int", builtin_parse_int);
    mica_register_native(vm, "abs", builtin_abs);
    mica_register_native(vm, "sqrt", builtin_sqrt);
    mica_register_native(vm, "floor", builtin_floor);
}