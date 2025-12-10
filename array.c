#include "mica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Array {
    size_t refcount;
    size_t capacity;
    size_t length;
    Value* data;
} Array;

Array* array_new(size_t capacity) {
    Array* arr = malloc(sizeof(Array));
    if (!arr) return NULL;
    
    arr->refcount = 1;
    arr->capacity = capacity > 0 ? capacity : 8;
    arr->length = 0;
    arr->data = malloc(sizeof(Value) * arr->capacity);
    
    if (!arr->data) {
        free(arr);
        return NULL;
    }
    
    // initialize all values to none to prevent garbage
    for (size_t i = 0; i < arr->capacity; i++) {
        arr->data[i] = value_none();
    }
    
    return arr;
}

void array_retain(Array* arr) {
    if (arr) arr->refcount++;
}

void array_release(Array* arr) {
    if (!arr) return;
    if (--arr->refcount == 0) {
        for (size_t i = 0; i < arr->length; i++) {
            value_free(&arr->data[i]);
        }
        free(arr->data);
        free(arr);
    }
}

Value array_get(Array* arr, size_t idx) {
    if (idx >= arr->length) {
        return value_none();
    }
    return arr->data[idx];
}

void array_set(Array* arr, size_t idx, Value val) {
    if (idx >= arr->length) return;
    value_free(&arr->data[idx]);
    arr->data[idx] = val;
}

void array_push(Array* arr, Value val) {
    if (arr->length >= arr->capacity) {
        size_t new_cap = arr->capacity * 2;
        Value* new_data = realloc(arr->data, sizeof(Value) * new_cap);
        if (!new_data) return;
        arr->data = new_data;
        arr->capacity = new_cap;
        // initialize new slots
        for (size_t i = arr->length; i < new_cap; i++) {
            arr->data[i] = value_none();
        }
    }
    arr->data[arr->length++] = val;
}

size_t array_len(Array* arr) {
    return arr ? arr->length : 0;
}

void array_print(Array* arr) {
    if (!arr) {
        printf("[]");
        return;
    }
    
    printf("[");
    for (size_t i = 0; i < arr->length; i++) {
        if (i > 0) printf(", ");
        value_print(arr->data[i]);
    }
    printf("]");
}

Value value_array(size_t capacity) {
    Array* arr = array_new(capacity);
    Value v;
    memset(&v, 0, sizeof(Value));
    v.type = VAL_ARRAY;
    v.as_ptr = arr;
    return v;
}