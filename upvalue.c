#include "mica.h"
#include <stdlib.h>

typedef struct Upvalue {
    Value* location;
    Value closed;
    bool is_closed;
    struct Upvalue* next;
} Upvalue;

Upvalue* upvalue_new(Value* location) {
    Upvalue* upval = malloc(sizeof(Upvalue));
    upval->location = location;
    upval->closed = value_none(); // initialize the closed value
    upval->is_closed = false;
    upval->next = NULL;
    return upval;
}

void upvalue_close(Upvalue* upval) {
    if (upval->is_closed) return;
    
    upval->closed = *upval->location;
    upval->location = &upval->closed;
    upval->is_closed = true;
}

void upvalue_free(Upvalue* upval) {
    if (upval->is_closed) {
        value_free(&upval->closed);
    }
    free(upval);
}