#include "cl_writer.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void cl_buf_reserve(cl_buf_t *buf, size_t extra) {
    if (buf->len + extra + 1 <= buf->capacity) {
        return;
    }
    size_t new_capacity = buf->capacity ? buf->capacity * 2 : 256;
    while (new_capacity < buf->len + extra + 1) {
        new_capacity *= 2;
    }
    char *new_data = realloc(buf->data, new_capacity);
    if (!new_data) {
        abort();
    }
    buf->data = new_data;
    buf->capacity = new_capacity;
}

void cl_buf_append(cl_buf_t *buf, const char *s) {
    size_t n = strlen(s);
    cl_buf_reserve(buf, n);
    memcpy(buf->data + buf->len, s, n);
    buf->len += n;
    buf->data[buf->len] = '\0';
}

void cl_buf_append_char(cl_buf_t *buf, char c) {
    cl_buf_reserve(buf, 1);
    buf->data[buf->len++] = c;
    buf->data[buf->len] = '\0';
}

void cl_buf_append_indent(cl_buf_t *buf, int indent) {
    for (int i = 0; i < indent; i++) {
        cl_buf_append(buf, "  ");
    }
}

char *cl_buf_finish(cl_buf_t *buf) {
    if (!buf->data) {
        buf->data = calloc(1, 1);
        if (!buf->data) {
            abort();
        }
    }
    return buf->data;
}

void cl_write_escaped_text(cl_buf_t *buf, const char *s) {
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"': cl_buf_append(buf, "\\\""); break;
            case '\\': cl_buf_append(buf, "\\\\"); break;
            case '\n': cl_buf_append(buf, "\\n"); break;
            case '\t': cl_buf_append(buf, "\\t"); break;
            case '\r': cl_buf_append(buf, "\\r"); break;
            case '$':
            case '%':
                cl_buf_append_char(buf, *p);
                if (p[1] == '{') {
                    cl_buf_append_char(buf, *p); /* "${" -> "$${", "%{" -> "%%{" */
                }
                break;
            default: cl_buf_append_char(buf, *p); break;
        }
    }
}

void cl_write_string_literal(cl_buf_t *buf, const char *s) {
    cl_buf_append_char(buf, '"');
    cl_write_escaped_text(buf, s);
    cl_buf_append_char(buf, '"');
}

int cl_format_number(char *out, size_t size, double value) {
    if (size > 0) {
        out[0] = '\0';
    }
    if (value != value || value - value != 0) {
        return -1; /* NaN, or an infinity (inf - inf is NaN) */
    }
    /* Range-checked before the cast: converting a double that doesn't fit
     * in a long long is undefined behavior. LLONG_MIN is a power of two, so
     * both bounds are exact doubles. */
    if (value >= (double)LLONG_MIN && value < -(double)LLONG_MIN && value == (double)(long long)value) {
        snprintf(out, size, "%lld", (long long)value);
        return 0;
    }
    for (int precision = 15; precision <= 17; precision++) {
        snprintf(out, size, "%.*g", precision, value);
        if (strtod(out, NULL) == value) {
            break;
        }
    }
    return 0;
}

void cl_write_number(cl_buf_t *buf, double value) {
    char tmp[64];
    if (cl_format_number(tmp, sizeof(tmp), value) == 0) {
        cl_buf_append(buf, tmp);
    } else if (value != value) {
        cl_buf_append(buf, "null");
    } else {
        cl_buf_append(buf, value > 0 ? "1e999" : "-1e999");
    }
}

int cl_is_bare_ident(const char *s) {
    if (!s || !(isalpha((unsigned char)s[0]) || s[0] == '_')) {
        return 0;
    }
    for (const char *p = s + 1; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

static void cl_write_expr(cl_buf_t *buf, const cl_expr_t *expr, int indent);

/* Defensively parenthesizes any sub-expression whose own precedence could
 * otherwise be misread once reprinted next to an operator or "?:". */
static void cl_write_operand(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    int needs_paren = expr->kind == CL_EXPR_BINARY || expr->kind == CL_EXPR_CONDITIONAL;
    if (needs_paren) {
        cl_buf_append_char(buf, '(');
    }
    cl_write_expr(buf, expr, indent);
    if (needs_paren) {
        cl_buf_append_char(buf, ')');
    }
}

/* A computed object key: a template is written back as its quoted string,
 * anything else inside the parentheses it came from. */
static void cl_write_computed_key(cl_buf_t *buf, const cl_expr_t *key_expr, int indent) {
    if (key_expr->kind == CL_EXPR_TEMPLATE) {
        cl_write_expr(buf, key_expr, indent);
        return;
    }
    cl_buf_append_char(buf, '(');
    cl_write_expr(buf, key_expr, indent);
    cl_buf_append_char(buf, ')');
}

static void cl_write_object(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    cl_buf_append(buf, "{\n");
    for (size_t i = 0; i < expr->as.object.count; i++) {
        const cl_object_item_t *item = &expr->as.object.items[i];
        cl_buf_append_indent(buf, indent + 1);
        if (item->key_expr) {
            cl_write_computed_key(buf, item->key_expr, indent + 1);
        } else if (cl_is_bare_ident(item->key)) {
            cl_buf_append(buf, item->key);
        } else {
            cl_write_string_literal(buf, item->key);
        }
        cl_buf_append(buf, " = ");
        cl_write_expr(buf, item->value, indent + 1);
        cl_buf_append_char(buf, '\n');
    }
    cl_buf_append_indent(buf, indent);
    cl_buf_append_char(buf, '}');
}

static void cl_write_steps(cl_buf_t *buf, const cl_traversal_step_t *steps, size_t count, int indent) {
    for (size_t i = 0; i < count; i++) {
        const cl_traversal_step_t *step = &steps[i];
        switch (step->kind) {
            case CL_STEP_ATTR:
                cl_buf_append_char(buf, '.');
                cl_buf_append(buf, step->name);
                break;
            case CL_STEP_INDEX_NUMBER:
                cl_buf_append_char(buf, '[');
                cl_write_number(buf, step->index);
                cl_buf_append_char(buf, ']');
                break;
            case CL_STEP_INDEX_STRING:
                cl_buf_append_char(buf, '[');
                cl_write_string_literal(buf, step->name);
                cl_buf_append_char(buf, ']');
                break;
            case CL_STEP_SPLAT_ATTR:
                cl_buf_append(buf, ".*");
                break;
            case CL_STEP_SPLAT_FULL:
                cl_buf_append(buf, "[*]");
                break;
            case CL_STEP_INDEX_EXPR:
                cl_buf_append_char(buf, '[');
                cl_write_expr(buf, step->expr, indent);
                cl_buf_append_char(buf, ']');
                break;
        }
    }
}

static void cl_write_traversal(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    cl_buf_append(buf, expr->as.traversal.root);
    cl_write_steps(buf, expr->as.traversal.steps, expr->as.traversal.count, indent);
}

/* Unlike cl_write_operand() (used for binary/conditional operands, where
 * "-x + y" is unambiguous without parens), a postfix base needs parens
 * around binary/conditional/unary sub-expressions too: postfix binds
 * tighter than unary in this grammar, so an unparenthesized "!flag[0]"
 * would reparse as "!(flag[0])" instead of "(!flag)[0]". */
static void cl_write_postfix(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    const cl_expr_t *base = expr->as.postfix.base;
    int needs_paren = base->kind == CL_EXPR_BINARY || base->kind == CL_EXPR_CONDITIONAL ||
                       base->kind == CL_EXPR_UNARY;
    if (needs_paren) {
        cl_buf_append_char(buf, '(');
    }
    cl_write_expr(buf, base, indent);
    if (needs_paren) {
        cl_buf_append_char(buf, ')');
    }
    cl_write_steps(buf, expr->as.postfix.steps, expr->as.postfix.count, indent);
}

static void cl_write_tuple(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    cl_buf_append_char(buf, '[');
    for (size_t i = 0; i < expr->as.tuple.count; i++) {
        if (i > 0) {
            cl_buf_append(buf, ", ");
        }
        cl_write_expr(buf, expr->as.tuple.items[i], indent);
    }
    cl_buf_append_char(buf, ']');
}

static void cl_write_call(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    cl_buf_append(buf, expr->as.call.name);
    cl_buf_append_char(buf, '(');
    for (size_t i = 0; i < expr->as.call.count; i++) {
        if (i > 0) {
            cl_buf_append(buf, ", ");
        }
        cl_write_expr(buf, expr->as.call.args[i], indent);
        if (expr->as.call.expand_final && i + 1 == expr->as.call.count) {
            cl_buf_append(buf, "...");
        }
    }
    cl_buf_append_char(buf, ')');
}

static const char *cl_unary_op_str(cl_unary_op_t op) {
    switch (op) {
        case CL_OP_NEG: return "-";
        case CL_OP_NOT: return "!";
    }
    return "?";
}

static const char *cl_binary_op_str(cl_binary_op_t op) {
    switch (op) {
        case CL_OP_ADD: return "+";
        case CL_OP_SUB: return "-";
        case CL_OP_MUL: return "*";
        case CL_OP_DIV: return "/";
        case CL_OP_MOD: return "%";
        case CL_OP_EQ: return "==";
        case CL_OP_NEQ: return "!=";
        case CL_OP_LT: return "<";
        case CL_OP_LE: return "<=";
        case CL_OP_GT: return ">";
        case CL_OP_GE: return ">=";
        case CL_OP_AND: return "&&";
        case CL_OP_OR: return "||";
    }
    return "?";
}

static void cl_write_for(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    cl_buf_append_char(buf, expr->as.for_expr.is_object ? '{' : '[');
    cl_buf_append(buf, "for ");
    if (expr->as.for_expr.key_var) {
        cl_buf_append(buf, expr->as.for_expr.key_var);
        cl_buf_append(buf, ", ");
    }
    cl_buf_append(buf, expr->as.for_expr.val_var);
    cl_buf_append(buf, " in ");
    cl_write_expr(buf, expr->as.for_expr.collection, indent);
    cl_buf_append(buf, " : ");
    if (expr->as.for_expr.is_object) {
        cl_write_expr(buf, expr->as.for_expr.key_expr, indent);
        cl_buf_append(buf, " => ");
        cl_write_expr(buf, expr->as.for_expr.value_expr, indent);
        if (expr->as.for_expr.grouping) {
            cl_buf_append(buf, "...");
        }
    } else {
        cl_write_expr(buf, expr->as.for_expr.value_expr, indent);
    }
    if (expr->as.for_expr.cond) {
        cl_buf_append(buf, " if ");
        cl_write_expr(buf, expr->as.for_expr.cond, indent);
    }
    cl_buf_append_char(buf, expr->as.for_expr.is_object ? '}' : ']');
}

/* Escapes the same characters as a plain string literal, plus literal
 * "${"/"%{" sequences (as "$${"/"%%{") so a template part that happens to
 * contain them round-trips instead of being reinterpreted on reload. */
static void cl_write_template(cl_buf_t *buf, const cl_template_t *tpl, int indent) {
    for (size_t i = 0; i < tpl->count; i++) {
        const cl_template_part_t *part = &tpl->parts[i];
        switch (part->kind) {
            case CL_TPL_LITERAL:
                cl_write_escaped_text(buf, part->text);
                break;
            case CL_TPL_INTERP:
                cl_buf_append(buf, "${");
                cl_write_expr(buf, part->expr, indent);
                cl_buf_append_char(buf, '}');
                break;
            case CL_TPL_IF:
                cl_buf_append(buf, "%{if ");
                cl_write_expr(buf, part->if_cond, indent);
                cl_buf_append_char(buf, '}');
                cl_write_template(buf, part->if_then, indent);
                if (part->if_else) {
                    cl_buf_append(buf, "%{else}");
                    cl_write_template(buf, part->if_else, indent);
                }
                cl_buf_append(buf, "%{endif}");
                break;
            case CL_TPL_FOR:
                cl_buf_append(buf, "%{for ");
                if (part->for_key_var) {
                    cl_buf_append(buf, part->for_key_var);
                    cl_buf_append(buf, ", ");
                }
                cl_buf_append(buf, part->for_val_var);
                cl_buf_append(buf, " in ");
                cl_write_expr(buf, part->for_collection, indent);
                cl_buf_append_char(buf, '}');
                cl_write_template(buf, part->for_body, indent);
                cl_buf_append(buf, "%{endfor}");
                break;
        }
    }
}

static void cl_write_expr(cl_buf_t *buf, const cl_expr_t *expr, int indent) {
    switch (expr->kind) {
        case CL_EXPR_STRING: cl_write_string_literal(buf, expr->as.string_value); break;
        case CL_EXPR_NUMBER: cl_write_number(buf, expr->as.number_value); break;
        case CL_EXPR_BOOL: cl_buf_append(buf, expr->as.bool_value ? "true" : "false"); break;
        case CL_EXPR_NULL: cl_buf_append(buf, "null"); break;
        case CL_EXPR_OBJECT: cl_write_object(buf, expr, indent); break;
        case CL_EXPR_TRAVERSAL: cl_write_traversal(buf, expr, indent); break;
        case CL_EXPR_POSTFIX: cl_write_postfix(buf, expr, indent); break;
        case CL_EXPR_TUPLE: cl_write_tuple(buf, expr, indent); break;
        case CL_EXPR_TEMPLATE:
            cl_buf_append_char(buf, '"');
            cl_write_template(buf, expr->as.tpl, indent);
            cl_buf_append_char(buf, '"');
            break;
        case CL_EXPR_CALL: cl_write_call(buf, expr, indent); break;
        case CL_EXPR_UNARY:
            cl_buf_append(buf, cl_unary_op_str(expr->as.unary.op));
            cl_write_operand(buf, expr->as.unary.operand, indent);
            break;
        case CL_EXPR_BINARY:
            cl_write_operand(buf, expr->as.binary.left, indent);
            cl_buf_append_char(buf, ' ');
            cl_buf_append(buf, cl_binary_op_str(expr->as.binary.op));
            cl_buf_append_char(buf, ' ');
            cl_write_operand(buf, expr->as.binary.right, indent);
            break;
        case CL_EXPR_CONDITIONAL:
            cl_write_operand(buf, expr->as.conditional.cond, indent);
            cl_buf_append(buf, " ? ");
            cl_write_operand(buf, expr->as.conditional.then_expr, indent);
            cl_buf_append(buf, " : ");
            cl_write_operand(buf, expr->as.conditional.else_expr, indent);
            break;
        case CL_EXPR_FOR: cl_write_for(buf, expr, indent); break;
    }
}

static void cl_write_body(cl_buf_t *buf, const cl_body_t *body, int indent) {
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        cl_buf_append_indent(buf, indent);
        if (item->kind == CL_ITEM_ATTRIBUTE) {
            const cl_attribute_t *attr = item->as.attribute;
            cl_buf_append(buf, attr->name);
            cl_buf_append(buf, " = ");
            cl_write_expr(buf, attr->value, indent);
            cl_buf_append_char(buf, '\n');
        } else {
            const cl_block_t *block = item->as.block;
            cl_buf_append(buf, block->type);
            for (size_t l = 0; l < block->label_count; l++) {
                cl_buf_append_char(buf, ' ');
                if (block->label_exprs && block->label_exprs[l]) {
                    cl_write_expr(buf, block->label_exprs[l], indent); /* a template: quoted */
                } else {
                    cl_write_string_literal(buf, block->labels[l]);
                }
            }
            cl_buf_append(buf, " {\n");
            cl_write_body(buf, block->body, indent + 1);
            cl_buf_append_indent(buf, indent);
            cl_buf_append(buf, "}\n");
        }
    }
}

char *cl_document_to_string(const cl_document_t *doc) {
    cl_buf_t buf = {0};
    cl_write_body(&buf, doc->root, 0);
    return cl_buf_finish(&buf);
}

int cl_save_file(const cl_document_t *doc, const char *path, cl_error_t *err) {
    char *text = cl_document_to_string(doc);
    FILE *f = fopen(path, "wb");
    if (!f) {
        if (err) {
            snprintf(err->message, sizeof(err->message), "nao foi possivel abrir '%s' para escrita", path);
            err->line = 0;
            err->col = 0;
        }
        free(text);
        return -1;
    }
    size_t len = strlen(text);
    size_t written = fwrite(text, 1, len, f);
    fclose(f);
    free(text);
    if (written != len) {
        if (err) {
            snprintf(err->message, sizeof(err->message), "falha ao escrever em '%s'", path);
            err->line = 0;
            err->col = 0;
        }
        return -1;
    }
    return 0;
}
