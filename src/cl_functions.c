#include "cl_functions.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static cl_value_t *cl_fn_upper(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    if (argc != 1) {
        cl_eval_fail(ctx, line, col, "upper() espera 1 argumento, recebeu %zu", argc);
        return NULL;
    }
    const char *s = cl_val_require_string(ctx, args[0], line, col);
    if (!s) {
        return NULL;
    }
    char *copy = malloc(strlen(s) + 1);
    if (!copy) {
        abort();
    }
    size_t i = 0;
    for (; s[i]; i++) {
        copy[i] = (char)toupper((unsigned char)s[i]);
    }
    copy[i] = '\0';
    cl_value_t *result = cl_val_string(ctx, copy);
    free(copy);
    return result;
}

static cl_value_t *cl_fn_lower(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    if (argc != 1) {
        cl_eval_fail(ctx, line, col, "lower() espera 1 argumento, recebeu %zu", argc);
        return NULL;
    }
    const char *s = cl_val_require_string(ctx, args[0], line, col);
    if (!s) {
        return NULL;
    }
    char *copy = malloc(strlen(s) + 1);
    if (!copy) {
        abort();
    }
    size_t i = 0;
    for (; s[i]; i++) {
        copy[i] = (char)tolower((unsigned char)s[i]);
    }
    copy[i] = '\0';
    cl_value_t *result = cl_val_string(ctx, copy);
    free(copy);
    return result;
}

static cl_value_t *cl_fn_length(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    if (argc != 1) {
        cl_eval_fail(ctx, line, col, "length() espera 1 argumento, recebeu %zu", argc);
        return NULL;
    }
    switch (args[0]->kind) {
        case CL_VAL_STRING: return cl_val_number(ctx, (double)strlen(args[0]->as.string_value));
        case CL_VAL_LIST: return cl_val_number(ctx, (double)args[0]->as.list.count);
        case CL_VAL_OBJECT: return cl_val_number(ctx, (double)args[0]->as.object.count);
        default:
            cl_eval_fail(ctx, line, col, "length() nao suporta esse tipo de valor");
            return NULL;
    }
}

/* If every argument is a list, concatenates them into one list (matching
 * HCL's own concat()). Otherwise, every argument is coerced to a string
 * (the same coercion template interpolation uses) and joined - this is the
 * shape our own fixtures actually exercise (concat(a, " ", b)). */
static cl_value_t *cl_fn_concat(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    if (argc == 0) {
        cl_eval_fail(ctx, line, col, "concat() espera ao menos 1 argumento");
        return NULL;
    }

    int all_lists = 1;
    for (size_t i = 0; i < argc; i++) {
        if (args[i]->kind != CL_VAL_LIST) {
            all_lists = 0;
            break;
        }
    }

    if (all_lists) {
        cl_value_t *result = cl_val_new_list(ctx);
        for (size_t i = 0; i < argc; i++) {
            for (size_t j = 0; j < args[i]->as.list.count; j++) {
                cl_val_list_add(ctx, result, args[i]->as.list.items[j]);
            }
        }
        return result;
    }

    char *buf = malloc(1);
    if (!buf) {
        abort();
    }
    buf[0] = '\0';
    size_t len = 0;
    for (size_t i = 0; i < argc; i++) {
        const char *s = cl_val_require_string(ctx, args[i], line, col);
        if (!s) {
            free(buf);
            return NULL;
        }
        size_t s_len = strlen(s);
        char *grown = realloc(buf, len + s_len + 1);
        if (!grown) {
            abort();
        }
        buf = grown;
        memcpy(buf + len, s, s_len);
        len += s_len;
        buf[len] = '\0';
    }
    cl_value_t *result = cl_val_string(ctx, buf);
    free(buf);
    return result;
}

typedef struct {
    const char *name;
    cl_builtin_fn_t fn;
} cl_builtin_entry_t;

static const cl_builtin_entry_t cl_builtins[] = {
    {"upper", cl_fn_upper},
    {"lower", cl_fn_lower},
    {"length", cl_fn_length},
    {"concat", cl_fn_concat},
};

cl_builtin_fn_t cl_builtin_lookup(const char *name) {
    size_t count = sizeof(cl_builtins) / sizeof(cl_builtins[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(cl_builtins[i].name, name) == 0) {
            return cl_builtins[i].fn;
        }
    }
    return NULL;
}
