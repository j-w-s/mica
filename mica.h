#ifndef MICA_H
#define MICA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct VM VM;
typedef struct Value Value;
typedef struct Array Array;
typedef struct String String;

typedef enum {
    VAL_I32,
    VAL_F32,
    VAL_BOOL,
    VAL_ARRAY,
    VAL_STRING,
    VAL_CLOSURE,
    VAL_NATIVE, 
    VAL_NONE
} ValueType;

// runtime values
struct Value {
    ValueType type;
    union {
        int32_t as_i32;
        float as_f32;
        bool as_bool;
        void* as_ptr;
    };
};

// native function type
typedef Value (*NativeFunction)(Value* args, size_t nargs);

VM* mica_new(void);
void mica_free(VM* vm);
bool mica_compile(VM* vm, const char* source);
bool mica_run(VM* vm);
Value mica_get_global(VM* vm, const char* name);
void mica_set_global(VM* vm, const char* name, Value val);
void mica_register_native(VM* vm, const char* name, NativeFunction fn);

Value value_i32(int32_t val);
Value value_f32(float val);
Value value_bool(bool val);
Value value_none(void);
Value value_array(size_t capacity);
Value value_string(const char* str);

bool value_is_truthy(Value val);
bool value_equal(Value a, Value b);
void value_print(Value val);
void value_free(Value* val);

extern Array* array_new(size_t capacity);
extern void array_release(Array* arr);
extern void array_print(Array* arr);

extern String* string_intern(const char* str, size_t len);
extern void string_release(String* str);
extern void string_print(String* str);

#endif // MICA_H