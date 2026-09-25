#ifndef CL_H
#define CL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CL_VERSION_MAJOR 0
#define CL_VERSION_MINOR 1
#define CL_VERSION_PATCH 0

const char *cl_version(void);

/* ------------------------------------------------------------------ */
/* Error reporting                                                     */
/* ------------------------------------------------------------------ */

typedef struct cl_error {
    char message[256];
    int line;
    int col;
} cl_error_t;

/* ------------------------------------------------------------------ */
/* Expression AST                                                      */
/* ------------------------------------------------------------------ */

typedef enum cl_expr_kind {
    CL_EXPR_STRING,
    CL_EXPR_NUMBER,
    CL_EXPR_BOOL,
    CL_EXPR_NULL,
    CL_EXPR_OBJECT,
    CL_EXPR_TRAVERSAL,
    CL_EXPR_TUPLE,
    CL_EXPR_TEMPLATE,
    CL_EXPR_CALL,
    CL_EXPR_UNARY,
    CL_EXPR_BINARY,
    CL_EXPR_CONDITIONAL,
    CL_EXPR_FOR,
    CL_EXPR_POSTFIX /* base expr + traversal steps applied to any primary,
                        not just an identifier root - see CL_EXPR_TRAVERSAL */
} cl_expr_kind_t;

typedef enum cl_traversal_step_kind {
    CL_STEP_ATTR,
    CL_STEP_INDEX_NUMBER,
    CL_STEP_INDEX_STRING,
    CL_STEP_SPLAT_ATTR, /* ".*" */
    CL_STEP_SPLAT_FULL, /* "[*]" */
    CL_STEP_INDEX_EXPR  /* "[expr]" - anything but a lone number/string literal */
} cl_traversal_step_kind_t;

typedef struct cl_traversal_step {
    cl_traversal_step_kind_t kind;
    char *name;         /* CL_STEP_ATTR / CL_STEP_INDEX_STRING */
    double index;       /* CL_STEP_INDEX_NUMBER */
    struct cl_expr *expr; /* CL_STEP_INDEX_EXPR; evaluates to a number (list
                             index) or a string (object key) */
} cl_traversal_step_t;

typedef enum cl_unary_op {
    CL_OP_NEG,
    CL_OP_NOT
} cl_unary_op_t;

typedef enum cl_binary_op {
    CL_OP_ADD,
    CL_OP_SUB,
    CL_OP_MUL,
    CL_OP_DIV,
    CL_OP_MOD,
    CL_OP_EQ,
    CL_OP_NEQ,
    CL_OP_LT,
    CL_OP_LE,
    CL_OP_GT,
    CL_OP_GE,
    CL_OP_AND,
    CL_OP_OR
} cl_binary_op_t;

typedef struct cl_expr cl_expr_t;

/* An object key is either constant - a bare identifier, or a quoted
 * string without interpolation, stored decoded in `key` ("$${x}" gives
 * the text "${x}") - or computed at evaluation time: a quoted string with
 * "${...}"/"%{...}", or "(expr)". A computed key has `key` == NULL and its
 * expression in `key_expr`; a constant one has `key_expr` == NULL. */
typedef struct cl_object_item {
    char *key;
    cl_expr_t *key_expr;
    cl_expr_t *value;
} cl_object_item_t;

/* ---- template mini-AST: ${...} interpolation and %{if}/%{for} directives */

typedef enum cl_template_part_kind {
    CL_TPL_LITERAL,
    CL_TPL_INTERP,
    CL_TPL_IF,
    CL_TPL_FOR
} cl_template_part_kind_t;

typedef struct cl_template cl_template_t;

typedef struct cl_template_part {
    cl_template_part_kind_t kind;

    char *text; /* CL_TPL_LITERAL */

    cl_expr_t *expr; /* CL_TPL_INTERP */

    cl_expr_t *if_cond; /* CL_TPL_IF */
    cl_template_t *if_then;
    cl_template_t *if_else; /* NULL when there is no %{else} */

    char *for_key_var; /* CL_TPL_FOR; NULL in the single-variable form */
    char *for_val_var;
    cl_expr_t *for_collection;
    cl_template_t *for_body;
} cl_template_part_t;

struct cl_template {
    cl_template_part_t *parts;
    size_t count;
    size_t capacity;
};

struct cl_expr {
    cl_expr_kind_t kind;
    int line;
    int col;
    union {
        char *string_value;
        double number_value;
        int bool_value;
        struct {
            cl_object_item_t *items;
            size_t count;
            size_t capacity;
        } object;
        struct {
            char *root;
            cl_traversal_step_t *steps;
            size_t count;
            size_t capacity;
        } traversal;
        struct {
            cl_expr_t *base;
            cl_traversal_step_t *steps;
            size_t count;
            size_t capacity;
        } postfix;
        struct {
            cl_expr_t **items;
            size_t count;
            size_t capacity;
        } tuple;
        cl_template_t *tpl; /* CL_EXPR_TEMPLATE */
        struct {
            char *name;
            cl_expr_t **args;
            size_t count;
            size_t capacity;
            int expand_final; /* trailing "..." on the last argument */
        } call;
        struct {
            cl_unary_op_t op;
            cl_expr_t *operand;
        } unary;
        struct {
            cl_binary_op_t op;
            cl_expr_t *left;
            cl_expr_t *right;
        } binary;
        struct {
            cl_expr_t *cond;
            cl_expr_t *then_expr;
            cl_expr_t *else_expr;
        } conditional;
        struct {
            char *key_var; /* NULL in the single-variable form */
            char *val_var;
            cl_expr_t *collection;
            cl_expr_t *key_expr;   /* object form only */
            cl_expr_t *value_expr;
            cl_expr_t *cond;       /* optional "if" filter, NULL when absent */
            int is_object;
            int grouping; /* object form's trailing "..." */
        } for_expr;
    } as;
};

/* ------------------------------------------------------------------ */
/* Body / block / attribute AST                                        */
/* ------------------------------------------------------------------ */

typedef struct cl_body cl_body_t;

typedef struct cl_attribute {
    char *name;
    cl_expr_t *value;
    int line;
    int col;
} cl_attribute_t;

/* Block labels are quoted strings and, like every quoted string, may be
 * templates - an extension of cl over HCL, which only allows literal
 * labels. A constant label is stored decoded in labels[i]. A label with
 * "${...}"/"%{...}" is computed at evaluation time: labels[i] is NULL and
 * label_exprs[i] holds its template. label_exprs itself is NULL when every
 * label is constant. */
typedef struct cl_block {
    char *type;
    char **labels;
    cl_expr_t **label_exprs;
    size_t label_count;
    size_t label_capacity;
    cl_body_t *body;
    int line;
    int col;
} cl_block_t;

typedef enum cl_body_item_kind {
    CL_ITEM_ATTRIBUTE,
    CL_ITEM_BLOCK
} cl_body_item_kind_t;

typedef struct cl_body_item {
    cl_body_item_kind_t kind;
    union {
        cl_attribute_t *attribute;
        cl_block_t *block;
    } as;
} cl_body_item_t;

struct cl_body {
    cl_body_item_t *items;
    size_t count;
    size_t capacity;
};

/* ------------------------------------------------------------------ */
/* Document lifecycle                                                   */
/* ------------------------------------------------------------------ */

typedef struct cl_document cl_document_t;

cl_document_t *cl_load_file(const char *path, cl_error_t *err);
cl_document_t *cl_load_string(const char *source, const char *source_name, cl_error_t *err);
void cl_document_free(cl_document_t *doc);
cl_body_t *cl_document_root(cl_document_t *doc);

/* ------------------------------------------------------------------ */
/* Serialization                                                        */
/* ------------------------------------------------------------------ */

char *cl_document_to_string(const cl_document_t *doc);
int cl_save_file(const cl_document_t *doc, const char *path, cl_error_t *err);

/* ------------------------------------------------------------------ */
/* Navigation / search                                                  */
/* ------------------------------------------------------------------ */

cl_attribute_t *cl_body_get_attribute(const cl_body_t *body, const char *name);

/* Returns the number of matches and allocates *out_blocks (caller must
 * free() it) with that many cl_block_t* pointers. *out_blocks is left
 * untouched (NULL) when the returned count is 0. */
size_t cl_body_find_blocks(const cl_body_t *body, const char *type, cl_block_t ***out_blocks);

/* Finds the first block of `type` whose labels match exactly. Pass
 * label_count == 0 (labels may be NULL) to match blocks with no labels.
 * A computed label ("web-${env}") has no value before evaluation and never
 * matches here; use cl_evaluated_body_find_block() / cl_get_block() on the
 * evaluated result instead. The same goes for computed keys in
 * cl_expr_object_get(). */
cl_block_t *cl_body_find_block(const cl_body_t *body, const char *type,
                                const char *const *labels, size_t label_count);

cl_expr_kind_t cl_expr_kind(const cl_expr_t *expr);
const char *cl_expr_as_string(const cl_expr_t *expr);
int cl_expr_as_number(const cl_expr_t *expr, double *out);
int cl_expr_as_bool(const cl_expr_t *expr, int *out);

size_t cl_expr_object_count(const cl_expr_t *expr);
/* NULL for a computed key: see cl_expr_object_key_expr_at(). */
const char *cl_expr_object_key_at(const cl_expr_t *expr, size_t index);
/* The expression of a computed key; NULL for a constant one. */
cl_expr_t *cl_expr_object_key_expr_at(const cl_expr_t *expr, size_t index);
cl_expr_t *cl_expr_object_value_at(const cl_expr_t *expr, size_t index);
cl_expr_t *cl_expr_object_get(const cl_expr_t *expr, const char *key);

const char *cl_expr_traversal_root(const cl_expr_t *expr);
size_t cl_expr_traversal_step_count(const cl_expr_t *expr);
const cl_traversal_step_t *cl_expr_traversal_step_at(const cl_expr_t *expr, size_t index);

/* CL_EXPR_POSTFIX: the same ".attr"/"[idx]"/splat steps as CL_EXPR_TRAVERSAL,
 * but chained onto an arbitrary base expression (a function call, a
 * parenthesized expression, an object/tuple literal, or a for-expression)
 * instead of a bare identifier root. */
cl_expr_t *cl_expr_postfix_base(const cl_expr_t *expr);
size_t cl_expr_postfix_step_count(const cl_expr_t *expr);
const cl_traversal_step_t *cl_expr_postfix_step_at(const cl_expr_t *expr, size_t index);

size_t cl_expr_tuple_count(const cl_expr_t *expr);
cl_expr_t *cl_expr_tuple_at(const cl_expr_t *expr, size_t index);

const cl_template_t *cl_expr_template(const cl_expr_t *expr);
size_t cl_template_part_count(const cl_template_t *tpl);
const cl_template_part_t *cl_template_part_at(const cl_template_t *tpl, size_t index);

const char *cl_expr_call_name(const cl_expr_t *expr);
size_t cl_expr_call_arg_count(const cl_expr_t *expr);
cl_expr_t *cl_expr_call_arg_at(const cl_expr_t *expr, size_t index);
int cl_expr_call_expand_final(const cl_expr_t *expr);

int cl_expr_unary_op(const cl_expr_t *expr, cl_unary_op_t *out);
cl_expr_t *cl_expr_unary_operand(const cl_expr_t *expr);

int cl_expr_binary_op(const cl_expr_t *expr, cl_binary_op_t *out);
cl_expr_t *cl_expr_binary_left(const cl_expr_t *expr);
cl_expr_t *cl_expr_binary_right(const cl_expr_t *expr);

cl_expr_t *cl_expr_conditional_cond(const cl_expr_t *expr);
cl_expr_t *cl_expr_conditional_then(const cl_expr_t *expr);
cl_expr_t *cl_expr_conditional_else(const cl_expr_t *expr);

const char *cl_expr_for_key_var(const cl_expr_t *expr);
const char *cl_expr_for_val_var(const cl_expr_t *expr);
cl_expr_t *cl_expr_for_collection(const cl_expr_t *expr);
cl_expr_t *cl_expr_for_key_expr(const cl_expr_t *expr);
cl_expr_t *cl_expr_for_value_expr(const cl_expr_t *expr);
cl_expr_t *cl_expr_for_cond(const cl_expr_t *expr);
int cl_expr_for_is_object(const cl_expr_t *expr);
int cl_expr_for_grouping(const cl_expr_t *expr);

/* ------------------------------------------------------------------ */
/* Mutation                                                             */
/* ------------------------------------------------------------------ */

cl_attribute_t *cl_body_set_string(cl_document_t *doc, cl_body_t *body, const char *name, const char *value);
cl_attribute_t *cl_body_set_number(cl_document_t *doc, cl_body_t *body, const char *name, double value);
cl_attribute_t *cl_body_set_bool(cl_document_t *doc, cl_body_t *body, const char *name, int value);
cl_attribute_t *cl_body_set_null(cl_document_t *doc, cl_body_t *body, const char *name);
int cl_body_remove_attribute(cl_body_t *body, const char *name);

cl_block_t *cl_body_add_block(cl_document_t *doc, cl_body_t *body, const char *type,
                               const char *const *labels, size_t label_count);
int cl_body_remove_block(cl_body_t *parent, cl_block_t *block);

/* ------------------------------------------------------------------ */
/* Evaluation (opt-in second phase, never run automatically by load)    */
/* ------------------------------------------------------------------ */

typedef enum cl_value_kind {
    CL_VAL_STRING,
    CL_VAL_NUMBER,
    CL_VAL_BOOL,
    CL_VAL_NULL,
    CL_VAL_LIST,
    CL_VAL_OBJECT
} cl_value_kind_t;

typedef struct cl_value cl_value_t;

typedef struct cl_value_object_item {
    char *key;
    cl_value_t *value;
} cl_value_object_item_t;

struct cl_value {
    cl_value_kind_t kind;
    union {
        char *string_value;
        double number_value;
        int bool_value;
        struct {
            cl_value_t **items;
            size_t count;
            size_t capacity;
        } list;
        struct {
            cl_value_object_item_t *items;
            size_t count;
            size_t capacity;
        } object; /* ordered association array, not a hash map */
    } as;
};

typedef struct cl_evaluated cl_evaluated_t;
typedef struct cl_evaluated_body cl_evaluated_body_t;

typedef struct cl_evaluated_attribute {
    char *name;
    cl_value_t *value;
} cl_evaluated_attribute_t;

typedef struct cl_evaluated_block {
    char *type;
    char **labels;
    size_t label_count;
    cl_evaluated_body_t *body;
} cl_evaluated_block_t;

typedef struct cl_evaluated_item {
    cl_body_item_kind_t kind;
    union {
        cl_evaluated_attribute_t *attribute;
        cl_evaluated_block_t *block;
    } as;
} cl_evaluated_item_t;

struct cl_evaluated_body {
    cl_evaluated_item_t *items;
    size_t count;
    size_t capacity;
};

/* ---- external bindings: values injected by the host program ---------
 *
 * A cl_bindings_t maps top-level names to values built by the C program.
 * During evaluation a binding is looked up right after "for" variables and
 * before the document's own top-level attributes/blocks, so it overrides a
 * same-named top-level attribute everywhere - both where it is referenced
 * ("x = env") and in the evaluated attribute itself ("env = "dev"" comes out
 * as the bound value). That lets a document keep defaults that the host
 * can override. Bindings carry no special names: the host picks them.
 *
 * Every cl_value_t below is owned by the cl_bindings_t that created it and
 * may only be passed to functions of that same cl_bindings_t. Evaluation
 * copies what it uses into the result, so the bindings can be freed right
 * after cl_document_evaluate_with() returns. */
typedef struct cl_bindings cl_bindings_t;

cl_bindings_t *cl_bindings_new(void);
void cl_bindings_free(cl_bindings_t *bindings);

cl_value_t *cl_bindings_string(cl_bindings_t *bindings, const char *value);
cl_value_t *cl_bindings_number(cl_bindings_t *bindings, double value);
cl_value_t *cl_bindings_bool(cl_bindings_t *bindings, int value);
cl_value_t *cl_bindings_null(cl_bindings_t *bindings);
cl_value_t *cl_bindings_list(cl_bindings_t *bindings);
cl_value_t *cl_bindings_object(cl_bindings_t *bindings);

/* Both return 0 on success, -1 on a wrong container kind, a NULL argument,
 * or when `item`/`value` already contains `list`/`object` (which would
 * make the value cyclic). cl_bindings_object_set replaces an existing key. */
int cl_bindings_list_add(cl_bindings_t *bindings, cl_value_t *list, cl_value_t *item);
int cl_bindings_object_set(cl_bindings_t *bindings, cl_value_t *object, const char *key, cl_value_t *value);

/* Binds `name` to `value`, replacing any previous binding of that name.
 * Returns 0 on success, -1 on a NULL argument. */
int cl_bindings_set(cl_bindings_t *bindings, const char *name, cl_value_t *value);
int cl_bindings_set_string(cl_bindings_t *bindings, const char *name, const char *value);
int cl_bindings_set_number(cl_bindings_t *bindings, const char *name, double value);
int cl_bindings_set_bool(cl_bindings_t *bindings, const char *name, int value);

/* Walks doc->root, resolving every expression (including traversals,
 * operators, for-expressions, splats, function calls and templates) into a
 * plain value tree. Never mutates `doc`; the raw AST stays explorable
 * through the Milestone-1 API regardless of whether this was called.
 * Stops at the first error found (eager evaluation). */
cl_evaluated_t *cl_document_evaluate(cl_document_t *doc, cl_error_t *err);

/* Same as cl_document_evaluate(), with `bindings` (may be NULL) visible to
 * the document - see cl_bindings_t above. */
cl_evaluated_t *cl_document_evaluate_with(cl_document_t *doc, const cl_bindings_t *bindings, cl_error_t *err);
void cl_evaluated_free(cl_evaluated_t *result);
cl_evaluated_body_t *cl_evaluated_root(cl_evaluated_t *result);

cl_evaluated_attribute_t *cl_evaluated_body_get_attribute(const cl_evaluated_body_t *body, const char *name);
size_t cl_evaluated_body_find_blocks(const cl_evaluated_body_t *body, const char *type,
                                      cl_evaluated_block_t ***out_blocks);
cl_evaluated_block_t *cl_evaluated_body_find_block(const cl_evaluated_body_t *body, const char *type,
                                                    const char *const *labels, size_t label_count);

cl_value_kind_t cl_value_kind(const cl_value_t *value);
const char *cl_value_as_string(const cl_value_t *value);
int cl_value_as_number(const cl_value_t *value, double *out);
int cl_value_as_bool(const cl_value_t *value, int *out);

size_t cl_value_list_count(const cl_value_t *value);
cl_value_t *cl_value_list_at(const cl_value_t *value, size_t index);

size_t cl_value_object_count(const cl_value_t *value);
const char *cl_value_object_key_at(const cl_value_t *value, size_t index);
cl_value_t *cl_value_object_value_at(const cl_value_t *value, size_t index);
cl_value_t *cl_value_object_get(const cl_value_t *value, const char *key);

/* ------------------------------------------------------------------ */
/* Path getters (convenience reads over an evaluated body)              */
/* ------------------------------------------------------------------ */

/* A path is a sequence of parts starting at `base` (cl_evaluated_root() for
 * the whole document, or some block's ->body for a relative read):
 *
 *     server.web.port     tags["Name"]     disks[0].size     ["a.b"].c
 *
 * `.name` or `["name"]` (for names holding "." or "[") look up, in a body,
 * an attribute first; otherwise a block of that type, consuming the next
 * parts as its labels - the longest label sequence that matches wins, and
 * among equal matches the first block in the document. Inside a value,
 * `.key`/`["key"]` read an object and `[n]` a list. Quoted names take no
 * escapes: they end at the first '"'.
 *
 * A missing path, a malformed path, a null value and a value of the wrong
 * type all count as "not defined": the scalar getters then return `def`,
 * and the others NULL. cl_get_int only accepts integral numbers that fit in
 * a long. */
const char *cl_get_string(const cl_evaluated_body_t *base, const char *path, const char *def);
long cl_get_int(const cl_evaluated_body_t *base, const char *path, long def);
double cl_get_number(const cl_evaluated_body_t *base, const char *path, double def);
int cl_get_bool(const cl_evaluated_body_t *base, const char *path, int def);
const cl_value_t *cl_get_list(const cl_evaluated_body_t *base, const char *path);
/* Any kind of value (objects included); NULL when not defined or null. */
const cl_value_t *cl_get_value(const cl_evaluated_body_t *base, const char *path);
/* The block the whole path names, e.g. "server.web"; NULL otherwise. */
const cl_evaluated_block_t *cl_get_block(const cl_evaluated_body_t *base, const char *path);

/* Walks every block of one type, in document order. The last part of
 * `path` is the block type; the parts before it must name a block whose
 * body is searched ("machine.web.disk"), or be absent to search `base`
 * itself ("disk"). The iterator keeps a pointer into `path`, so `path` must
 * outlive it. The fields are private; the struct is public only so it can
 * live on the stack (no allocation, nothing to free):
 *
 *     cl_block_iter_t it;
 *     const cl_evaluated_block_t *disk;
 *     cl_block_iter_init(&it, root, "machine.web.disk");
 *     while ((disk = cl_block_iter_next(&it)) != NULL) { ... }
 */
typedef struct cl_block_iter {
    const cl_evaluated_body_t *body;
    const char *type;
    size_t type_len;
    size_t next;
} cl_block_iter_t;

void cl_block_iter_init(cl_block_iter_t *it, const cl_evaluated_body_t *base, const char *path);
const cl_evaluated_block_t *cl_block_iter_next(cl_block_iter_t *it);

/* What a path names. A null value counts as CL_GET_MISSING, like in the
 * getters above. */
typedef enum cl_get_kind {
    CL_GET_MISSING,
    CL_GET_STRING,
    CL_GET_NUMBER,
    CL_GET_BOOL,
    CL_GET_LIST,
    CL_GET_OBJECT,
    CL_GET_BLOCK
} cl_get_kind_t;

cl_get_kind_t cl_get_kind(const cl_evaluated_body_t *base, const char *path);
/* 1 when the path names a block or a non-null value, 0 otherwise. */
int cl_has(const cl_evaluated_body_t *base, const char *path);
/* Items of a list, keys of an object, or - when the path does not name a
 * value - how many blocks cl_block_iter would yield for it ("server",
 * "machine.m1.disk"). 0 for anything else. */
size_t cl_get_count(const cl_evaluated_body_t *base, const char *path);

/* Index in `names` of the string at `path` (exact, case-sensitive match),
 * or `def` when it is not defined, not a string, or not in `names`. */
int cl_get_enum(const cl_evaluated_body_t *base, const char *path, const char *const *names, size_t count, int def);

/* Copy a list into a C array. When `path` names a list whose items all
 * have the right type (for ints: integral numbers that fit in a long),
 * write the first min(count, max) items to `out` and return the list's
 * count - larger than `max` when `out` was too small; `out` may be NULL
 * with `max` 0 to only ask for the count. Otherwise return 0 and leave
 * `out` untouched. Strings stay owned by the evaluated result. */
size_t cl_get_strings(const cl_evaluated_body_t *base, const char *path, const char **out, size_t max);
size_t cl_get_ints(const cl_evaluated_body_t *base, const char *path, long *out, size_t max);
size_t cl_get_numbers(const cl_evaluated_body_t *base, const char *path, double *out, size_t max);

/* Walks the attributes of a block body or the keys of an object value, in
 * document order. `path` names the block or object; NULL or "" walks
 * `base` itself. Nested blocks are skipped, and null values are yielded
 * as they are (kind CL_VAL_NULL). Same stack-only contract as
 * cl_block_iter_t:
 *
 *     cl_attr_iter_t it;
 *     const char *key;
 *     const cl_value_t *value;
 *     cl_attr_iter_init(&it, root, "service.api.env");
 *     while (cl_attr_iter_next(&it, &key, &value)) { ... }
 */
typedef struct cl_attr_iter {
    const cl_evaluated_body_t *body;
    const cl_value_t *object;
    size_t next;
} cl_attr_iter_t;

void cl_attr_iter_init(cl_attr_iter_t *it, const cl_evaluated_body_t *base, const char *path);
/* Returns 1 and sets *key / *value (either may be NULL) for the next item,
 * 0 when there are no more. */
int cl_attr_iter_next(cl_attr_iter_t *it, const char **key, const cl_value_t **value);

/* printf-style paths, for labels that come from variables:
 *
 *     long port = cl_get_intf(root, 80, "server.%s.port", name);
 *
 * The formatted text is an ordinary path: a label holding "." or "["
 * still needs the ["..."] form. */
#if defined(__GNUC__) || defined(__clang__)
#define CL_PRINTF_FORMAT(fmt_index, first_arg) __attribute__((format(printf, fmt_index, first_arg)))
#else
#define CL_PRINTF_FORMAT(fmt_index, first_arg)
#endif

const char *cl_get_stringf(const cl_evaluated_body_t *base, const char *def, const char *fmt, ...)
    CL_PRINTF_FORMAT(3, 4);
long cl_get_intf(const cl_evaluated_body_t *base, long def, const char *fmt, ...) CL_PRINTF_FORMAT(3, 4);
int cl_get_boolf(const cl_evaluated_body_t *base, int def, const char *fmt, ...) CL_PRINTF_FORMAT(3, 4);
const cl_evaluated_block_t *cl_get_blockf(const cl_evaluated_body_t *base, const char *fmt, ...)
    CL_PRINTF_FORMAT(2, 3);

/* ------------------------------------------------------------------ */
/* Export of evaluated results                                          */
/* ------------------------------------------------------------------ */

/* Every function returns a malloc'd string the caller releases with
 * free(), or NULL when given NULL.
 *
 * The *_to_string() forms write cl syntax. For a whole result it is a
 * "flattened" document: every expression, template and binding already
 * resolved, so reloading and evaluating it gives the same values.
 * Infinities are written as 1e999/-1e999; NaN, which has no cl form, as
 * null.
 *
 * The *_to_json() forms write indented JSON (2 spaces). A body becomes
 * { "attributes": {...}, "blocks": [...] } and a block
 * { "type": "...", "labels": [...], "body": <body> }, blocks in document
 * order. Where a body repeats an attribute name, only the first one (the
 * one every lookup sees) is written, so keys stay unique. NaN and
 * infinities become null. Documents and blocks end with a newline; single
 * values don't. */
char *cl_value_to_string(const cl_value_t *value);
char *cl_value_to_json(const cl_value_t *value);
char *cl_evaluated_to_string(const cl_evaluated_t *result);
char *cl_evaluated_to_json(const cl_evaluated_t *result);
char *cl_evaluated_block_to_string(const cl_evaluated_block_t *block);
char *cl_evaluated_block_to_json(const cl_evaluated_block_t *block);

/* ------------------------------------------------------------------ */
/* Schemas (opt-in validation of specialized block types)               */
/* ------------------------------------------------------------------ */

/* A schema lets the host program give meaning to block types it cares
 * about - e.g. a "machine" block that may only hold "cpu" and "memory" -
 * while the language itself stays agnostic. Rules match top-level blocks
 * by type (labels are not checked). Inside a registered block everything is
 * closed: only declared attributes and declared sub-blocks may appear, each
 * attribute at most once, and required attributes must be present.
 * Top-level blocks of an unregistered type are ignored, unless the schema
 * is strict, in which case they are an error. Top-level attributes are
 * never checked. */
typedef enum cl_schema_type {
    CL_TYPE_ANY, /* the only type that accepts null */
    CL_TYPE_STRING,
    CL_TYPE_NUMBER,
    CL_TYPE_BOOL,
    CL_TYPE_LIST,
    CL_TYPE_OBJECT
} cl_schema_type_t;

typedef struct cl_schema cl_schema_t;
typedef struct cl_schema_block cl_schema_block_t;

cl_schema_t *cl_schema_new(void);
void cl_schema_free(cl_schema_t *schema);
void cl_schema_set_strict(cl_schema_t *schema, int strict);

/* Register a block type at the top level, or as a sub-block allowed inside
 * `parent`. Return NULL when that type is already registered at that level.
 * The returned rule stays valid until cl_schema_free(). */
cl_schema_block_t *cl_schema_add_block(cl_schema_t *schema, const char *type);
cl_schema_block_t *cl_schema_block_add_block(cl_schema_block_t *parent, const char *type);

/* Declares an attribute allowed inside `block`. Returns -1 when `name` is
 * already declared there (or on a NULL argument), 0 otherwise. */
int cl_schema_block_add_attr(cl_schema_block_t *block, const char *name, cl_schema_type_t type, int required);

/* Declares a string attribute that may only hold one of `values` (an
 * enum). Returns -1 when `name` is already declared, `count` is 0, or
 * `values` holds a NULL or repeated entry; 0 otherwise. */
int cl_schema_block_add_enum(cl_schema_block_t *block, const char *name, const char *const *values, size_t count,
                             int required);

/* Checks `doc` against `schema`. With `result` == NULL, attribute types are
 * only checked for literal values ("x", 1, true, [...], {...}); pass the
 * cl_evaluated_t of this same document to check every value's evaluated
 * type too. Returns 0 when valid; otherwise -1 with the first problem found
 * (and its line/column in `doc`) in *err. */
int cl_schema_validate(const cl_schema_t *schema, const cl_document_t *doc, const cl_evaluated_t *result,
                       cl_error_t *err);

/* Builds a schema from a .cl schema file - see README.md, "Block schemas":
 *
 *     strict = true                    # optional
 *     block "machine" {
 *       cpu  = { type = "number", required = true }
 *       tags = "list"                  # short form: type only
 *       kind = ["ssd", "hdd"]          # short form: enum of strings
 *       block "disk" { size = "number" }
 *     }
 *
 * "block" and "strict" are vocabulary of this schema format only, not of
 * the language. Returns NULL with the error in *err on an invalid file. */
cl_schema_t *cl_schema_load_file(const char *path, cl_error_t *err);
cl_schema_t *cl_schema_load_string(const char *source, const char *source_name, cl_error_t *err);

#ifdef __cplusplus
}
#endif

#endif /* CL_H */
