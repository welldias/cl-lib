#include "cl_eval.h"
#include "cl_writer.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Evaluated values and bodies in cl syntax                             */
/* ------------------------------------------------------------------ */

static void cl_export_value(cl_buf_t *buf, const cl_value_t *value, int indent) {
    switch (value->kind) {
        case CL_VAL_STRING: cl_write_string_literal(buf, value->as.string_value); break;
        case CL_VAL_NUMBER: cl_write_number(buf, value->as.number_value); break;
        case CL_VAL_BOOL: cl_buf_append(buf, value->as.bool_value ? "true" : "false"); break;
        case CL_VAL_NULL: cl_buf_append(buf, "null"); break;
        case CL_VAL_LIST:
            cl_buf_append_char(buf, '[');
            for (size_t i = 0; i < value->as.list.count; i++) {
                if (i > 0) {
                    cl_buf_append(buf, ", ");
                }
                cl_export_value(buf, value->as.list.items[i], indent);
            }
            cl_buf_append_char(buf, ']');
            break;
        case CL_VAL_OBJECT:
            if (value->as.object.count == 0) {
                cl_buf_append(buf, "{}");
                break;
            }
            cl_buf_append(buf, "{\n");
            for (size_t i = 0; i < value->as.object.count; i++) {
                const cl_value_object_item_t *item = &value->as.object.items[i];
                cl_buf_append_indent(buf, indent + 1);
                if (cl_is_bare_ident(item->key)) {
                    cl_buf_append(buf, item->key);
                } else {
                    cl_write_string_literal(buf, item->key);
                }
                cl_buf_append(buf, " = ");
                cl_export_value(buf, item->value, indent + 1);
                cl_buf_append_char(buf, '\n');
            }
            cl_buf_append_indent(buf, indent);
            cl_buf_append_char(buf, '}');
            break;
    }
}

static void cl_export_body(cl_buf_t *buf, const cl_evaluated_body_t *body, int indent);

static void cl_export_block(cl_buf_t *buf, const cl_evaluated_block_t *block, int indent) {
    cl_buf_append_indent(buf, indent);
    cl_buf_append(buf, block->type);
    for (size_t l = 0; l < block->label_count; l++) {
        cl_buf_append_char(buf, ' ');
        cl_write_string_literal(buf, block->labels[l]);
    }
    cl_buf_append(buf, " {\n");
    cl_export_body(buf, block->body, indent + 1);
    cl_buf_append_indent(buf, indent);
    cl_buf_append(buf, "}\n");
}

static void cl_export_body(cl_buf_t *buf, const cl_evaluated_body_t *body, int indent) {
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_ATTRIBUTE) {
            cl_buf_append_indent(buf, indent);
            cl_buf_append(buf, item->as.attribute->name);
            cl_buf_append(buf, " = ");
            cl_export_value(buf, item->as.attribute->value, indent);
            cl_buf_append_char(buf, '\n');
        } else {
            cl_export_block(buf, item->as.block, indent);
        }
    }
}

char *cl_value_to_string(const cl_value_t *value) {
    if (!value) {
        return NULL;
    }
    cl_buf_t buf = {0};
    cl_export_value(&buf, value, 0);
    return cl_buf_finish(&buf);
}

char *cl_evaluated_to_string(const cl_evaluated_t *result) {
    if (!result) {
        return NULL;
    }
    cl_buf_t buf = {0};
    cl_export_body(&buf, result->root, 0);
    return cl_buf_finish(&buf);
}

char *cl_evaluated_block_to_string(const cl_evaluated_block_t *block) {
    if (!block) {
        return NULL;
    }
    cl_buf_t buf = {0};
    cl_export_block(&buf, block, 0);
    return cl_buf_finish(&buf);
}

/* ------------------------------------------------------------------ */
/* JSON                                                                 */
/* ------------------------------------------------------------------ */

static void cl_json_string(cl_buf_t *buf, const char *s) {
    cl_buf_append_char(buf, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
            case '"': cl_buf_append(buf, "\\\""); break;
            case '\\': cl_buf_append(buf, "\\\\"); break;
            case '\b': cl_buf_append(buf, "\\b"); break;
            case '\f': cl_buf_append(buf, "\\f"); break;
            case '\n': cl_buf_append(buf, "\\n"); break;
            case '\r': cl_buf_append(buf, "\\r"); break;
            case '\t': cl_buf_append(buf, "\\t"); break;
            default:
                if (*p < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)*p);
                    cl_buf_append(buf, tmp);
                } else {
                    cl_buf_append_char(buf, (char)*p); /* UTF-8 passes through */
                }
                break;
        }
    }
    cl_buf_append_char(buf, '"');
}

static void cl_json_value(cl_buf_t *buf, const cl_value_t *value, int indent) {
    switch (value->kind) {
        case CL_VAL_STRING: cl_json_string(buf, value->as.string_value); break;
        case CL_VAL_NUMBER: {
            char tmp[64];
            /* JSON has no NaN or infinities */
            cl_buf_append(buf, cl_format_number(tmp, sizeof(tmp), value->as.number_value) == 0 ? tmp : "null");
            break;
        }
        case CL_VAL_BOOL: cl_buf_append(buf, value->as.bool_value ? "true" : "false"); break;
        case CL_VAL_NULL: cl_buf_append(buf, "null"); break;
        case CL_VAL_LIST:
            if (value->as.list.count == 0) {
                cl_buf_append(buf, "[]");
                break;
            }
            cl_buf_append(buf, "[\n");
            for (size_t i = 0; i < value->as.list.count; i++) {
                cl_buf_append_indent(buf, indent + 1);
                cl_json_value(buf, value->as.list.items[i], indent + 1);
                cl_buf_append(buf, i + 1 < value->as.list.count ? ",\n" : "\n");
            }
            cl_buf_append_indent(buf, indent);
            cl_buf_append_char(buf, ']');
            break;
        case CL_VAL_OBJECT:
            if (value->as.object.count == 0) {
                cl_buf_append(buf, "{}");
                break;
            }
            cl_buf_append(buf, "{\n");
            for (size_t i = 0; i < value->as.object.count; i++) {
                cl_buf_append_indent(buf, indent + 1);
                cl_json_string(buf, value->as.object.items[i].key);
                cl_buf_append(buf, ": ");
                cl_json_value(buf, value->as.object.items[i].value, indent + 1);
                cl_buf_append(buf, i + 1 < value->as.object.count ? ",\n" : "\n");
            }
            cl_buf_append_indent(buf, indent);
            cl_buf_append_char(buf, '}');
            break;
    }
}

/* True when an earlier attribute of `body` (before index `before`) has the
 * same name - JSON keys must be unique, and the first one is the one every
 * lookup sees. */
static int cl_json_is_shadowed(const cl_evaluated_body_t *body, size_t before, const char *name) {
    for (size_t i = 0; i < before; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_ATTRIBUTE && strcmp(item->as.attribute->name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

static void cl_json_body(cl_buf_t *buf, const cl_evaluated_body_t *body, int indent);

/* { "type": ..., "labels": [...], "body": {...} } */
static void cl_json_block(cl_buf_t *buf, const cl_evaluated_block_t *block, int indent) {
    cl_buf_append(buf, "{\n");
    cl_buf_append_indent(buf, indent + 1);
    cl_buf_append(buf, "\"type\": ");
    cl_json_string(buf, block->type);
    cl_buf_append(buf, ",\n");
    cl_buf_append_indent(buf, indent + 1);
    cl_buf_append(buf, "\"labels\": [");
    for (size_t l = 0; l < block->label_count; l++) {
        if (l > 0) {
            cl_buf_append(buf, ", ");
        }
        cl_json_string(buf, block->labels[l]);
    }
    cl_buf_append(buf, "],\n");
    cl_buf_append_indent(buf, indent + 1);
    cl_buf_append(buf, "\"body\": ");
    cl_json_body(buf, block->body, indent + 1);
    cl_buf_append_char(buf, '\n');
    cl_buf_append_indent(buf, indent);
    cl_buf_append_char(buf, '}');
}

/* { "attributes": {...}, "blocks": [...] } */
static void cl_json_body(cl_buf_t *buf, const cl_evaluated_body_t *body, int indent) {
    size_t attr_count = 0;
    size_t block_count = 0;
    for (size_t i = 0; i < body->count; i++) {
        if (body->items[i].kind == CL_ITEM_BLOCK) {
            block_count++;
        } else if (!cl_json_is_shadowed(body, i, body->items[i].as.attribute->name)) {
            attr_count++;
        }
    }

    cl_buf_append(buf, "{\n");
    cl_buf_append_indent(buf, indent + 1);
    cl_buf_append(buf, "\"attributes\": ");
    if (attr_count == 0) {
        cl_buf_append(buf, "{}");
    } else {
        cl_buf_append(buf, "{\n");
        size_t written = 0;
        for (size_t i = 0; i < body->count; i++) {
            const cl_evaluated_item_t *item = &body->items[i];
            if (item->kind != CL_ITEM_ATTRIBUTE || cl_json_is_shadowed(body, i, item->as.attribute->name)) {
                continue;
            }
            cl_buf_append_indent(buf, indent + 2);
            cl_json_string(buf, item->as.attribute->name);
            cl_buf_append(buf, ": ");
            cl_json_value(buf, item->as.attribute->value, indent + 2);
            cl_buf_append(buf, ++written < attr_count ? ",\n" : "\n");
        }
        cl_buf_append_indent(buf, indent + 1);
        cl_buf_append_char(buf, '}');
    }
    cl_buf_append(buf, ",\n");

    cl_buf_append_indent(buf, indent + 1);
    cl_buf_append(buf, "\"blocks\": ");
    if (block_count == 0) {
        cl_buf_append(buf, "[]");
    } else {
        cl_buf_append(buf, "[\n");
        size_t written = 0;
        for (size_t i = 0; i < body->count; i++) {
            if (body->items[i].kind != CL_ITEM_BLOCK) {
                continue;
            }
            cl_buf_append_indent(buf, indent + 2);
            cl_json_block(buf, body->items[i].as.block, indent + 2);
            cl_buf_append(buf, ++written < block_count ? ",\n" : "\n");
        }
        cl_buf_append_indent(buf, indent + 1);
        cl_buf_append_char(buf, ']');
    }
    cl_buf_append_char(buf, '\n');
    cl_buf_append_indent(buf, indent);
    cl_buf_append_char(buf, '}');
}

char *cl_value_to_json(const cl_value_t *value) {
    if (!value) {
        return NULL;
    }
    cl_buf_t buf = {0};
    cl_json_value(&buf, value, 0);
    return cl_buf_finish(&buf);
}

char *cl_evaluated_to_json(const cl_evaluated_t *result) {
    if (!result) {
        return NULL;
    }
    cl_buf_t buf = {0};
    cl_json_body(&buf, result->root, 0);
    cl_buf_append_char(&buf, '\n');
    return cl_buf_finish(&buf);
}

char *cl_evaluated_block_to_json(const cl_evaluated_block_t *block) {
    if (!block) {
        return NULL;
    }
    cl_buf_t buf = {0};
    cl_json_block(&buf, block, 0);
    cl_buf_append_char(&buf, '\n');
    return cl_buf_finish(&buf);
}
