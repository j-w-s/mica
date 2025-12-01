#include "mica.h"
#include <stdlib.h>

// upvalue structure
typedef struct Upvalue {
    Value* location;
    Value closed;
    bool is_closed;
    struct Upvalue* next;
} Upvalue;

// create new upvalue
Upvalue* upvalue_new(Value* location) {
    Upvalue* upval = malloc(sizeof(Upvalue));
    upval->location = location;
    upval->closed = value_none(); // initialize the closed value
    upval->is_closed = false;
    upval->next = NULL;
    return upval;
}

// close upvalue
void upvalue_close(Upvalue* upval) {
    if (upval->is_closed) return;
    
    upval->closed = *upval->location;
    upval->location = &upval->closed;
    upval->is_closed = true;
}

// free upvalue
void upvalue_free(Upvalue* upval) {
    if (upval->is_closed) {
        value_free(&upval->closed);
    }
    free(upval);
}