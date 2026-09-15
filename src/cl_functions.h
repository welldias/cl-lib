#ifndef CL_FUNCTIONS_H
#define CL_FUNCTIONS_H

#include "cl_eval.h"

typedef cl_value_t *(*cl_builtin_fn_t)(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col);

/* Returns NULL when no built-in is registered under `name`. */
cl_builtin_fn_t cl_builtin_lookup(const char *name);

#endif /* CL_FUNCTIONS_H */
