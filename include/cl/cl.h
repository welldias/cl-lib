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

typedef struct cl_object_item {
    char *key;
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

typedef struct cl_block {
    char *type;
    char **labels;
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
 * label_count == 0 (labels may be NULL) to match blocks with no labels. */
cl_block_t *cl_body_find_block(const cl_body_t *body, const char *type,
                                const char *const *labels, size_t label_count);

cl_expr_kind_t cl_expr_kind(const cl_expr_t *expr);
const char *cl_expr_as_string(const cl_expr_t *expr);
int cl_expr_as_number(const cl_expr_t *expr, double *out);
int cl_expr_as_bool(const cl_expr_t *expr, int *out);

size_t cl_expr_object_count(const cl_expr_t *expr);
const char *cl_expr_object_key_at(const cl_expr_t *expr, size_t index);
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

#ifdef __cplusplus
}
#endif

#endif /* CL_H */
