#include "cl_schema.h"

#include "cl_eval.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Construction                                                         */
/* ------------------------------------------------------------------ */

cl_schema_t *cl_schema_new(void) {
    cl_schema_t *schema = calloc(1, sizeof(cl_schema_t));
    if (!schema) {
        abort();
    }
    schema->root.owner = schema;
    return schema;
}

void cl_schema_free(cl_schema_t *schema) {
    if (!schema) {
        return;
    }
    cl_arena_free_all(&schema->arena);
    free(schema);
}

void cl_schema_set_strict(cl_schema_t *schema, int strict) {
    if (schema) {
        schema->strict = strict ? 1 : 0;
    }
}

static cl_schema_block_t *cl_schema_find_block(const cl_schema_block_t *level, const char *type) {
    for (size_t i = 0; i < level->block_count; i++) {
        if (strcmp(level->blocks[i]->type, type) == 0) {
            return level->blocks[i];
        }
    }
    return NULL;
}

static const cl_schema_attr_t *cl_schema_find_attr(const cl_schema_block_t *block, const char *name) {
    for (size_t i = 0; i < block->attr_count; i++) {
        if (strcmp(block->attrs[i]->name, name) == 0) {
            return block->attrs[i];
        }
    }
    return NULL;
}

cl_schema_block_t *cl_schema_block_add_block(cl_schema_block_t *parent, const char *type) {
    if (!parent || !type || cl_schema_find_block(parent, type)) {
        return NULL;
    }
    cl_arena_t *arena = &parent->owner->arena;
    cl_schema_block_t *block = cl_arena_alloc_raw(arena, sizeof(cl_schema_block_t));
    block->type = cl_arena_strdup_raw(arena, type);
    block->owner = parent->owner;
    cl_array_grow_raw(arena, (void **)&parent->blocks, &parent->block_count, &parent->block_capacity,
                       sizeof(cl_schema_block_t *));
    parent->blocks[parent->block_count++] = block;
    return block;
}

cl_schema_block_t *cl_schema_add_block(cl_schema_t *schema, const char *type) {
    if (!schema) {
        return NULL;
    }
    return cl_schema_block_add_block(&schema->root, type);
}

static cl_schema_attr_t *cl_schema_new_attr(cl_schema_block_t *block, const char *name, cl_schema_type_t type,
                                            int required) {
    cl_arena_t *arena = &block->owner->arena;
    cl_schema_attr_t *attr = cl_arena_alloc_raw(arena, sizeof(cl_schema_attr_t));
    attr->name = cl_arena_strdup_raw(arena, name);
    attr->type = type;
    attr->required = required ? 1 : 0;
    cl_array_grow_raw(arena, (void **)&block->attrs, &block->attr_count, &block->attr_capacity,
                       sizeof(cl_schema_attr_t *));
    block->attrs[block->attr_count++] = attr;
    return attr;
}

int cl_schema_block_add_attr(cl_schema_block_t *block, const char *name, cl_schema_type_t type, int required) {
    if (!block || !name || cl_schema_find_attr(block, name)) {
        return -1;
    }
    cl_schema_new_attr(block, name, type, required);
    return 0;
}

int cl_schema_block_add_enum(cl_schema_block_t *block, const char *name, const char *const *values, size_t count,
                             int required) {
    if (!block || !name || !values || count == 0 || cl_schema_find_attr(block, name)) {
        return -1;
    }
    for (size_t i = 0; i < count; i++) {
        if (!values[i]) {
            return -1;
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(values[i], values[j]) == 0) {
                return -1;
            }
        }
    }
    cl_schema_attr_t *attr = cl_schema_new_attr(block, name, CL_TYPE_STRING, required);
    cl_arena_t *arena = &block->owner->arena;
    attr->values = cl_arena_alloc_raw(arena, count * sizeof(char *));
    for (size_t i = 0; i < count; i++) {
        attr->values[i] = cl_arena_strdup_raw(arena, values[i]);
    }
    attr->value_count = count;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Validation                                                           */
/* ------------------------------------------------------------------ */

static int cl_schema_fail(cl_error_t *err, int line, int col, const char *fmt, ...) {
    if (err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err->message, sizeof(err->message), fmt, ap);
        va_end(ap);
        err->line = line;
        err->col = col;
    }
    return -1;
}

static const char *cl_schema_type_name(cl_schema_type_t type) {
    switch (type) {
        case CL_TYPE_ANY: return "any";
        case CL_TYPE_STRING: return "string";
        case CL_TYPE_NUMBER: return "number";
        case CL_TYPE_BOOL: return "bool";
        case CL_TYPE_LIST: return "list";
        case CL_TYPE_OBJECT: return "object";
    }
    return "?";
}

static const char *cl_value_kind_name(cl_value_kind_t kind) {
    switch (kind) {
        case CL_VAL_STRING: return "string";
        case CL_VAL_NUMBER: return "number";
        case CL_VAL_BOOL: return "bool";
        case CL_VAL_NULL: return "null";
        case CL_VAL_LIST: return "list";
        case CL_VAL_OBJECT: return "object";
    }
    return "?";
}

static int cl_schema_type_accepts(cl_schema_type_t type, cl_value_kind_t kind) {
    switch (type) {
        case CL_TYPE_ANY: return 1;
        case CL_TYPE_STRING: return kind == CL_VAL_STRING;
        case CL_TYPE_NUMBER: return kind == CL_VAL_NUMBER;
        case CL_TYPE_BOOL: return kind == CL_VAL_BOOL;
        case CL_TYPE_LIST: return kind == CL_VAL_LIST;
        case CL_TYPE_OBJECT: return kind == CL_VAL_OBJECT;
    }
    return 0;
}

/* The value kind a literal expression is certain to evaluate to, so its
 * type can be checked without evaluating. Returns 0 for anything else
 * (traversals, operators, calls, interpolated templates, ...). */
static int cl_literal_kind(const cl_expr_t *expr, cl_value_kind_t *out) {
    switch (expr->kind) {
        case CL_EXPR_STRING: *out = CL_VAL_STRING; return 1;
        case CL_EXPR_NUMBER: *out = CL_VAL_NUMBER; return 1;
        case CL_EXPR_BOOL: *out = CL_VAL_BOOL; return 1;
        case CL_EXPR_NULL: *out = CL_VAL_NULL; return 1;
        case CL_EXPR_TUPLE: *out = CL_VAL_LIST; return 1;
        case CL_EXPR_OBJECT: *out = CL_VAL_OBJECT; return 1;
        default: return 0;
    }
}

static int cl_schema_enum_has(const cl_schema_attr_t *attr, const char *text) {
    for (size_t i = 0; i < attr->value_count; i++) {
        if (strcmp(attr->values[i], text) == 0) {
            return 1;
        }
    }
    return 0;
}

/* "a, b, c" into `out`, truncated to fit (error messages are 256 bytes). */
static void cl_schema_enum_join(const cl_schema_attr_t *attr, char *out, size_t out_size) {
    size_t len = 0;
    out[0] = '\0';
    for (size_t i = 0; i < attr->value_count && len < out_size; i++) {
        int n = snprintf(out + len, out_size - len, "%s%s", i > 0 ? ", " : "", attr->values[i]);
        if (n < 0) {
            break;
        }
        len += (size_t)n;
    }
}

static int cl_schema_check_result_shape(const cl_body_t *body, const cl_evaluated_body_t *ebody, int line, int col,
                                        cl_error_t *err) {
    if (ebody && ebody->count != body->count) {
        return cl_schema_fail(err, line, col, "result does not match the document");
    }
    return 0;
}

/* `eblock` is the evaluated counterpart of `block` (NULL when validating
 * without a result): cl_eval_build_body() produces exactly one evaluated
 * item per document item, in the same order, so both are walked by index. */
static int cl_schema_validate_block(const cl_schema_block_t *rule, const cl_block_t *block,
                                    const cl_evaluated_block_t *eblock, cl_error_t *err) {
    const cl_body_t *body = block->body;
    const cl_evaluated_body_t *ebody = eblock ? eblock->body : NULL;
    if (cl_schema_check_result_shape(body, ebody, block->line, block->col, err) != 0) {
        return -1;
    }

    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        const cl_evaluated_item_t *eitem = ebody ? &ebody->items[i] : NULL;
        if (eitem && eitem->kind != item->kind) {
            return cl_schema_fail(err, block->line, block->col, "result does not match the document");
        }

        if (item->kind == CL_ITEM_ATTRIBUTE) {
            const cl_attribute_t *attr = item->as.attribute;
            const cl_schema_attr_t *attr_rule = cl_schema_find_attr(rule, attr->name);
            if (!attr_rule) {
                return cl_schema_fail(err, attr->line, attr->col, "attribute '%s' not allowed in block '%s'",
                                      attr->name, rule->type);
            }
            for (size_t j = 0; j < i; j++) {
                if (body->items[j].kind == CL_ITEM_ATTRIBUTE &&
                    strcmp(body->items[j].as.attribute->name, attr->name) == 0) {
                    return cl_schema_fail(err, attr->line, attr->col, "duplicate attribute '%s' in block '%s'",
                                          attr->name, rule->type);
                }
            }
            cl_value_kind_t kind;
            int known = 0;
            const char *text = NULL; /* the string value, when known */
            if (eitem) {
                kind = eitem->as.attribute->value->kind;
                known = 1;
                text = cl_value_as_string(eitem->as.attribute->value);
            } else {
                known = cl_literal_kind(attr->value, &kind);
                text = cl_expr_as_string(attr->value);
            }
            if (known && !cl_schema_type_accepts(attr_rule->type, kind)) {
                return cl_schema_fail(err, attr->line, attr->col, "attribute '%s' in block '%s' must be %s, but is %s",
                                      attr->name, rule->type, cl_schema_type_name(attr_rule->type),
                                      cl_value_kind_name(kind));
            }
            if (text && attr_rule->values && !cl_schema_enum_has(attr_rule, text)) {
                char allowed[160];
                cl_schema_enum_join(attr_rule, allowed, sizeof(allowed));
                return cl_schema_fail(err, attr->line, attr->col,
                                      "attribute '%s' in block '%s' must be one of: %s; but is '%s'", attr->name,
                                      rule->type, allowed, text);
            }
        } else {
            const cl_block_t *child = item->as.block;
            const cl_schema_block_t *child_rule = cl_schema_find_block(rule, child->type);
            if (!child_rule) {
                return cl_schema_fail(err, child->line, child->col, "block '%s' not allowed inside '%s'",
                                      child->type, rule->type);
            }
            if (cl_schema_validate_block(child_rule, child, eitem ? eitem->as.block : NULL, err) != 0) {
                return -1;
            }
        }
    }

    for (size_t r = 0; r < rule->attr_count; r++) {
        const cl_schema_attr_t *attr_rule = rule->attrs[r];
        if (attr_rule->required && !cl_body_get_attribute(body, attr_rule->name)) {
            return cl_schema_fail(err, block->line, block->col, "required attribute '%s' missing in block '%s'",
                                  attr_rule->name, rule->type);
        }
    }
    return 0;
}

int cl_schema_validate(const cl_schema_t *schema, const cl_document_t *doc, const cl_evaluated_t *result,
                       cl_error_t *err) {
    if (!schema || !doc) {
        return cl_schema_fail(err, 0, 0, "missing schema or document");
    }
    const cl_body_t *root = doc->root;
    const cl_evaluated_body_t *eroot = result ? result->root : NULL;
    if (cl_schema_check_result_shape(root, eroot, 1, 1, err) != 0) {
        return -1;
    }

    for (size_t i = 0; i < root->count; i++) {
        const cl_body_item_t *item = &root->items[i];
        if (item->kind != CL_ITEM_BLOCK) {
            continue; /* top-level attributes are never checked */
        }
        const cl_evaluated_item_t *eitem = eroot ? &eroot->items[i] : NULL;
        if (eitem && eitem->kind != CL_ITEM_BLOCK) {
            return cl_schema_fail(err, 1, 1, "result does not match the document");
        }
        const cl_block_t *block = item->as.block;
        const cl_schema_block_t *rule = cl_schema_find_block(&schema->root, block->type);
        if (!rule) {
            if (schema->strict) {
                return cl_schema_fail(err, block->line, block->col, "block type '%s' not registered in the schema",
                                      block->type);
            }
            continue;
        }
        if (cl_schema_validate_block(rule, block, eitem ? eitem->as.block : NULL, err) != 0) {
            return -1;
        }
    }
    return 0;
}
