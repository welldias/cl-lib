#ifndef CL_FUNCTIONS_H
#define CL_FUNCTIONS_H

#include "cl_bindings.h"
#include "cl_eval.h"

typedef cl_value_t *(*cl_builtin_fn_t)(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col);

typedef struct cl_builtin {
    const char *name;
    size_t min_args;
    size_t max_args;
    cl_builtin_fn_t fn; /* only called once argc is within [min_args, max_args] */
} cl_builtin_t;

/* Returns NULL when no built-in is registered under `name`. */
const cl_builtin_t *cl_builtin_lookup(const char *name);

/* Reports an argument-count mismatch for a built-in or host function (e.g.
 * "upper() expects 1 argument, got 2"). */
void cl_function_arity_fail(cl_eval_ctx_t *ctx, const char *name, size_t min_args, size_t max_args, size_t argc,
                            int line, int col);

/* Runs a host function (see cl_call.c) with already-evaluated arguments.
 * Returns NULL with ctx failed on any error. */
cl_value_t *cl_call_invoke(cl_eval_ctx_t *ctx, const cl_host_function_t *fn, cl_value_t **args, size_t argc,
                           int line, int col);

#endif /* CL_FUNCTIONS_H */
