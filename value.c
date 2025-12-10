#include "mica.h"
#include <stdio.h>
#include <string.h>

Value value_i32(int32_t val) {
    Value v;
    memset(&v, 0, sizeof(Value));
    v.type = VAL_I32;
    v.as_i32 = val;
    return v;
}

Value value_f32(float val) {
    Value v;
    memset(&v, 0, sizeof(Value));
    v.type = VAL_F32;
    v.as_f32 = val;
    return v;
}

Value value_bool(bool val) {
    Value v;
    memset(&v, 0, sizeof(Value));
    v.type = VAL_BOOL;
    v.as_bool = val;
    return v;
}

Value value_none(void) {
    Value v;
    // zero out entire struct to ensure no garbage in union
    memset(&v, 0, sizeof(Value));
    v.type = VAL_NONE;
    return v;
}

// truthiness
bool value_is_truthy(Value val) {
    switch (val.type) {
        case VAL_BOOL: return val.as_bool;
        case VAL_NONE: return false;
        case VAL_I32: return val.as_i32 != 0;
        case VAL_F32: return val.as_f32 != 0.0f;
        default: return true;
    }
}

// equality
bool value_equal(Value a, Value b) {
    if (a.type != b.type) return false;
    
    switch (a.type) {
        case VAL_I32: return a.as_i32 == b.as_i32;
        case VAL_F32: return a.as_f32 == b.as_f32;
        case VAL_BOOL: return a.as_bool == b.as_bool;
        case VAL_NONE: return true;
        case VAL_ARRAY:
        case VAL_STRING:
        case VAL_CLOSURE:
        case VAL_NATIVE:
            return a.as_ptr == b.as_ptr;
        default: return false;
    }
}

void value_print(Value val) {
    switch (val.type) {
        case VAL_I32:
            printf("%d", val.as_i32);
            break;
        case VAL_F32:
            printf("%g", val.as_f32);
            break;
        case VAL_BOOL:
            printf("%s", val.as_bool ? "true" : "false");
            break;
        case VAL_NONE:
            printf("None");
            break;
        case VAL_ARRAY:
            array_print(val.as_ptr);
            break;
        case VAL_STRING:
            string_print(val.as_ptr);
            break;
        case VAL_CLOSURE:
            printf("<closure>");
            break;
        case VAL_NATIVE:
            printf("<native function>");
            break;
    }
}

void value_free(Value* val) {
    switch (val->type) {
        case VAL_ARRAY:
            if (val->as_ptr) array_release(val->as_ptr);
            break;
        case VAL_STRING:
            if (val->as_ptr) string_release(val->as_ptr);
            break;
        case VAL_CLOSURE:
            // closures are refcounted and managed by the vm
            break;
        case VAL_NATIVE:
            // native functions don't need cleanup
            break;
        default:
            break; // primitives don't need cleanup
    }
    val->type = VAL_NONE;
    val->as_ptr = NULL;
}