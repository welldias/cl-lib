#include "cl_ast.h"

#include <string.h>

static int cl_body_remove_item_at(cl_body_t *body, size_t index) {
    if (index >= body->count) {
        return -1;
    }
    memmove(&body->items[index], &body->items[index + 1],
            (body->count - index - 1) * sizeof(cl_body_item_t));
    body->count--;
    return 0;
}

static cl_attribute_t *cl_body_set_expr(cl_document_t *doc, cl_body_t *body, const char *name, cl_expr_t *value) {
    cl_attribute_t *existing = cl_body_get_attribute(body, name);
    if (existing) {
        existing->value = value;
        return existing;
    }
    return cl_body_append_attribute(doc, body, name, value, 0, 0);
}

cl_attribute_t *cl_body_set_string(cl_document_t *doc, cl_body_t *body, const char *name, const char *value) {
    return cl_body_set_expr(doc, body, name, cl_expr_new_string(doc, value, 0, 0));
}

cl_attribute_t *cl_body_set_number(cl_document_t *doc, cl_body_t *body, const char *name, double value) {
    return cl_body_set_expr(doc, body, name, cl_expr_new_number(doc, value, 0, 0));
}

cl_attribute_t *cl_body_set_bool(cl_document_t *doc, cl_body_t *body, const char *name, int value) {
    return cl_body_set_expr(doc, body, name, cl_expr_new_bool(doc, value, 0, 0));
}

cl_attribute_t *cl_body_set_null(cl_document_t *doc, cl_body_t *body, const char *name) {
    return cl_body_set_expr(doc, body, name, cl_expr_new_null(doc, 0, 0));
}

int cl_body_remove_attribute(cl_body_t *body, const char *name) {
    for (size_t i = 0; i < body->count; i++) {
        cl_body_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_ATTRIBUTE && strcmp(item->as.attribute->name, name) == 0) {
            return cl_body_remove_item_at(body, i);
        }
    }
    return -1;
}

cl_block_t *cl_body_add_block(cl_document_t *doc, cl_body_t *body, const char *type,
                               const char *const *labels, size_t label_count) {
    char **owned_labels = NULL;
    size_t owned_count = 0;
    size_t owned_capacity = 0;
    for (size_t i = 0; i < label_count; i++) {
        cl_label_list_add(doc, &owned_labels, &owned_count, &owned_capacity, labels[i]);
    }
    cl_body_t *child_body = cl_body_new(doc);
    return cl_body_append_block(doc, body, type, owned_labels, NULL, owned_count, child_body, 0, 0);
}

int cl_body_remove_block(cl_body_t *parent, cl_block_t *block) {
    for (size_t i = 0; i < parent->count; i++) {
        cl_body_item_t *item = &parent->items[i];
        if (item->kind == CL_ITEM_BLOCK && item->as.block == block) {
            return cl_body_remove_item_at(parent, i);
        }
    }
    return -1;
}
