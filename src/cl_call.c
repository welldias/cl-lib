#include "cl_functions.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Everything a host function sees of the call in progress. Lives on the
 * stack of cl_call_invoke() for the duration of one callback. */
struct cl_call {
    cl_eval_ctx_t *ctx;
    const char *name;
    cl_value_t **args;
    size_t argc;
    int line;
    int col;
    /* Containers created by this call: the only ones cl_call_list_add() and
     * cl_call_object_set() may modify, since arguments are shared with the
     * rest of the document. malloc'd, freed when the callback returns. */
    cl_value_t **owned;
    size_t owned_count;
    size_t owned_capacity;
};

cl_value_t *cl_call_invoke(cl_eval_ctx_t *ctx, const cl_host_function_t *fn, cl_value_t **args, size_t argc,
                           int line, int col) {
    cl_call_t call = {0};
    call.ctx = ctx;
    call.name = fn->name;
    call.args = args;
    call.argc = argc;
    call.line = line;
    call.col = col;

    cl_value_t *result = fn->fn(&call, fn->userdata);
    free(call.owned);

    if (ctx->failed) {
        return NULL; /* cl_call_error() was called, even if a value came back */
    }
    if (!result) {
        cl_eval_fail(ctx, line, col, "%s() failed without reporting an error", fn->name);
        return NULL;
    }
    return result;
}

const char *cl_call_name(const cl_call_t *call) {
    return call ? call->name : NULL;
}

size_t cl_call_argc(const cl_call_t *call) {
    return call ? call->argc : 0;
}

cl_value_t *cl_call_arg(const cl_call_t *call, size_t index) {
    if (!call || index >= call->argc) {
        return NULL;
    }
    return call->args[index];
}

cl_value_t *cl_call_string(cl_call_t *call, const char *value) {
    if (!call || !value) {
        return NULL;
    }
    return cl_val_string(call->ctx, value);
}

cl_value_t *cl_call_number(cl_call_t *call, double value) {
    return call ? cl_val_number(call->ctx, value) : NULL;
}

cl_value_t *cl_call_bool(cl_call_t *call, int value) {
    return call ? cl_val_bool(call->ctx, value ? 1 : 0) : NULL;
}

cl_value_t *cl_call_null(cl_call_t *call) {
    return call ? cl_val_null(call->ctx) : NULL;
}

static cl_value_t *cl_call_own(cl_call_t *call, cl_value_t *container) {
    if (call->owned_count == call->owned_capacity) {
        size_t capacity = call->owned_capacity ? call->owned_capacity * 2 : 8;
        cl_value_t **grown = realloc(call->owned, capacity * sizeof(cl_value_t *));
        if (!grown) {
            abort();
        }
        call->owned = grown;
        call->owned_capacity = capacity;
    }
    call->owned[call->owned_count++] = container;
    return container;
}

static int cl_call_owns(const cl_call_t *call, const cl_value_t *container) {
    for (size_t i = call->owned_count; i > 0; i--) { /* most recent first */
        if (call->owned[i - 1] == container) {
            return 1;
        }
    }
    return 0;
}

cl_value_t *cl_call_list(cl_call_t *call) {
    return call ? cl_call_own(call, cl_val_new_list(call->ctx)) : NULL;
}

cl_value_t *cl_call_object(cl_call_t *call) {
    return call ? cl_call_own(call, cl_val_new_object(call->ctx)) : NULL;
}

int cl_call_list_add(cl_call_t *call, cl_value_t *list, cl_value_t *item) {
    if (!call || !list || !item || list->kind != CL_VAL_LIST || !cl_call_owns(call, list) ||
        cl_value_reaches(item, list)) {
        return -1;
    }
    cl_val_list_add(call->ctx, list, item);
    return 0;
}

int cl_call_object_set(cl_call_t *call, cl_value_t *object, const char *key, cl_value_t *value) {
    if (!call || !object || !key || !value || object->kind != CL_VAL_OBJECT || !cl_call_owns(call, object) ||
        cl_value_reaches(value, object)) {
        return -1;
    }
    for (size_t i = 0; i < object->as.object.count; i++) {
        if (strcmp(object->as.object.items[i].key, key) == 0) {
            object->as.object.items[i].value = value;
            return 0;
        }
    }
    cl_val_object_add(call->ctx, object, key, value);
    return 0;
}

cl_value_t *cl_call_copy(cl_call_t *call, const cl_value_t *value) {
    if (!call || !value) {
        return NULL;
    }
    cl_value_t *copy = cl_val_copy(call->ctx, value);
    if (copy->kind == CL_VAL_LIST || copy->kind == CL_VAL_OBJECT) {
        cl_call_own(call, copy); /* a fresh copy is the caller's to extend */
    }
    return copy;
}

cl_value_t *cl_call_error(cl_call_t *call, const char *fmt, ...) {
    if (!call || !fmt) {
        return NULL;
    }
    char message[sizeof(((cl_error_t *)0)->message)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);
    cl_eval_fail(call->ctx, call->line, call->col, "%s(): %s", call->name, message);
    return NULL;
}
