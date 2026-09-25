#include "cl/cl.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Path parsing: a cursor over [p, end), read one part at a time        */
/* ------------------------------------------------------------------ */

typedef enum cl_part_kind {
    CL_PART_NAME, /* .name or ["name"] */
    CL_PART_INDEX /* [n] */
} cl_part_kind_t;

typedef struct cl_part {
    cl_part_kind_t kind;
    const char *name; /* not NUL-terminated: points into the path */
    size_t len;
    size_t index;
} cl_part_t;

typedef struct cl_path_cursor {
    const char *p;
    const char *end;
    int first;
} cl_path_cursor_t;

/* Reads the next part into *out. Returns 1 when a part was read, 0 at the
 * end of the path, -1 on a malformed path (an empty path included). */
static int cl_path_next(cl_path_cursor_t *c, cl_part_t *out) {
    if (c->p == c->end) {
        return c->first ? -1 : 0;
    }
    int first = c->first;
    c->first = 0;

    if (*c->p == '[') {
        c->p++;
        if (c->p < c->end && *c->p == '"') {
            const char *start = ++c->p;
            while (c->p < c->end && *c->p != '"') {
                c->p++;
            }
            if (c->p == c->end) {
                return -1;
            }
            out->kind = CL_PART_NAME;
            out->name = start;
            out->len = (size_t)(c->p - start);
            c->p++;
        } else {
            const char *start = c->p;
            size_t n = 0;
            while (c->p < c->end && *c->p >= '0' && *c->p <= '9') {
                size_t digit = (size_t)(*c->p - '0');
                if (n > (SIZE_MAX - digit) / 10) {
                    return -1;
                }
                n = n * 10 + digit;
                c->p++;
            }
            if (c->p == start) {
                return -1;
            }
            out->kind = CL_PART_INDEX;
            out->index = n;
        }
        if (c->p == c->end || *c->p != ']') {
            return -1;
        }
        c->p++;
        return 1;
    }

    if (!first) {
        if (*c->p != '.') {
            return -1;
        }
        c->p++;
    }
    const char *start = c->p;
    while (c->p < c->end && *c->p != '.' && *c->p != '[') {
        c->p++;
    }
    if (c->p == start) {
        return -1;
    }
    out->kind = CL_PART_NAME;
    out->name = start;
    out->len = (size_t)(c->p - start);
    return 1;
}

/* True when the NUL-terminated `s` is exactly the `len` bytes at `name`. */
static int cl_name_eq(const char *s, const char *name, size_t len) {
    return strncmp(s, name, len) == 0 && s[len] == '\0';
}

/* ------------------------------------------------------------------ */
/* Resolution                                                           */
/* ------------------------------------------------------------------ */

static const cl_evaluated_attribute_t *cl_get_find_attribute(const cl_evaluated_body_t *body,
                                                              const cl_part_t *part) {
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_ATTRIBUTE && cl_name_eq(item->as.attribute->name, part->name, part->len)) {
            return item->as.attribute;
        }
    }
    return NULL;
}

/* True when the next `n` parts read from *probe are names equal to the
 * block's labels, in order. */
static int cl_get_labels_match(const cl_evaluated_block_t *block, cl_path_cursor_t *probe, size_t n) {
    for (size_t i = 0; i < n; i++) {
        cl_part_t part = {CL_PART_NAME, NULL, 0, 0};
        if (cl_path_next(probe, &part) != 1 || part.kind != CL_PART_NAME ||
            !cl_name_eq(block->labels[i], part.name, part.len)) {
            return 0;
        }
    }
    return 1;
}

/* Finds a block typed `type` in `body` whose labels are the next parts of
 * the path, trying the longest label count first - the same rule as
 * traversal resolution during evaluation. On a match the cursor moves past
 * the consumed labels. */
static const cl_evaluated_block_t *cl_get_find_block(const cl_evaluated_body_t *body, const cl_part_t *type,
                                                      cl_path_cursor_t *c) {
    size_t max_labels = 0;
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && cl_name_eq(item->as.block->type, type->name, type->len) &&
            item->as.block->label_count > max_labels) {
            max_labels = item->as.block->label_count;
        }
    }
    for (size_t n = max_labels + 1; n-- > 0;) {
        for (size_t i = 0; i < body->count; i++) {
            const cl_evaluated_item_t *item = &body->items[i];
            if (item->kind != CL_ITEM_BLOCK || item->as.block->label_count != n ||
                !cl_name_eq(item->as.block->type, type->name, type->len)) {
                continue;
            }
            cl_path_cursor_t probe = *c;
            if (cl_get_labels_match(item->as.block, &probe, n)) {
                *c = probe;
                return item->as.block;
            }
        }
    }
    return NULL;
}

static const cl_value_t *cl_get_step(const cl_value_t *value, const cl_part_t *part) {
    if (part->kind == CL_PART_INDEX) {
        if (value->kind != CL_VAL_LIST || part->index >= value->as.list.count) {
            return NULL;
        }
        return value->as.list.items[part->index];
    }
    if (value->kind != CL_VAL_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < value->as.object.count; i++) {
        if (cl_name_eq(value->as.object.items[i].key, part->name, part->len)) {
            return value->as.object.items[i].value;
        }
    }
    return NULL;
}

typedef struct cl_resolved {
    const cl_evaluated_block_t *block; /* the path names a block */
    const cl_value_t *value;           /* the path names a value */
} cl_resolved_t;

/* Resolves the path [path, end) from `body`. Returns 0 when it names
 * nothing or is malformed, 1 otherwise (with exactly one of out->block and
 * out->value set). */
static int cl_get_resolve(const cl_evaluated_body_t *body, const char *path, const char *end, cl_resolved_t *out) {
    out->block = NULL;
    out->value = NULL;
    if (!body || !path) {
        return 0;
    }
    cl_path_cursor_t c = {path, end, 1};
    const cl_evaluated_block_t *block = NULL;
    const cl_value_t *value = NULL;
    cl_part_t part = {CL_PART_NAME, NULL, 0, 0};
    int r;
    while ((r = cl_path_next(&c, &part)) == 1) {
        if (value) {
            value = cl_get_step(value, &part);
            if (!value) {
                return 0;
            }
            continue;
        }
        if (part.kind != CL_PART_NAME) {
            return 0; /* a body has no list items to index */
        }
        const cl_evaluated_attribute_t *attr = cl_get_find_attribute(body, &part);
        if (attr) {
            value = attr->value;
            continue;
        }
        block = cl_get_find_block(body, &part, &c);
        if (!block) {
            return 0;
        }
        body = block->body;
    }
    if (r < 0) {
        return 0;
    }
    if (value) {
        out->value = value;
    } else {
        out->block = block;
    }
    return 1;
}

static int cl_get_resolve_path(const cl_evaluated_body_t *base, const char *path, cl_resolved_t *out) {
    if (!path) {
        out->block = NULL;
        out->value = NULL;
        return 0;
    }
    return cl_get_resolve(base, path, path + strlen(path), out);
}

/* The value the whole path names, or NULL when it names nothing, a block,
 * or a null value. */
static const cl_value_t *cl_get_defined(const cl_evaluated_body_t *base, const char *path) {
    cl_resolved_t r;
    if (!cl_get_resolve_path(base, path, &r) || !r.value || r.value->kind == CL_VAL_NULL) {
        return NULL;
    }
    return r.value;
}

/* True when `v` is an integral number that fits in a long. */
static int cl_get_as_long(const cl_value_t *v, long *out) {
    if (!v || v->kind != CL_VAL_NUMBER) {
        return 0;
    }
    double x = v->as.number_value;
    /* LONG_MIN is a power of two, so both bounds are exact doubles; NaN
     * fails both comparisons. */
    if (!(x >= (double)LONG_MIN && x < -(double)LONG_MIN)) {
        return 0;
    }
    long n = (long)x;
    if ((double)n != x) {
        return 0;
    }
    *out = n;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Public getters                                                       */
/* ------------------------------------------------------------------ */

const char *cl_get_string(const cl_evaluated_body_t *base, const char *path, const char *def) {
    const cl_value_t *v = cl_get_defined(base, path);
    return v && v->kind == CL_VAL_STRING ? v->as.string_value : def;
}

long cl_get_int(const cl_evaluated_body_t *base, const char *path, long def) {
    long n;
    return cl_get_as_long(cl_get_defined(base, path), &n) ? n : def;
}

double cl_get_number(const cl_evaluated_body_t *base, const char *path, double def) {
    const cl_value_t *v = cl_get_defined(base, path);
    return v && v->kind == CL_VAL_NUMBER ? v->as.number_value : def;
}

int cl_get_bool(const cl_evaluated_body_t *base, const char *path, int def) {
    const cl_value_t *v = cl_get_defined(base, path);
    return v && v->kind == CL_VAL_BOOL ? v->as.bool_value : def;
}

const cl_value_t *cl_get_list(const cl_evaluated_body_t *base, const char *path) {
    const cl_value_t *v = cl_get_defined(base, path);
    return v && v->kind == CL_VAL_LIST ? v : NULL;
}

const cl_value_t *cl_get_value(const cl_evaluated_body_t *base, const char *path) {
    return cl_get_defined(base, path);
}

const cl_evaluated_block_t *cl_get_block(const cl_evaluated_body_t *base, const char *path) {
    cl_resolved_t r;
    return cl_get_resolve_path(base, path, &r) ? r.block : NULL;
}

/* ------------------------------------------------------------------ */
/* Block iterator                                                       */
/* ------------------------------------------------------------------ */

void cl_block_iter_init(cl_block_iter_t *it, const cl_evaluated_body_t *base, const char *path) {
    it->body = NULL;
    it->type = NULL;
    it->type_len = 0;
    it->next = 0;
    if (!base || !path) {
        return;
    }

    /* Find where the last part starts: everything before it is the path
     * to the block whose body is searched. */
    const char *end = path + strlen(path);
    cl_path_cursor_t c = {path, end, 1};
    cl_part_t part = {CL_PART_NAME, NULL, 0, 0};
    cl_part_t last = part;
    const char *last_start = path;
    int r;
    for (;;) {
        const char *before = c.p;
        r = cl_path_next(&c, &part);
        if (r != 1) {
            break;
        }
        last_start = before;
        last = part;
    }
    if (r < 0 || last.kind != CL_PART_NAME) {
        return;
    }

    const cl_evaluated_body_t *body = base;
    if (last_start != path) {
        cl_resolved_t parent;
        if (!cl_get_resolve(base, path, last_start, &parent) || !parent.block) {
            return;
        }
        body = parent.block->body;
    }
    it->body = body;
    it->type = last.name;
    it->type_len = last.len;
}

const cl_evaluated_block_t *cl_block_iter_next(cl_block_iter_t *it) {
    if (!it->body) {
        return NULL;
    }
    while (it->next < it->body->count) {
        const cl_evaluated_item_t *item = &it->body->items[it->next++];
        if (item->kind == CL_ITEM_BLOCK && cl_name_eq(item->as.block->type, it->type, it->type_len)) {
            return item->as.block;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Kind, existence and count                                            */
/* ------------------------------------------------------------------ */

cl_get_kind_t cl_get_kind(const cl_evaluated_body_t *base, const char *path) {
    cl_resolved_t r;
    if (!cl_get_resolve_path(base, path, &r)) {
        return CL_GET_MISSING;
    }
    if (r.block) {
        return CL_GET_BLOCK;
    }
    switch (r.value->kind) {
        case CL_VAL_STRING: return CL_GET_STRING;
        case CL_VAL_NUMBER: return CL_GET_NUMBER;
        case CL_VAL_BOOL: return CL_GET_BOOL;
        case CL_VAL_LIST: return CL_GET_LIST;
        case CL_VAL_OBJECT: return CL_GET_OBJECT;
        case CL_VAL_NULL: break;
    }
    return CL_GET_MISSING;
}

int cl_has(const cl_evaluated_body_t *base, const char *path) {
    return cl_get_kind(base, path) != CL_GET_MISSING;
}

size_t cl_get_count(const cl_evaluated_body_t *base, const char *path) {
    cl_resolved_t r;
    cl_get_resolve_path(base, path, &r);
    if (r.value) {
        if (r.value->kind == CL_VAL_LIST) {
            return r.value->as.list.count;
        }
        if (r.value->kind == CL_VAL_OBJECT) {
            return r.value->as.object.count;
        }
        return 0;
    }
    cl_block_iter_t it;
    size_t n = 0;
    cl_block_iter_init(&it, base, path);
    while (cl_block_iter_next(&it)) {
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Enum                                                                 */
/* ------------------------------------------------------------------ */

int cl_get_enum(const cl_evaluated_body_t *base, const char *path, const char *const *names, size_t count, int def) {
    const char *s = cl_get_string(base, path, NULL);
    if (!s || !names) {
        return def;
    }
    for (size_t i = 0; i < count && i <= (size_t)INT_MAX; i++) {
        if (names[i] && strcmp(names[i], s) == 0) {
            return (int)i;
        }
    }
    return def;
}

/* ------------------------------------------------------------------ */
/* Lists into C arrays                                                  */
/* ------------------------------------------------------------------ */

/* The list at `path` when every item has kind `kind`, NULL otherwise. */
static const cl_value_t *cl_get_list_of(const cl_evaluated_body_t *base, const char *path, cl_value_kind_t kind) {
    const cl_value_t *list = cl_get_list(base, path);
    if (!list) {
        return NULL;
    }
    for (size_t i = 0; i < list->as.list.count; i++) {
        if (list->as.list.items[i]->kind != kind) {
            return NULL;
        }
    }
    return list;
}

size_t cl_get_strings(const cl_evaluated_body_t *base, const char *path, const char **out, size_t max) {
    const cl_value_t *list = cl_get_list_of(base, path, CL_VAL_STRING);
    if (!list) {
        return 0;
    }
    for (size_t i = 0; out && i < max && i < list->as.list.count; i++) {
        out[i] = list->as.list.items[i]->as.string_value;
    }
    return list->as.list.count;
}

size_t cl_get_ints(const cl_evaluated_body_t *base, const char *path, long *out, size_t max) {
    const cl_value_t *list = cl_get_list_of(base, path, CL_VAL_NUMBER);
    if (!list) {
        return 0;
    }
    long n;
    for (size_t i = 0; i < list->as.list.count; i++) {
        if (!cl_get_as_long(list->as.list.items[i], &n)) {
            return 0; /* checked up front so `out` stays untouched */
        }
    }
    for (size_t i = 0; out && i < max && i < list->as.list.count; i++) {
        cl_get_as_long(list->as.list.items[i], &out[i]);
    }
    return list->as.list.count;
}

size_t cl_get_numbers(const cl_evaluated_body_t *base, const char *path, double *out, size_t max) {
    const cl_value_t *list = cl_get_list_of(base, path, CL_VAL_NUMBER);
    if (!list) {
        return 0;
    }
    for (size_t i = 0; out && i < max && i < list->as.list.count; i++) {
        out[i] = list->as.list.items[i]->as.number_value;
    }
    return list->as.list.count;
}

/* ------------------------------------------------------------------ */
/* Attribute iterator                                                   */
/* ------------------------------------------------------------------ */

void cl_attr_iter_init(cl_attr_iter_t *it, const cl_evaluated_body_t *base, const char *path) {
    it->body = NULL;
    it->object = NULL;
    it->next = 0;
    if (!path || !*path) {
        it->body = base;
        return;
    }
    cl_resolved_t r;
    if (!cl_get_resolve_path(base, path, &r)) {
        return;
    }
    if (r.block) {
        it->body = r.block->body;
    } else if (r.value->kind == CL_VAL_OBJECT) {
        it->object = r.value;
    }
}

int cl_attr_iter_next(cl_attr_iter_t *it, const char **key, const cl_value_t **value) {
    const char *k = NULL;
    const cl_value_t *v = NULL;
    if (it->object) {
        if (it->next >= it->object->as.object.count) {
            return 0;
        }
        k = it->object->as.object.items[it->next].key;
        v = it->object->as.object.items[it->next].value;
        it->next++;
    } else if (it->body) {
        while (it->next < it->body->count && it->body->items[it->next].kind != CL_ITEM_ATTRIBUTE) {
            it->next++;
        }
        if (it->next >= it->body->count) {
            return 0;
        }
        k = it->body->items[it->next].as.attribute->name;
        v = it->body->items[it->next].as.attribute->value;
        it->next++;
    } else {
        return 0;
    }
    if (key) {
        *key = k;
    }
    if (value) {
        *value = v;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* printf-style paths                                                   */
/* ------------------------------------------------------------------ */

/* Formats into `buf` when the path fits, or into a malloc'd string the
 * caller frees (compare the result with `buf`). NULL on a format error. */
static char *cl_get_vformat(char *buf, size_t size, const char *fmt, va_list ap) {
    va_list again;
    va_copy(again, ap);
    int n = vsnprintf(buf, size, fmt, ap);
    if (n < 0) {
        va_end(again);
        return NULL;
    }
    if ((size_t)n < size) {
        va_end(again);
        return buf;
    }
    char *big = malloc((size_t)n + 1);
    if (!big) {
        abort();
    }
    vsnprintf(big, (size_t)n + 1, fmt, again);
    va_end(again);
    return big;
}

#define CL_GET_PATH_BUF 256

const char *cl_get_stringf(const cl_evaluated_body_t *base, const char *def, const char *fmt, ...) {
    char buf[CL_GET_PATH_BUF];
    va_list ap;
    va_start(ap, fmt);
    char *path = cl_get_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const char *result = path ? cl_get_string(base, path, def) : def;
    if (path != buf) {
        free(path);
    }
    return result;
}

long cl_get_intf(const cl_evaluated_body_t *base, long def, const char *fmt, ...) {
    char buf[CL_GET_PATH_BUF];
    va_list ap;
    va_start(ap, fmt);
    char *path = cl_get_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    long result = path ? cl_get_int(base, path, def) : def;
    if (path != buf) {
        free(path);
    }
    return result;
}

int cl_get_boolf(const cl_evaluated_body_t *base, int def, const char *fmt, ...) {
    char buf[CL_GET_PATH_BUF];
    va_list ap;
    va_start(ap, fmt);
    char *path = cl_get_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    int result = path ? cl_get_bool(base, path, def) : def;
    if (path != buf) {
        free(path);
    }
    return result;
}

const cl_evaluated_block_t *cl_get_blockf(const cl_evaluated_body_t *base, const char *fmt, ...) {
    char buf[CL_GET_PATH_BUF];
    va_list ap;
    va_start(ap, fmt);
    char *path = cl_get_vformat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const cl_evaluated_block_t *result = path ? cl_get_block(base, path) : NULL;
    if (path != buf) {
        free(path);
    }
    return result;
}
