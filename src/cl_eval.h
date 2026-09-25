#ifndef CL_EVAL_H
#define CL_EVAL_H

#include "cl_internal.h"

struct cl_evaluated {
    cl_evaluated_body_t *root;
    cl_arena_t arena;
};

/* Chain of "for" loop-variable bindings currently in scope. Looked up
 * before falling back to the document's top-level attributes/blocks, so a
 * loop variable always shadows a same-named top-level definition - this is
 * ordinary lexical scoping of names the user themselves chose in their own
 * for-expression/%{for}, not a reintroduction of HCL's keyword magic. */
typedef struct cl_eval_scope {
    const struct cl_eval_scope *parent;
    const char *key_name; /* NULL when this frame has no key binding */
    cl_value_t *key_value;
    const char *val_name;
    cl_value_t *val_value;
} cl_eval_scope_t;

/* Chain of top-level attributes/blocks whose value is currently being
 * resolved by a traversal, innermost first. Checked before recursing into
 * one of those values so "a = b" / "b = a" (or a single "a = a") fails with
 * a clear error instead of recursing until the C stack overflows. */
typedef struct cl_eval_resolving {
    const struct cl_eval_resolving *parent;
    const void *key; /* identity of the cl_attribute_t/cl_block_t in progress */
} cl_eval_resolving_t;

typedef struct cl_eval_ctx {
    cl_document_t *doc;
    cl_body_t *root_scope;
    cl_evaluated_t *result;
    cl_error_t *err;
    int failed;
    const cl_eval_resolving_t *resolving;
    const cl_bindings_t *bindings; /* NULL when evaluating without bindings */
    cl_value_t **bound_copies;     /* result-arena copy of bindings->items[i].value,
                                      made on first use; parallel to bindings->items */
} cl_eval_ctx_t;

void cl_eval_fail(cl_eval_ctx_t *ctx, int line, int col, const char *fmt, ...);

cl_value_t *cl_val_string(cl_eval_ctx_t *ctx, const char *s);
cl_value_t *cl_val_number(cl_eval_ctx_t *ctx, double n);
cl_value_t *cl_val_bool(cl_eval_ctx_t *ctx, int b);
cl_value_t *cl_val_null(cl_eval_ctx_t *ctx);
cl_value_t *cl_val_new_list(cl_eval_ctx_t *ctx);
void cl_val_list_add(cl_eval_ctx_t *ctx, cl_value_t *list, cl_value_t *item);
cl_value_t *cl_val_new_object(cl_eval_ctx_t *ctx);
void cl_val_object_add(cl_eval_ctx_t *ctx, cl_value_t *obj, const char *key, cl_value_t *value);

/* Coerces `v` to a string the way template interpolation does (string
 * passthrough, formatted number, "true"/"false"); fails ctx and returns
 * NULL for CL_VAL_NULL/LIST/OBJECT. Used by built-ins in cl_functions.c. */
const char *cl_val_require_string(cl_eval_ctx_t *ctx, const cl_value_t *v, int line, int col);

#endif /* CL_EVAL_H */
