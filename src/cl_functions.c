#include "cl_functions.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* range() refuses to build lists longer than this, so a typo such as
 * range(1e12) is an evaluation error instead of an out-of-memory abort. */
#define CL_FN_RANGE_LIMIT 1048576

/* ------------------------------------------------------------------ */
/* Argument helpers                                                     */
/* ------------------------------------------------------------------ */

/* Every helper takes the function name and a 1-based position so errors
 * read "join() list element 3 must be a string, got list". `what` is
 * "argument" for call arguments, or a description such as "list element". */

static const char *cl_fn_kind_name(cl_value_kind_t kind) {
    switch (kind) {
        case CL_VAL_STRING: return "string";
        case CL_VAL_NUMBER: return "number";
        case CL_VAL_BOOL: return "bool";
        case CL_VAL_NULL: return "null";
        case CL_VAL_LIST: return "list";
        case CL_VAL_OBJECT: return "object";
    }
    return "unknown";
}

/* Strings, numbers and bools coerce to text the same way template
 * interpolation does; null, lists and objects are rejected. */
static const char *cl_fn_string(cl_eval_ctx_t *ctx, const cl_value_t *v, const char *name, const char *what, size_t n,
                                int line, int col) {
    if (v->kind == CL_VAL_NULL || v->kind == CL_VAL_LIST || v->kind == CL_VAL_OBJECT) {
        cl_eval_fail(ctx, line, col, "%s() %s %zu must be a string, got %s", name, what, n, cl_fn_kind_name(v->kind));
        return NULL;
    }
    return cl_val_require_string(ctx, v, line, col);
}

static int cl_fn_number(cl_eval_ctx_t *ctx, const cl_value_t *v, const char *name, const char *what, size_t n,
                        double *out, int line, int col) {
    if (v->kind != CL_VAL_NUMBER) {
        cl_eval_fail(ctx, line, col, "%s() %s %zu must be a number, got %s", name, what, n, cl_fn_kind_name(v->kind));
        return -1;
    }
    *out = v->as.number_value;
    return 0;
}

static int cl_fn_integer(cl_eval_ctx_t *ctx, const cl_value_t *v, const char *name, const char *what, size_t n,
                         double *out, int line, int col) {
    if (cl_fn_number(ctx, v, name, what, n, out, line, col) != 0) {
        return -1;
    }
    if (!isfinite(*out) || floor(*out) != *out) {
        cl_eval_fail(ctx, line, col, "%s() %s %zu must be an integer, got %g", name, what, n, *out);
        return -1;
    }
    return 0;
}

static const cl_value_t *cl_fn_kind(cl_eval_ctx_t *ctx, const cl_value_t *v, cl_value_kind_t kind, const char *name,
                                    const char *what, size_t n, int line, int col) {
    if (v->kind != kind) {
        cl_eval_fail(ctx, line, col, "%s() %s %zu must be %s %s, got %s", name, what, n,
                     kind == CL_VAL_OBJECT ? "an" : "a", cl_fn_kind_name(kind), cl_fn_kind_name(v->kind));
        return NULL;
    }
    return v;
}

/* Growable malloc'd text buffer; always NUL-terminated once non-empty. */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} cl_fn_buf_t;

static void cl_fn_buf_append(cl_fn_buf_t *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 32;
        while (cap < b->len + n + 1) {
            cap *= 2;
        }
        char *grown = realloc(b->data, cap);
        if (!grown) {
            abort();
        }
        b->data = grown;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static cl_value_t *cl_fn_buf_finish(cl_eval_ctx_t *ctx, cl_fn_buf_t *b) {
    cl_value_t *result = cl_val_string_n(ctx, b->data ? b->data : "", b->len);
    free(b->data);
    return result;
}

/* Sets `key` in `obj`, replacing the value in place (keeping the key's
 * original position) when it is already present. */
static void cl_fn_object_set(cl_eval_ctx_t *ctx, cl_value_t *obj, const char *key, cl_value_t *value) {
    for (size_t i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.items[i].key, key) == 0) {
            obj->as.object.items[i].value = value;
            return;
        }
    }
    cl_val_object_add(ctx, obj, key, value);
}

/* ------------------------------------------------------------------ */
/* Strings                                                              */
/* ------------------------------------------------------------------ */

static cl_value_t *cl_fn_map_chars(cl_eval_ctx_t *ctx, cl_value_t **args, const char *name, int (*map)(int), int line,
                                   int col) {
    const char *s = cl_fn_string(ctx, args[0], name, "argument", 1, line, col);
    if (!s) {
        return NULL;
    }
    size_t len = strlen(s);
    cl_value_t *result = cl_val_string_n(ctx, s, len);
    for (size_t i = 0; i < len; i++) {
        result->as.string_value[i] = (char)map((unsigned char)s[i]);
    }
    return result;
}

static cl_value_t *cl_fn_upper(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    return cl_fn_map_chars(ctx, args, "upper", toupper, line, col);
}

static cl_value_t *cl_fn_lower(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    return cl_fn_map_chars(ctx, args, "lower", tolower, line, col);
}

static cl_value_t *cl_fn_length(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    switch (args[0]->kind) {
        case CL_VAL_STRING: return cl_val_number(ctx, (double)strlen(args[0]->as.string_value));
        case CL_VAL_LIST: return cl_val_number(ctx, (double)args[0]->as.list.count);
        case CL_VAL_OBJECT: return cl_val_number(ctx, (double)args[0]->as.object.count);
        default:
            cl_eval_fail(ctx, line, col, "length() argument 1 must be a string, list or object, got %s",
                         cl_fn_kind_name(args[0]->kind));
            return NULL;
    }
}

/* If every argument is a list, concatenates them into one list (matching
 * HCL's own concat()). Otherwise, every argument is coerced to a string
 * (the same coercion template interpolation uses) and joined - this is the
 * shape our own fixtures actually exercise (concat(a, " ", b)). */
static cl_value_t *cl_fn_concat(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
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

    cl_fn_buf_t buf = {0};
    for (size_t i = 0; i < argc; i++) {
        const char *s = cl_fn_string(ctx, args[i], "concat", "argument", i + 1, line, col);
        if (!s) {
            free(buf.data);
            return NULL;
        }
        cl_fn_buf_append(&buf, s, strlen(s));
    }
    return cl_fn_buf_finish(ctx, &buf);
}

static int cl_fn_in_cutset(char c, const char *cutset) {
    return c != '\0' && strchr(cutset, c) != NULL;
}

/* trim(s, cutset): removes every leading/trailing character found in cutset. */
static cl_value_t *cl_fn_trim(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "trim", "argument", 1, line, col);
    const char *cutset = s ? cl_fn_string(ctx, args[1], "trim", "argument", 2, line, col) : NULL;
    if (!cutset) {
        return NULL;
    }
    size_t start = 0;
    size_t end = strlen(s);
    while (start < end && cl_fn_in_cutset(s[start], cutset)) {
        start++;
    }
    while (end > start && cl_fn_in_cutset(s[end - 1], cutset)) {
        end--;
    }
    return cl_val_string_n(ctx, s + start, end - start);
}

static cl_value_t *cl_fn_trimspace(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "trimspace", "argument", 1, line, col);
    if (!s) {
        return NULL;
    }
    size_t start = 0;
    size_t end = strlen(s);
    while (start < end && isspace((unsigned char)s[start])) {
        start++;
    }
    while (end > start && isspace((unsigned char)s[end - 1])) {
        end--;
    }
    return cl_val_string_n(ctx, s + start, end - start);
}

static cl_value_t *cl_fn_trimprefix(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "trimprefix", "argument", 1, line, col);
    const char *prefix = s ? cl_fn_string(ctx, args[1], "trimprefix", "argument", 2, line, col) : NULL;
    if (!prefix) {
        return NULL;
    }
    size_t plen = strlen(prefix);
    return cl_val_string(ctx, strncmp(s, prefix, plen) == 0 ? s + plen : s);
}

static cl_value_t *cl_fn_trimsuffix(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "trimsuffix", "argument", 1, line, col);
    const char *suffix = s ? cl_fn_string(ctx, args[1], "trimsuffix", "argument", 2, line, col) : NULL;
    if (!suffix) {
        return NULL;
    }
    size_t len = strlen(s);
    size_t slen = strlen(suffix);
    if (slen <= len && strcmp(s + len - slen, suffix) == 0) {
        len -= slen;
    }
    return cl_val_string_n(ctx, s, len);
}

/* replace(s, search, replacement): replaces every literal occurrence. */
static cl_value_t *cl_fn_replace(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "replace", "argument", 1, line, col);
    const char *search = s ? cl_fn_string(ctx, args[1], "replace", "argument", 2, line, col) : NULL;
    const char *repl = search ? cl_fn_string(ctx, args[2], "replace", "argument", 3, line, col) : NULL;
    if (!repl) {
        return NULL;
    }
    size_t search_len = strlen(search);
    if (search_len == 0) {
        cl_eval_fail(ctx, line, col, "replace() argument 2 must not be empty");
        return NULL;
    }
    size_t repl_len = strlen(repl);
    cl_fn_buf_t buf = {0};
    const char *p = s;
    const char *hit;
    while ((hit = strstr(p, search)) != NULL) {
        cl_fn_buf_append(&buf, p, (size_t)(hit - p));
        cl_fn_buf_append(&buf, repl, repl_len);
        p = hit + search_len;
    }
    cl_fn_buf_append(&buf, p, strlen(p));
    return cl_fn_buf_finish(ctx, &buf);
}

/* split(separator, s): argument order follows HCL. */
static cl_value_t *cl_fn_split(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *sep = cl_fn_string(ctx, args[0], "split", "argument", 1, line, col);
    const char *s = sep ? cl_fn_string(ctx, args[1], "split", "argument", 2, line, col) : NULL;
    if (!s) {
        return NULL;
    }
    size_t sep_len = strlen(sep);
    if (sep_len == 0) {
        cl_eval_fail(ctx, line, col, "split() argument 1 must not be empty");
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    const char *p = s;
    const char *hit;
    while ((hit = strstr(p, sep)) != NULL) {
        cl_val_list_add(ctx, result, cl_val_string_n(ctx, p, (size_t)(hit - p)));
        p = hit + sep_len;
    }
    cl_val_list_add(ctx, result, cl_val_string(ctx, p));
    return result;
}

/* join(separator, list): argument order follows HCL. */
static cl_value_t *cl_fn_join(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *sep = cl_fn_string(ctx, args[0], "join", "argument", 1, line, col);
    const cl_value_t *list = sep ? cl_fn_kind(ctx, args[1], CL_VAL_LIST, "join", "argument", 2, line, col) : NULL;
    if (!list) {
        return NULL;
    }
    size_t sep_len = strlen(sep);
    cl_fn_buf_t buf = {0};
    for (size_t i = 0; i < list->as.list.count; i++) {
        const char *item = cl_fn_string(ctx, list->as.list.items[i], "join", "list element", i + 1, line, col);
        if (!item) {
            free(buf.data);
            return NULL;
        }
        if (i > 0) {
            cl_fn_buf_append(&buf, sep, sep_len);
        }
        cl_fn_buf_append(&buf, item, strlen(item));
    }
    return cl_fn_buf_finish(ctx, &buf);
}

/* substr(s, offset, length): byte-based, like length(). A negative offset
 * counts from the end; length -1 means "up to the end". */
static cl_value_t *cl_fn_substr(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    double offset;
    double count;
    const char *s = cl_fn_string(ctx, args[0], "substr", "argument", 1, line, col);
    if (!s || cl_fn_integer(ctx, args[1], "substr", "argument", 2, &offset, line, col) != 0 ||
        cl_fn_integer(ctx, args[2], "substr", "argument", 3, &count, line, col) != 0) {
        return NULL;
    }
    size_t len = strlen(s);
    if (offset < 0) {
        offset += (double)len;
    }
    if (offset < 0 || offset > (double)len) {
        cl_eval_fail(ctx, line, col, "substr() offset %g is out of range for a string of length %zu",
                     args[1]->as.number_value, len);
        return NULL;
    }
    if (count < -1) {
        cl_eval_fail(ctx, line, col, "substr() length must be -1 or non-negative, got %g", count);
        return NULL;
    }
    size_t start = (size_t)offset;
    size_t avail = len - start;
    size_t n = (count < 0 || count > (double)avail) ? avail : (size_t)count;
    return cl_val_string_n(ctx, s + start, n);
}

static cl_value_t *cl_fn_startswith(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "startswith", "argument", 1, line, col);
    const char *prefix = s ? cl_fn_string(ctx, args[1], "startswith", "argument", 2, line, col) : NULL;
    if (!prefix) {
        return NULL;
    }
    return cl_val_bool(ctx, strncmp(s, prefix, strlen(prefix)) == 0);
}

static cl_value_t *cl_fn_endswith(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "endswith", "argument", 1, line, col);
    const char *suffix = s ? cl_fn_string(ctx, args[1], "endswith", "argument", 2, line, col) : NULL;
    if (!suffix) {
        return NULL;
    }
    size_t len = strlen(s);
    size_t slen = strlen(suffix);
    return cl_val_bool(ctx, slen <= len && strcmp(s + len - slen, suffix) == 0);
}

static cl_value_t *cl_fn_strcontains(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const char *s = cl_fn_string(ctx, args[0], "strcontains", "argument", 1, line, col);
    const char *sub = s ? cl_fn_string(ctx, args[1], "strcontains", "argument", 2, line, col) : NULL;
    if (!sub) {
        return NULL;
    }
    return cl_val_bool(ctx, strstr(s, sub) != NULL);
}

/* format(fmt, args...): supports %s (any string-coercible value), %d
 * (integer), %f / %.Nf (number) and %% (a literal percent sign). */
static cl_value_t *cl_fn_format(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    const char *fmt = cl_fn_string(ctx, args[0], "format", "argument", 1, line, col);
    if (!fmt) {
        return NULL;
    }
    cl_fn_buf_t buf = {0};
    size_t next = 1;
    const char *p = fmt;
    while (*p) {
        const char *pct = strchr(p, '%');
        if (!pct) {
            cl_fn_buf_append(&buf, p, strlen(p));
            break;
        }
        cl_fn_buf_append(&buf, p, (size_t)(pct - p));
        p = pct + 1;
        if (*p == '%') {
            cl_fn_buf_append(&buf, "%", 1);
            p++;
            continue;
        }

        int precision = -1;
        if (*p == '.') {
            p++;
            if (!isdigit((unsigned char)*p)) {
                cl_eval_fail(ctx, line, col, "format() expects digits after '%%.'");
                goto fail;
            }
            precision = 0;
            while (isdigit((unsigned char)*p)) {
                precision = precision * 10 + (*p - '0');
                if (precision > 100) {
                    cl_eval_fail(ctx, line, col, "format() precision must be at most 100");
                    goto fail;
                }
                p++;
            }
        }

        char verb = *p;
        if (verb == '\0') {
            cl_eval_fail(ctx, line, col, "format() string ends with an incomplete '%%' verb");
            goto fail;
        }
        p++;
        if (verb != 's' && verb != 'd' && verb != 'f') {
            cl_eval_fail(ctx, line, col, "format() does not support the verb '%%%c'", verb);
            goto fail;
        }
        if (precision >= 0 && verb != 'f') {
            cl_eval_fail(ctx, line, col, "format() precision is only valid with '%%f'");
            goto fail;
        }
        if (next >= argc) {
            cl_eval_fail(ctx, line, col, "format() has more verbs than arguments");
            goto fail;
        }

        const cl_value_t *arg = args[next];
        size_t n = ++next; /* 1-based position of arg */
        char tmp[512];     /* fits any finite double with precision <= 100 */
        double num;
        switch (verb) {
            case 's': {
                const char *s = cl_fn_string(ctx, arg, "format", "argument", n, line, col);
                if (!s) {
                    goto fail;
                }
                cl_fn_buf_append(&buf, s, strlen(s));
                break;
            }
            case 'd':
                if (cl_fn_integer(ctx, arg, "format", "argument", n, &num, line, col) != 0) {
                    goto fail;
                }
                snprintf(tmp, sizeof(tmp), "%.0f", num);
                cl_fn_buf_append(&buf, tmp, strlen(tmp));
                break;
            default: /* 'f' */
                if (cl_fn_number(ctx, arg, "format", "argument", n, &num, line, col) != 0) {
                    goto fail;
                }
                if (!isfinite(num)) {
                    cl_eval_fail(ctx, line, col, "format() argument %zu must be a finite number", n);
                    goto fail;
                }
                snprintf(tmp, sizeof(tmp), "%.*f", precision < 0 ? 6 : precision, num);
                cl_fn_buf_append(&buf, tmp, strlen(tmp));
                break;
        }
    }
    if (next < argc) {
        cl_eval_fail(ctx, line, col, "format() has %zu more argument(s) than verbs", argc - next);
        goto fail;
    }
    return cl_fn_buf_finish(ctx, &buf);

fail:
    free(buf.data);
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Numbers                                                              */
/* ------------------------------------------------------------------ */

static cl_value_t *cl_fn_extreme(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, const char *name, int want_max,
                                 int line, int col) {
    double best = 0;
    for (size_t i = 0; i < argc; i++) {
        double n;
        if (cl_fn_number(ctx, args[i], name, "argument", i + 1, &n, line, col) != 0) {
            return NULL;
        }
        if (i == 0 || (want_max ? n > best : n < best)) {
            best = n;
        }
    }
    return cl_val_number(ctx, best);
}

static cl_value_t *cl_fn_min(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    return cl_fn_extreme(ctx, args, argc, "min", 0, line, col);
}

static cl_value_t *cl_fn_max(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    return cl_fn_extreme(ctx, args, argc, "max", 1, line, col);
}

static cl_value_t *cl_fn_unary_math(cl_eval_ctx_t *ctx, cl_value_t **args, const char *name, double (*op)(double),
                                    int line, int col) {
    double n;
    if (cl_fn_number(ctx, args[0], name, "argument", 1, &n, line, col) != 0) {
        return NULL;
    }
    return cl_val_number(ctx, op(n));
}

static cl_value_t *cl_fn_abs(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    return cl_fn_unary_math(ctx, args, "abs", fabs, line, col);
}

static cl_value_t *cl_fn_floor(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    return cl_fn_unary_math(ctx, args, "floor", floor, line, col);
}

static cl_value_t *cl_fn_ceil(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    return cl_fn_unary_math(ctx, args, "ceil", ceil, line, col);
}

/* Rounds half away from zero (round(2.5) == 3, round(-2.5) == -3). */
static cl_value_t *cl_fn_round(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    return cl_fn_unary_math(ctx, args, "round", round, line, col);
}

static cl_value_t *cl_fn_pow(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    double base;
    double exp;
    if (cl_fn_number(ctx, args[0], "pow", "argument", 1, &base, line, col) != 0 ||
        cl_fn_number(ctx, args[1], "pow", "argument", 2, &exp, line, col) != 0) {
        return NULL;
    }
    double result = pow(base, exp);
    if (isnan(result)) {
        cl_eval_fail(ctx, line, col, "pow(%g, %g) is not a real number", base, exp);
        return NULL;
    }
    return cl_val_number(ctx, result);
}

/* parseint(s, base): base between 2 and 36, the whole string must parse. */
static cl_value_t *cl_fn_parseint(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    double base;
    const char *s = cl_fn_string(ctx, args[0], "parseint", "argument", 1, line, col);
    if (!s || cl_fn_integer(ctx, args[1], "parseint", "argument", 2, &base, line, col) != 0) {
        return NULL;
    }
    if (base < 2 || base > 36) {
        cl_eval_fail(ctx, line, col, "parseint() base must be between 2 and 36, got %g", base);
        return NULL;
    }
    char *end = NULL;
    errno = 0;
    long long value = s[0] && !isspace((unsigned char)s[0]) ? strtoll(s, &end, (int)base) : 0;
    if (!end || end == s || *end != '\0') {
        cl_eval_fail(ctx, line, col, "parseint() cannot parse \"%s\" as a base %g integer", s, base);
        return NULL;
    }
    if (errno == ERANGE) {
        cl_eval_fail(ctx, line, col, "parseint() value \"%s\" is out of range", s);
        return NULL;
    }
    return cl_val_number(ctx, (double)value);
}

/* ------------------------------------------------------------------ */
/* Collections                                                          */
/* ------------------------------------------------------------------ */

/* keys(obj) / values(obj): in the object's own (declaration) order. */
static cl_value_t *cl_fn_keys(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *obj = cl_fn_kind(ctx, args[0], CL_VAL_OBJECT, "keys", "argument", 1, line, col);
    if (!obj) {
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    for (size_t i = 0; i < obj->as.object.count; i++) {
        cl_val_list_add(ctx, result, cl_val_string(ctx, obj->as.object.items[i].key));
    }
    return result;
}

static cl_value_t *cl_fn_values(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *obj = cl_fn_kind(ctx, args[0], CL_VAL_OBJECT, "values", "argument", 1, line, col);
    if (!obj) {
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    for (size_t i = 0; i < obj->as.object.count; i++) {
        cl_val_list_add(ctx, result, obj->as.object.items[i].value);
    }
    return result;
}

/* lookup(obj, key[, default]): a missing key without a default is an error. */
static cl_value_t *cl_fn_lookup(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    const cl_value_t *obj = cl_fn_kind(ctx, args[0], CL_VAL_OBJECT, "lookup", "argument", 1, line, col);
    const char *key = obj ? cl_fn_string(ctx, args[1], "lookup", "argument", 2, line, col) : NULL;
    if (!key) {
        return NULL;
    }
    cl_value_t *found = cl_value_object_get(obj, key);
    if (found) {
        return found;
    }
    if (argc == 3) {
        return args[2];
    }
    cl_eval_fail(ctx, line, col, "lookup() key '%s' not found", key);
    return NULL;
}

/* merge(objs...): later objects win; a key keeps its first position. */
static cl_value_t *cl_fn_merge(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    cl_value_t *result = cl_val_new_object(ctx);
    for (size_t i = 0; i < argc; i++) {
        const cl_value_t *obj = cl_fn_kind(ctx, args[i], CL_VAL_OBJECT, "merge", "argument", i + 1, line, col);
        if (!obj) {
            return NULL;
        }
        for (size_t j = 0; j < obj->as.object.count; j++) {
            cl_fn_object_set(ctx, result, obj->as.object.items[j].key, obj->as.object.items[j].value);
        }
    }
    return result;
}

static cl_value_t *cl_fn_contains(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *list = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "contains", "argument", 1, line, col);
    if (!list) {
        return NULL;
    }
    for (size_t i = 0; i < list->as.list.count; i++) {
        if (cl_value_equal(list->as.list.items[i], args[1])) {
            return cl_val_bool(ctx, 1);
        }
    }
    return cl_val_bool(ctx, 0);
}

/* element(list, index): wraps around, so element(l, length(l)) == l[0]. */
static cl_value_t *cl_fn_element(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    double index;
    const cl_value_t *list = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "element", "argument", 1, line, col);
    if (!list || cl_fn_integer(ctx, args[1], "element", "argument", 2, &index, line, col) != 0) {
        return NULL;
    }
    if (list->as.list.count == 0) {
        cl_eval_fail(ctx, line, col, "element() cannot pick from an empty list");
        return NULL;
    }
    if (index < 0) {
        cl_eval_fail(ctx, line, col, "element() index must not be negative, got %g", index);
        return NULL;
    }
    return list->as.list.items[(size_t)fmod(index, (double)list->as.list.count)];
}

/* slice(list, start, end): items from start (inclusive) to end (exclusive). */
static cl_value_t *cl_fn_slice(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    double start;
    double end;
    const cl_value_t *list = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "slice", "argument", 1, line, col);
    if (!list || cl_fn_integer(ctx, args[1], "slice", "argument", 2, &start, line, col) != 0 ||
        cl_fn_integer(ctx, args[2], "slice", "argument", 3, &end, line, col) != 0) {
        return NULL;
    }
    if (start < 0 || end < start || end > (double)list->as.list.count) {
        cl_eval_fail(ctx, line, col, "slice() range [%g, %g) is out of bounds for a list of %zu items", start, end,
                     list->as.list.count);
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    for (size_t i = (size_t)start; i < (size_t)end; i++) {
        cl_val_list_add(ctx, result, list->as.list.items[i]);
    }
    return result;
}

static cl_value_t *cl_fn_reverse(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *list = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "reverse", "argument", 1, line, col);
    if (!list) {
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    for (size_t i = list->as.list.count; i > 0; i--) {
        cl_val_list_add(ctx, result, list->as.list.items[i - 1]);
    }
    return result;
}

/* distinct(list): drops later duplicates, keeping first occurrences in order. */
static cl_value_t *cl_fn_distinct(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *list = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "distinct", "argument", 1, line, col);
    if (!list) {
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    for (size_t i = 0; i < list->as.list.count; i++) {
        int seen = 0;
        for (size_t j = 0; j < result->as.list.count && !seen; j++) {
            seen = cl_value_equal(result->as.list.items[j], list->as.list.items[i]);
        }
        if (!seen) {
            cl_val_list_add(ctx, result, list->as.list.items[i]);
        }
    }
    return result;
}

static void cl_fn_flatten_into(cl_eval_ctx_t *ctx, cl_value_t *out, const cl_value_t *list) {
    for (size_t i = 0; i < list->as.list.count; i++) {
        cl_value_t *item = list->as.list.items[i];
        if (item->kind == CL_VAL_LIST) {
            cl_fn_flatten_into(ctx, out, item);
        } else {
            cl_val_list_add(ctx, out, item);
        }
    }
}

/* flatten(list): flattens nested lists at any depth. */
static cl_value_t *cl_fn_flatten(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *list = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "flatten", "argument", 1, line, col);
    if (!list) {
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    cl_fn_flatten_into(ctx, result, list);
    return result;
}

/* range(end) / range(start, end) / range(start, end, step): end is
 * exclusive. Without a step it is 1, or -1 when start > end. */
static cl_value_t *cl_fn_range(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    double nums[3];
    for (size_t i = 0; i < argc; i++) {
        if (cl_fn_number(ctx, args[i], "range", "argument", i + 1, &nums[i], line, col) != 0) {
            return NULL;
        }
        if (!isfinite(nums[i])) {
            cl_eval_fail(ctx, line, col, "range() argument %zu must be a finite number", i + 1);
            return NULL;
        }
    }
    double start = argc == 1 ? 0 : nums[0];
    double end = argc == 1 ? nums[0] : nums[1];
    double step = argc == 3 ? nums[2] : (start <= end ? 1 : -1);
    if (step == 0) {
        cl_eval_fail(ctx, line, col, "range() step must not be zero");
        return NULL;
    }
    double span = ceil((end - start) / step);
    double count = span > 0 ? span : 0;
    if (count > CL_FN_RANGE_LIMIT) {
        cl_eval_fail(ctx, line, col, "range() would produce more than %d items", CL_FN_RANGE_LIMIT);
        return NULL;
    }
    cl_value_t *result = cl_val_new_list(ctx);
    for (size_t i = 0; i < (size_t)count; i++) {
        cl_val_list_add(ctx, result, cl_val_number(ctx, start + (double)i * step));
    }
    return result;
}

/* zipmap(keys, values): builds an object; a repeated key keeps its last value. */
static cl_value_t *cl_fn_zipmap(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    const cl_value_t *keys = cl_fn_kind(ctx, args[0], CL_VAL_LIST, "zipmap", "argument", 1, line, col);
    const cl_value_t *values = keys ? cl_fn_kind(ctx, args[1], CL_VAL_LIST, "zipmap", "argument", 2, line, col) : NULL;
    if (!values) {
        return NULL;
    }
    if (keys->as.list.count != values->as.list.count) {
        cl_eval_fail(ctx, line, col, "zipmap() got %zu keys but %zu values", keys->as.list.count,
                     values->as.list.count);
        return NULL;
    }
    cl_value_t *result = cl_val_new_object(ctx);
    for (size_t i = 0; i < keys->as.list.count; i++) {
        const char *key = cl_fn_string(ctx, keys->as.list.items[i], "zipmap", "key", i + 1, line, col);
        if (!key) {
            return NULL;
        }
        cl_fn_object_set(ctx, result, key, values->as.list.items[i]);
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* Types and conversion                                                 */
/* ------------------------------------------------------------------ */

static cl_value_t *cl_fn_type(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    (void)line;
    (void)col;
    return cl_val_string(ctx, cl_fn_kind_name(args[0]->kind));
}

/* The to*() conversions pass null through unchanged. */
static cl_value_t *cl_fn_tostring(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    if (args[0]->kind == CL_VAL_NULL || args[0]->kind == CL_VAL_STRING) {
        return args[0];
    }
    const char *s = cl_fn_string(ctx, args[0], "tostring", "argument", 1, line, col);
    return s ? cl_val_string(ctx, s) : NULL;
}

static cl_value_t *cl_fn_tonumber(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    cl_value_t *v = args[0];
    if (v->kind == CL_VAL_NULL || v->kind == CL_VAL_NUMBER) {
        return v;
    }
    if (v->kind != CL_VAL_STRING) {
        cl_eval_fail(ctx, line, col, "tonumber() cannot convert a %s to a number", cl_fn_kind_name(v->kind));
        return NULL;
    }
    const char *s = v->as.string_value;
    char *end = NULL;
    double n = s[0] && !isspace((unsigned char)s[0]) ? strtod(s, &end) : 0;
    if (!end || end == s || *end != '\0' || !isfinite(n)) {
        cl_eval_fail(ctx, line, col, "tonumber() cannot convert \"%s\" to a number", s);
        return NULL;
    }
    return cl_val_number(ctx, n);
}

static cl_value_t *cl_fn_tobool(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    (void)argc;
    cl_value_t *v = args[0];
    if (v->kind == CL_VAL_NULL || v->kind == CL_VAL_BOOL) {
        return v;
    }
    if (v->kind == CL_VAL_STRING) {
        if (strcmp(v->as.string_value, "true") == 0) {
            return cl_val_bool(ctx, 1);
        }
        if (strcmp(v->as.string_value, "false") == 0) {
            return cl_val_bool(ctx, 0);
        }
        cl_eval_fail(ctx, line, col, "tobool() cannot convert \"%s\" to a bool", v->as.string_value);
        return NULL;
    }
    cl_eval_fail(ctx, line, col, "tobool() cannot convert a %s to a bool", cl_fn_kind_name(v->kind));
    return NULL;
}

/* coalesce(values...): the first non-null argument. */
static cl_value_t *cl_fn_coalesce(cl_eval_ctx_t *ctx, cl_value_t **args, size_t argc, int line, int col) {
    for (size_t i = 0; i < argc; i++) {
        if (args[i]->kind != CL_VAL_NULL) {
            return args[i];
        }
    }
    cl_eval_fail(ctx, line, col, "coalesce() got only null arguments");
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Registry                                                             */
/* ------------------------------------------------------------------ */

static const cl_builtin_t cl_builtins[] = {
    /* strings */
    {"upper", 1, 1, cl_fn_upper},
    {"lower", 1, 1, cl_fn_lower},
    {"length", 1, 1, cl_fn_length},
    {"concat", 1, CL_FUNCTION_VARIADIC, cl_fn_concat},
    {"trim", 2, 2, cl_fn_trim},
    {"trimspace", 1, 1, cl_fn_trimspace},
    {"trimprefix", 2, 2, cl_fn_trimprefix},
    {"trimsuffix", 2, 2, cl_fn_trimsuffix},
    {"replace", 3, 3, cl_fn_replace},
    {"split", 2, 2, cl_fn_split},
    {"join", 2, 2, cl_fn_join},
    {"substr", 3, 3, cl_fn_substr},
    {"startswith", 2, 2, cl_fn_startswith},
    {"endswith", 2, 2, cl_fn_endswith},
    {"strcontains", 2, 2, cl_fn_strcontains},
    {"format", 1, CL_FUNCTION_VARIADIC, cl_fn_format},
    /* numbers */
    {"min", 1, CL_FUNCTION_VARIADIC, cl_fn_min},
    {"max", 1, CL_FUNCTION_VARIADIC, cl_fn_max},
    {"abs", 1, 1, cl_fn_abs},
    {"floor", 1, 1, cl_fn_floor},
    {"ceil", 1, 1, cl_fn_ceil},
    {"round", 1, 1, cl_fn_round},
    {"pow", 2, 2, cl_fn_pow},
    {"parseint", 2, 2, cl_fn_parseint},
    /* collections */
    {"keys", 1, 1, cl_fn_keys},
    {"values", 1, 1, cl_fn_values},
    {"lookup", 2, 3, cl_fn_lookup},
    {"merge", 1, CL_FUNCTION_VARIADIC, cl_fn_merge},
    {"contains", 2, 2, cl_fn_contains},
    {"element", 2, 2, cl_fn_element},
    {"slice", 3, 3, cl_fn_slice},
    {"reverse", 1, 1, cl_fn_reverse},
    {"distinct", 1, 1, cl_fn_distinct},
    {"flatten", 1, 1, cl_fn_flatten},
    {"range", 1, 3, cl_fn_range},
    {"zipmap", 2, 2, cl_fn_zipmap},
    /* types and conversion */
    {"type", 1, 1, cl_fn_type},
    {"tostring", 1, 1, cl_fn_tostring},
    {"tonumber", 1, 1, cl_fn_tonumber},
    {"tobool", 1, 1, cl_fn_tobool},
    {"coalesce", 1, CL_FUNCTION_VARIADIC, cl_fn_coalesce},
};

const cl_builtin_t *cl_builtin_lookup(const char *name) {
    size_t count = sizeof(cl_builtins) / sizeof(cl_builtins[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(cl_builtins[i].name, name) == 0) {
            return &cl_builtins[i];
        }
    }
    return NULL;
}

void cl_function_arity_fail(cl_eval_ctx_t *ctx, const char *name, size_t min_args, size_t max_args, size_t argc,
                            int line, int col) {
    const char *plural = min_args == 1 ? "" : "s";
    if (max_args == CL_FUNCTION_VARIADIC) {
        cl_eval_fail(ctx, line, col, "%s() expects at least %zu argument%s, got %zu", name, min_args, plural, argc);
    } else if (min_args == max_args) {
        cl_eval_fail(ctx, line, col, "%s() expects %zu argument%s, got %zu", name, min_args, plural, argc);
    } else {
        cl_eval_fail(ctx, line, col, "%s() expects %zu to %zu arguments, got %zu", name, min_args, max_args, argc);
    }
}
