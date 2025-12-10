#include "mica.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct String {
    size_t refcount;
    size_t length;
    uint32_t hash;
    char data[];
} String;

// fnv-1a hash
static uint32_t hash_string(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint8_t)str[i];
        hash *= 16777619;
    }
    return hash;
}

String* string_new(const char* str, size_t len) {
    String* s = malloc(sizeof(String) + len + 1);
    if (!s) return NULL;
    
    s->refcount = 1;
    s->length = len;
    s->hash = hash_string(str, len);
    memcpy(s->data, str, len);
    s->data[len] = '\0';
    
    return s;
}

// intern table for linear search
#define INTERN_TABLE_SIZE 256
static String* intern_table[INTERN_TABLE_SIZE] = {0};

String* string_intern(const char* str, size_t len) {
    uint32_t hash = hash_string(str, len);
    size_t index = hash % INTERN_TABLE_SIZE;
    
    // check if already interned
    String* s = intern_table[index];
    while (s) {
        if (s->hash == hash && s->length == len && 
            memcmp(s->data, str, len) == 0) {
            s->refcount++;
            return s;
        }
        // probe
        index = (index + 1) % INTERN_TABLE_SIZE;
        s = intern_table[index];
    }
    
    // not found, create new
    s = string_new(str, len);
    if (s) {
        intern_table[index] = s;
    }
    return s;
}

void string_retain(String* str) {
    if (str) str->refcount++;
}

void string_release(String* str) {
    if (!str) return;
    if (--str->refcount == 0) {
        free(str);
    }
}

void string_print(String* str) {
    if (str) {
        printf("%s", str->data);
    }
}

Value value_string(const char* str) {
    String* s = string_intern(str, strlen(str));
    Value v;
    memset(&v, 0, sizeof(Value));
    v.type = VAL_STRING;
    v.as_ptr = s;
    return v;
}