#ifndef CL_BINDINGS_H
#define CL_BINDINGS_H

#include "cl_internal.h"

typedef struct cl_binding {
    char *name;
    cl_value_t *value;
} cl_binding_t;

typedef struct cl_host_function {
    char *name;
    size_t min_args;
    size_t max_args;
    cl_function_t fn;
    void *userdata;
} cl_host_function_t;

struct cl_bindings {
    cl_binding_t *items;
    size_t count;
    size_t capacity;
    cl_host_function_t *functions;
    size_t function_count;
    size_t function_capacity;
    cl_arena_t arena;
};

/* The value bound to `name`, or NULL. `bindings` may be NULL. */
const cl_value_t *cl_bindings_lookup(const cl_bindings_t *bindings, const char *name);

/* The host function registered as `name`, or NULL. `bindings` may be NULL. */
const cl_host_function_t *cl_bindings_lookup_function(const cl_bindings_t *bindings, const char *name);

/* True when `target` is `v` itself or appears anywhere inside it. */
int cl_value_reaches(const cl_value_t *v, const cl_value_t *target);

#endif /* CL_BINDINGS_H */
