#include "cl/cl.h"

#include <stdlib.h>
#include <string.h>

cl_attribute_t *cl_body_get_attribute(const cl_body_t *body, const char *name) {
    if (!body) {
        return NULL;
    }
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_ATTRIBUTE && strcmp(item->as.attribute->name, name) == 0) {
            return item->as.attribute;
        }
    }
    return NULL;
}

size_t cl_body_find_blocks(const cl_body_t *body, const char *type, cl_block_t ***out_blocks) {
    if (out_blocks) {
        *out_blocks = NULL;
    }
    if (!body) {
        return 0;
    }

    size_t match_count = 0;
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && strcmp(item->as.block->type, type) == 0) {
            match_count++;
        }
    }
    if (match_count == 0 || !out_blocks) {
        return match_count;
    }

    cl_block_t **matches = malloc(match_count * sizeof(cl_block_t *));
    if (!matches) {
        abort();
    }
    size_t next = 0;
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && strcmp(item->as.block->type, type) == 0) {
            matches[next++] = item->as.block;
        }
    }
    *out_blocks = matches;
    return match_count;
}

static int cl_labels_match(const cl_block_t *block, const char *const *labels, size_t label_count) {
    if (block->label_count != label_count) {
        return 0;
    }
    for (size_t i = 0; i < label_count; i++) {
        if (strcmp(block->labels[i], labels[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

cl_block_t *cl_body_find_block(const cl_body_t *body, const char *type, const char *const *labels,
                                size_t label_count) {
    if (!body) {
        return NULL;
    }
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && strcmp(item->as.block->type, type) == 0 &&
            cl_labels_match(item->as.block, labels, label_count)) {
            return item->as.block;
        }
    }
    return NULL;
}

cl_expr_kind_t cl_expr_kind(const cl_expr_t *expr) {
    return expr->kind;
}

const char *cl_expr_as_string(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_STRING) {
        return NULL;
    }
    return expr->as.string_value;
}

int cl_expr_as_number(const cl_expr_t *expr, double *out) {
    if (!expr || expr->kind != CL_EXPR_NUMBER) {
        return -1;
    }
    if (out) {
        *out = expr->as.number_value;
    }
    return 0;
}

int cl_expr_as_bool(const cl_expr_t *expr, int *out) {
    if (!expr || expr->kind != CL_EXPR_BOOL) {
        return -1;
    }
    if (out) {
        *out = expr->as.bool_value;
    }
    return 0;
}

size_t cl_expr_object_count(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_OBJECT) {
        return 0;
    }
    return expr->as.object.count;
}

const char *cl_expr_object_key_at(const cl_expr_t *expr, size_t index) {
    if (!expr || expr->kind != CL_EXPR_OBJECT || index >= expr->as.object.count) {
        return NULL;
    }
    return expr->as.object.items[index].key;
}

cl_expr_t *cl_expr_object_value_at(const cl_expr_t *expr, size_t index) {
    if (!expr || expr->kind != CL_EXPR_OBJECT || index >= expr->as.object.count) {
        return NULL;
    }
    return expr->as.object.items[index].value;
}

cl_expr_t *cl_expr_object_get(const cl_expr_t *expr, const char *key) {
    if (!expr || expr->kind != CL_EXPR_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < expr->as.object.count; i++) {
        if (strcmp(expr->as.object.items[i].key, key) == 0) {
            return expr->as.object.items[i].value;
        }
    }
    return NULL;
}

const char *cl_expr_traversal_root(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_TRAVERSAL) {
        return NULL;
    }
    return expr->as.traversal.root;
}

size_t cl_expr_traversal_step_count(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_TRAVERSAL) {
        return 0;
    }
    return expr->as.traversal.count;
}

const cl_traversal_step_t *cl_expr_traversal_step_at(const cl_expr_t *expr, size_t index) {
    if (!expr || expr->kind != CL_EXPR_TRAVERSAL || index >= expr->as.traversal.count) {
        return NULL;
    }
    return &expr->as.traversal.steps[index];
}

cl_expr_t *cl_expr_postfix_base(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_POSTFIX) {
        return NULL;
    }
    return expr->as.postfix.base;
}

size_t cl_expr_postfix_step_count(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_POSTFIX) {
        return 0;
    }
    return expr->as.postfix.count;
}

const cl_traversal_step_t *cl_expr_postfix_step_at(const cl_expr_t *expr, size_t index) {
    if (!expr || expr->kind != CL_EXPR_POSTFIX || index >= expr->as.postfix.count) {
        return NULL;
    }
    return &expr->as.postfix.steps[index];
}

size_t cl_expr_tuple_count(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_TUPLE) {
        return 0;
    }
    return expr->as.tuple.count;
}

cl_expr_t *cl_expr_tuple_at(const cl_expr_t *expr, size_t index) {
    if (!expr || expr->kind != CL_EXPR_TUPLE || index >= expr->as.tuple.count) {
        return NULL;
    }
    return expr->as.tuple.items[index];
}

const cl_template_t *cl_expr_template(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_TEMPLATE) {
        return NULL;
    }
    return expr->as.tpl;
}

size_t cl_template_part_count(const cl_template_t *tpl) {
    if (!tpl) {
        return 0;
    }
    return tpl->count;
}

const cl_template_part_t *cl_template_part_at(const cl_template_t *tpl, size_t index) {
    if (!tpl || index >= tpl->count) {
        return NULL;
    }
    return &tpl->parts[index];
}

const char *cl_expr_call_name(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_CALL) {
        return NULL;
    }
    return expr->as.call.name;
}

size_t cl_expr_call_arg_count(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_CALL) {
        return 0;
    }
    return expr->as.call.count;
}

cl_expr_t *cl_expr_call_arg_at(const cl_expr_t *expr, size_t index) {
    if (!expr || expr->kind != CL_EXPR_CALL || index >= expr->as.call.count) {
        return NULL;
    }
    return expr->as.call.args[index];
}

int cl_expr_call_expand_final(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_CALL) {
        return 0;
    }
    return expr->as.call.expand_final;
}

int cl_expr_unary_op(const cl_expr_t *expr, cl_unary_op_t *out) {
    if (!expr || expr->kind != CL_EXPR_UNARY) {
        return -1;
    }
    if (out) {
        *out = expr->as.unary.op;
    }
    return 0;
}

cl_expr_t *cl_expr_unary_operand(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_UNARY) {
        return NULL;
    }
    return expr->as.unary.operand;
}

int cl_expr_binary_op(const cl_expr_t *expr, cl_binary_op_t *out) {
    if (!expr || expr->kind != CL_EXPR_BINARY) {
        return -1;
    }
    if (out) {
        *out = expr->as.binary.op;
    }
    return 0;
}

cl_expr_t *cl_expr_binary_left(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_BINARY) {
        return NULL;
    }
    return expr->as.binary.left;
}

cl_expr_t *cl_expr_binary_right(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_BINARY) {
        return NULL;
    }
    return expr->as.binary.right;
}

cl_expr_t *cl_expr_conditional_cond(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_CONDITIONAL) {
        return NULL;
    }
    return expr->as.conditional.cond;
}

cl_expr_t *cl_expr_conditional_then(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_CONDITIONAL) {
        return NULL;
    }
    return expr->as.conditional.then_expr;
}

cl_expr_t *cl_expr_conditional_else(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_CONDITIONAL) {
        return NULL;
    }
    return expr->as.conditional.else_expr;
}

const char *cl_expr_for_key_var(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return NULL;
    }
    return expr->as.for_expr.key_var;
}

const char *cl_expr_for_val_var(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return NULL;
    }
    return expr->as.for_expr.val_var;
}

cl_expr_t *cl_expr_for_collection(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return NULL;
    }
    return expr->as.for_expr.collection;
}

cl_expr_t *cl_expr_for_key_expr(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return NULL;
    }
    return expr->as.for_expr.key_expr;
}

cl_expr_t *cl_expr_for_value_expr(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return NULL;
    }
    return expr->as.for_expr.value_expr;
}

cl_expr_t *cl_expr_for_cond(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return NULL;
    }
    return expr->as.for_expr.cond;
}

int cl_expr_for_is_object(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return 0;
    }
    return expr->as.for_expr.is_object;
}

int cl_expr_for_grouping(const cl_expr_t *expr) {
    if (!expr || expr->kind != CL_EXPR_FOR) {
        return 0;
    }
    return expr->as.for_expr.grouping;
}
