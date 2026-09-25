#include "cl_ast.h"

cl_body_t *cl_body_new(cl_document_t *doc) {
    cl_body_t *body = cl_arena_alloc(doc, sizeof(cl_body_t));
    body->items = NULL;
    body->count = 0;
    body->capacity = 0;
    return body;
}

cl_attribute_t *cl_body_append_attribute(cl_document_t *doc, cl_body_t *body, const char *name,
                                          cl_expr_t *value, int line, int col) {
    cl_attribute_t *attr = cl_arena_alloc(doc, sizeof(cl_attribute_t));
    attr->name = cl_arena_strdup(doc, name);
    attr->value = value;
    attr->line = line;
    attr->col = col;

    cl_array_grow(doc, (void **)&body->items, &body->count, &body->capacity, sizeof(cl_body_item_t));
    cl_body_item_t *item = &body->items[body->count++];
    item->kind = CL_ITEM_ATTRIBUTE;
    item->as.attribute = attr;
    return attr;
}

cl_block_t *cl_body_append_block(cl_document_t *doc, cl_body_t *body, const char *type,
                                  char **labels, size_t label_count, cl_body_t *block_body,
                                  int line, int col) {
    cl_block_t *block = cl_arena_alloc(doc, sizeof(cl_block_t));
    block->type = cl_arena_strdup(doc, type);
    block->labels = labels;
    block->label_count = label_count;
    block->label_capacity = label_count;
    block->body = block_body;
    block->line = line;
    block->col = col;

    cl_array_grow(doc, (void **)&body->items, &body->count, &body->capacity, sizeof(cl_body_item_t));
    cl_body_item_t *item = &body->items[body->count++];
    item->kind = CL_ITEM_BLOCK;
    item->as.block = block;
    return block;
}

void cl_label_list_add(cl_document_t *doc, char ***labels, size_t *count, size_t *capacity, const char *label) {
    cl_array_grow(doc, (void **)labels, count, capacity, sizeof(char *));
    (*labels)[(*count)++] = cl_arena_strdup(doc, label);
}

static cl_expr_t *cl_expr_new(cl_document_t *doc, cl_expr_kind_t kind, int line, int col) {
    cl_expr_t *expr = cl_arena_alloc(doc, sizeof(cl_expr_t));
    expr->kind = kind;
    expr->line = line;
    expr->col = col;
    return expr;
}

cl_expr_t *cl_expr_new_string(cl_document_t *doc, const char *value, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_STRING, line, col);
    expr->as.string_value = cl_arena_strdup(doc, value);
    return expr;
}

cl_expr_t *cl_expr_new_number(cl_document_t *doc, double value, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_NUMBER, line, col);
    expr->as.number_value = value;
    return expr;
}

cl_expr_t *cl_expr_new_bool(cl_document_t *doc, int value, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_BOOL, line, col);
    expr->as.bool_value = value;
    return expr;
}

cl_expr_t *cl_expr_new_null(cl_document_t *doc, int line, int col) {
    return cl_expr_new(doc, CL_EXPR_NULL, line, col);
}

cl_expr_t *cl_expr_new_object(cl_document_t *doc, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_OBJECT, line, col);
    expr->as.object.items = NULL;
    expr->as.object.count = 0;
    expr->as.object.capacity = 0;
    return expr;
}

cl_expr_t *cl_expr_new_traversal(cl_document_t *doc, const char *root, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_TRAVERSAL, line, col);
    expr->as.traversal.root = cl_arena_strdup(doc, root);
    expr->as.traversal.steps = NULL;
    expr->as.traversal.count = 0;
    expr->as.traversal.capacity = 0;
    return expr;
}

void cl_expr_object_add(cl_document_t *doc, cl_expr_t *obj, const char *key, cl_expr_t *value) {
    cl_array_grow(doc, (void **)&obj->as.object.items, &obj->as.object.count, &obj->as.object.capacity,
                  sizeof(cl_object_item_t));
    cl_object_item_t *item = &obj->as.object.items[obj->as.object.count++];
    item->key = cl_arena_strdup(doc, key);
    item->value = value;
}

static cl_traversal_step_t *cl_expr_traversal_push(cl_document_t *doc, cl_expr_t *trav) {
    cl_array_grow(doc, (void **)&trav->as.traversal.steps, &trav->as.traversal.count,
                  &trav->as.traversal.capacity, sizeof(cl_traversal_step_t));
    return &trav->as.traversal.steps[trav->as.traversal.count++];
}

void cl_expr_traversal_add_attr(cl_document_t *doc, cl_expr_t *trav, const char *name) {
    cl_traversal_step_t *step = cl_expr_traversal_push(doc, trav);
    step->kind = CL_STEP_ATTR;
    step->name = cl_arena_strdup(doc, name);
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_traversal_add_index_number(cl_document_t *doc, cl_expr_t *trav, double index) {
    cl_traversal_step_t *step = cl_expr_traversal_push(doc, trav);
    step->kind = CL_STEP_INDEX_NUMBER;
    step->name = NULL;
    step->index = index;
    step->expr = NULL;
}

void cl_expr_traversal_add_index_string(cl_document_t *doc, cl_expr_t *trav, const char *name) {
    cl_traversal_step_t *step = cl_expr_traversal_push(doc, trav);
    step->kind = CL_STEP_INDEX_STRING;
    step->name = cl_arena_strdup(doc, name);
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_traversal_add_splat_attr(cl_document_t *doc, cl_expr_t *trav) {
    cl_traversal_step_t *step = cl_expr_traversal_push(doc, trav);
    step->kind = CL_STEP_SPLAT_ATTR;
    step->name = NULL;
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_traversal_add_splat_full(cl_document_t *doc, cl_expr_t *trav) {
    cl_traversal_step_t *step = cl_expr_traversal_push(doc, trav);
    step->kind = CL_STEP_SPLAT_FULL;
    step->name = NULL;
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_traversal_add_index_expr(cl_document_t *doc, cl_expr_t *trav, cl_expr_t *index) {
    cl_traversal_step_t *step = cl_expr_traversal_push(doc, trav);
    step->kind = CL_STEP_INDEX_EXPR;
    step->name = NULL;
    step->index = 0.0;
    step->expr = index;
}

cl_expr_t *cl_expr_new_postfix(cl_document_t *doc, cl_expr_t *base, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_POSTFIX, line, col);
    expr->as.postfix.base = base;
    expr->as.postfix.steps = NULL;
    expr->as.postfix.count = 0;
    expr->as.postfix.capacity = 0;
    return expr;
}

static cl_traversal_step_t *cl_expr_postfix_push(cl_document_t *doc, cl_expr_t *expr) {
    cl_array_grow(doc, (void **)&expr->as.postfix.steps, &expr->as.postfix.count,
                  &expr->as.postfix.capacity, sizeof(cl_traversal_step_t));
    return &expr->as.postfix.steps[expr->as.postfix.count++];
}

void cl_expr_postfix_add_attr(cl_document_t *doc, cl_expr_t *expr, const char *name) {
    cl_traversal_step_t *step = cl_expr_postfix_push(doc, expr);
    step->kind = CL_STEP_ATTR;
    step->name = cl_arena_strdup(doc, name);
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_postfix_add_index_number(cl_document_t *doc, cl_expr_t *expr, double index) {
    cl_traversal_step_t *step = cl_expr_postfix_push(doc, expr);
    step->kind = CL_STEP_INDEX_NUMBER;
    step->name = NULL;
    step->index = index;
    step->expr = NULL;
}

void cl_expr_postfix_add_index_string(cl_document_t *doc, cl_expr_t *expr, const char *name) {
    cl_traversal_step_t *step = cl_expr_postfix_push(doc, expr);
    step->kind = CL_STEP_INDEX_STRING;
    step->name = cl_arena_strdup(doc, name);
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_postfix_add_splat_attr(cl_document_t *doc, cl_expr_t *expr) {
    cl_traversal_step_t *step = cl_expr_postfix_push(doc, expr);
    step->kind = CL_STEP_SPLAT_ATTR;
    step->name = NULL;
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_postfix_add_splat_full(cl_document_t *doc, cl_expr_t *expr) {
    cl_traversal_step_t *step = cl_expr_postfix_push(doc, expr);
    step->kind = CL_STEP_SPLAT_FULL;
    step->name = NULL;
    step->index = 0.0;
    step->expr = NULL;
}

void cl_expr_postfix_add_index_expr(cl_document_t *doc, cl_expr_t *expr, cl_expr_t *index) {
    cl_traversal_step_t *step = cl_expr_postfix_push(doc, expr);
    step->kind = CL_STEP_INDEX_EXPR;
    step->name = NULL;
    step->index = 0.0;
    step->expr = index;
}

cl_expr_t *cl_expr_new_tuple(cl_document_t *doc, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_TUPLE, line, col);
    expr->as.tuple.items = NULL;
    expr->as.tuple.count = 0;
    expr->as.tuple.capacity = 0;
    return expr;
}

void cl_expr_tuple_add(cl_document_t *doc, cl_expr_t *tuple, cl_expr_t *item) {
    cl_array_grow(doc, (void **)&tuple->as.tuple.items, &tuple->as.tuple.count, &tuple->as.tuple.capacity,
                  sizeof(cl_expr_t *));
    tuple->as.tuple.items[tuple->as.tuple.count++] = item;
}

cl_expr_t *cl_expr_new_template(cl_document_t *doc, cl_template_t *tpl, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_TEMPLATE, line, col);
    expr->as.tpl = tpl;
    return expr;
}

cl_expr_t *cl_expr_new_call(cl_document_t *doc, const char *name, int expand_final, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_CALL, line, col);
    expr->as.call.name = cl_arena_strdup(doc, name);
    expr->as.call.args = NULL;
    expr->as.call.count = 0;
    expr->as.call.capacity = 0;
    expr->as.call.expand_final = expand_final;
    return expr;
}

void cl_expr_call_add_arg(cl_document_t *doc, cl_expr_t *call, cl_expr_t *arg) {
    cl_array_grow(doc, (void **)&call->as.call.args, &call->as.call.count, &call->as.call.capacity,
                  sizeof(cl_expr_t *));
    call->as.call.args[call->as.call.count++] = arg;
}

cl_expr_t *cl_expr_new_unary(cl_document_t *doc, cl_unary_op_t op, cl_expr_t *operand, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_UNARY, line, col);
    expr->as.unary.op = op;
    expr->as.unary.operand = operand;
    return expr;
}

cl_expr_t *cl_expr_new_binary(cl_document_t *doc, cl_binary_op_t op, cl_expr_t *left, cl_expr_t *right,
                               int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_BINARY, line, col);
    expr->as.binary.op = op;
    expr->as.binary.left = left;
    expr->as.binary.right = right;
    return expr;
}

cl_expr_t *cl_expr_new_conditional(cl_document_t *doc, cl_expr_t *cond, cl_expr_t *then_expr,
                                    cl_expr_t *else_expr, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_CONDITIONAL, line, col);
    expr->as.conditional.cond = cond;
    expr->as.conditional.then_expr = then_expr;
    expr->as.conditional.else_expr = else_expr;
    return expr;
}

cl_expr_t *cl_expr_new_for(cl_document_t *doc, const char *key_var, const char *val_var,
                            cl_expr_t *collection, cl_expr_t *key_expr, cl_expr_t *value_expr,
                            cl_expr_t *cond, int is_object, int grouping, int line, int col) {
    cl_expr_t *expr = cl_expr_new(doc, CL_EXPR_FOR, line, col);
    expr->as.for_expr.key_var = key_var ? cl_arena_strdup(doc, key_var) : NULL;
    expr->as.for_expr.val_var = cl_arena_strdup(doc, val_var);
    expr->as.for_expr.collection = collection;
    expr->as.for_expr.key_expr = key_expr;
    expr->as.for_expr.value_expr = value_expr;
    expr->as.for_expr.cond = cond;
    expr->as.for_expr.is_object = is_object;
    expr->as.for_expr.grouping = grouping;
    return expr;
}

cl_template_t *cl_template_new(cl_document_t *doc) {
    cl_template_t *tpl = cl_arena_alloc(doc, sizeof(cl_template_t));
    tpl->parts = NULL;
    tpl->count = 0;
    tpl->capacity = 0;
    return tpl;
}

static cl_template_part_t *cl_template_push(cl_document_t *doc, cl_template_t *tpl) {
    cl_array_grow(doc, (void **)&tpl->parts, &tpl->count, &tpl->capacity, sizeof(cl_template_part_t));
    cl_template_part_t *part = &tpl->parts[tpl->count++];
    part->text = NULL;
    part->expr = NULL;
    part->if_cond = NULL;
    part->if_then = NULL;
    part->if_else = NULL;
    part->for_key_var = NULL;
    part->for_val_var = NULL;
    part->for_collection = NULL;
    part->for_body = NULL;
    return part;
}

cl_template_part_t *cl_template_add_literal(cl_document_t *doc, cl_template_t *tpl, const char *text) {
    cl_template_part_t *part = cl_template_push(doc, tpl);
    part->kind = CL_TPL_LITERAL;
    part->text = cl_arena_strdup(doc, text);
    return part;
}

cl_template_part_t *cl_template_add_interp(cl_document_t *doc, cl_template_t *tpl, cl_expr_t *expr) {
    cl_template_part_t *part = cl_template_push(doc, tpl);
    part->kind = CL_TPL_INTERP;
    part->expr = expr;
    return part;
}

cl_template_part_t *cl_template_add_if(cl_document_t *doc, cl_template_t *tpl, cl_expr_t *cond,
                                        cl_template_t *then_tpl, cl_template_t *else_tpl) {
    cl_template_part_t *part = cl_template_push(doc, tpl);
    part->kind = CL_TPL_IF;
    part->if_cond = cond;
    part->if_then = then_tpl;
    part->if_else = else_tpl;
    return part;
}

cl_template_part_t *cl_template_add_for(cl_document_t *doc, cl_template_t *tpl, const char *key_var,
                                         const char *val_var, cl_expr_t *collection, cl_template_t *body) {
    cl_template_part_t *part = cl_template_push(doc, tpl);
    part->kind = CL_TPL_FOR;
    part->for_key_var = key_var ? cl_arena_strdup(doc, key_var) : NULL;
    part->for_val_var = cl_arena_strdup(doc, val_var);
    part->for_collection = collection;
    part->for_body = body;
    return part;
}
