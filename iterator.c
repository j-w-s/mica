#include "mica.h"
#include <stdlib.h>

typedef struct Array Array;
extern size_t array_len(Array* arr);
extern Value array_get(Array* arr, size_t idx);

typedef struct Iterator {
    Value source;
    size_t index;
} Iterator;

Iterator* iter_new(Value iterable) {
    Iterator* it = malloc(sizeof(Iterator));
    it->source = iterable;
    it->index = 0;
    return it;
}

Value iter_next(Iterator* iter) {
    if (iter->source.type == VAL_ARRAY) {
        Array* arr = (Array*)iter->source.as_ptr;
        if (iter->index >= array_len(arr)) {
            return value_none();
        }
        Value val = array_get(arr, iter->index);
        iter->index++;
        return val;
    }
    
    return value_none();
}

bool iter_has_next(Iterator* iter) {
    if (iter->source.type == VAL_ARRAY) {
        Array* arr = (Array*)iter->source.as_ptr;
        return iter->index < array_len(arr);
    }
    return false;
}

void iter_free(Iterator* iter) {
    if (iter) free(iter);
}