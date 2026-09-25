#ifndef CL_BINDINGS_H
#define CL_BINDINGS_H

#include "cl_internal.h"

typedef struct cl_binding {
    char *name;
    cl_value_t *value;
} cl_binding_t;

struct cl_bindings {
    cl_binding_t *items;
    size_t count;
    size_t capacity;
    cl_arena_t arena;
};

/* The value bound to `name`, or NULL. `bindings` may be NULL. */
const cl_value_t *cl_bindings_lookup(const cl_bindings_t *bindings, const char *name);

#endif /* CL_BINDINGS_H */
