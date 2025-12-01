#include "mica.h"
#include <stdlib.h>

// forward declarations
typedef struct Array Array;
extern size_t array_len(Array* arr);
extern Value array_get(Array* arr, size_t idx);

// iterator
typedef struct Iterator {
    Value source;
    size_t index;
} Iterator;

// create iterator
Iterator* iter_new(Value iterable) {
    Iterator* it = malloc(sizeof(Iterator));
    it->source = iterable;
    it->index = 0;
    return it;
}

// get next value
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

// check if has next
bool iter_has_next(Iterator* iter) {
    if (iter->source.type == VAL_ARRAY) {
        Array* arr = (Array*)iter->source.as_ptr;
        return iter->index < array_len(arr);
    }
    return false;
}

// free iterator
void iter_free(Iterator* iter) {
    if (iter) free(iter);
}