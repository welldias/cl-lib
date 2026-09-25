#include "cl/cl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_indent(int indent) {
    for (int i = 0; i < indent; i++) {
        printf("  ");
    }
}

/* ------------------------------------------------------------------ */
/* Phase 1 dump: raw literal AST, exactly what cl_load_* produced        */
/* ------------------------------------------------------------------ */

static void dump_expr(const cl_expr_t *expr, int indent);

static void dump_object(const cl_expr_t *expr, int indent) {
    printf("{\n");
    size_t count = cl_expr_object_count(expr);
    for (size_t i = 0; i < count; i++) {
        print_indent(indent + 1);
        printf("%s = ", cl_expr_object_key_at(expr, i));
        dump_expr(cl_expr_object_value_at(expr, i), indent + 1);
    }
    print_indent(indent);
    printf("}\n");
}

static void dump_tuple(const cl_expr_t *expr, int indent) {
    printf("[");
    size_t count = cl_expr_tuple_count(expr);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            printf(", ");
        }
        dump_expr(cl_expr_tuple_at(expr, i), indent);
    }
    printf("]\n");
}

static void dump_traversal(const cl_expr_t *expr) {
    printf("%s", cl_expr_traversal_root(expr));
    size_t count = cl_expr_traversal_step_count(expr);
    for (size_t i = 0; i < count; i++) {
        const cl_traversal_step_t *step = cl_expr_traversal_step_at(expr, i);
        switch (step->kind) {
            case CL_STEP_ATTR: printf(".%s", step->name); break;
            case CL_STEP_INDEX_NUMBER: printf("[%g]", step->index); break;
            case CL_STEP_INDEX_STRING: printf("[\"%s\"]", step->name); break;
            case CL_STEP_SPLAT_ATTR: printf(".*"); break;
            case CL_STEP_SPLAT_FULL: printf("[*]"); break;
            case CL_STEP_INDEX_EXPR: printf("[<expr>]"); break;
        }
    }
    printf("\n");
}

static void dump_postfix(const cl_expr_t *expr, int indent) {
    printf("<postfix\n");
    print_indent(indent + 1);
    printf("base: ");
    dump_expr(cl_expr_postfix_base(expr), indent + 1);
    print_indent(indent + 1);
    printf("steps: ");
    size_t count = cl_expr_postfix_step_count(expr);
    for (size_t i = 0; i < count; i++) {
        const cl_traversal_step_t *step = cl_expr_postfix_step_at(expr, i);
        switch (step->kind) {
            case CL_STEP_ATTR: printf(".%s", step->name); break;
            case CL_STEP_INDEX_NUMBER: printf("[%g]", step->index); break;
            case CL_STEP_INDEX_STRING: printf("[\"%s\"]", step->name); break;
            case CL_STEP_SPLAT_ATTR: printf(".*"); break;
            case CL_STEP_SPLAT_FULL: printf("[*]"); break;
            case CL_STEP_INDEX_EXPR: printf("[<expr>]"); break;
        }
    }
    printf("\n");
    print_indent(indent);
    printf(">\n");
}

static void dump_call(const cl_expr_t *expr) {
    printf("%s(", cl_expr_call_name(expr));
    size_t count = cl_expr_call_arg_count(expr);
    for (size_t i = 0; i < count; i++) {
        if (i > 0) {
            printf(", ");
        }
        dump_expr(cl_expr_call_arg_at(expr, i), 0);
    }
    printf(")%s\n", cl_expr_call_expand_final(expr) ? " [expand]" : "");
}

static void dump_template(const cl_template_t *tpl, int indent) {
    printf("<template:\n");
    size_t count = cl_template_part_count(tpl);
    for (size_t i = 0; i < count; i++) {
        const cl_template_part_t *part = cl_template_part_at(tpl, i);
        print_indent(indent + 1);
        switch (part->kind) {
            case CL_TPL_LITERAL: printf("literal %s\n", part->text); break;
            case CL_TPL_INTERP:
                printf("interp ");
                dump_expr(part->expr, indent + 1);
                break;
            case CL_TPL_IF:
                printf("if ");
                dump_expr(part->if_cond, indent + 1);
                dump_template(part->if_then, indent + 1);
                if (part->if_else) {
                    print_indent(indent + 1);
                    printf("else\n");
                    dump_template(part->if_else, indent + 1);
                }
                break;
            case CL_TPL_FOR:
                printf("for %s%s in ", part->for_key_var ? part->for_key_var : "",
                       part->for_key_var ? ", " : "");
                printf("%s ", part->for_val_var);
                dump_expr(part->for_collection, indent + 1);
                dump_template(part->for_body, indent + 1);
                break;
        }
    }
    print_indent(indent);
    printf(">\n");
}

static void dump_expr(const cl_expr_t *expr, int indent) {
    double num;
    int flag;
    cl_unary_op_t uop;
    cl_binary_op_t bop;
    switch (cl_expr_kind(expr)) {
        case CL_EXPR_STRING: printf("\"%s\"\n", cl_expr_as_string(expr)); break;
        case CL_EXPR_NUMBER: cl_expr_as_number(expr, &num); printf("%g\n", num); break;
        case CL_EXPR_BOOL: cl_expr_as_bool(expr, &flag); printf("%s\n", flag ? "true" : "false"); break;
        case CL_EXPR_NULL: printf("null\n"); break;
        case CL_EXPR_OBJECT: dump_object(expr, indent); break;
        case CL_EXPR_TRAVERSAL: dump_traversal(expr); break;
        case CL_EXPR_POSTFIX: dump_postfix(expr, indent); break;
        case CL_EXPR_TUPLE: dump_tuple(expr, indent); break;
        case CL_EXPR_TEMPLATE: dump_template(cl_expr_template(expr), indent); break;
        case CL_EXPR_CALL: dump_call(expr); break;
        case CL_EXPR_UNARY:
            cl_expr_unary_op(expr, &uop);
            printf("%s", uop == CL_OP_NEG ? "-" : "!");
            dump_expr(cl_expr_unary_operand(expr), indent);
            break;
        case CL_EXPR_BINARY:
            cl_expr_binary_op(expr, &bop);
            printf("(binary op=%d)\n", (int)bop);
            break;
        case CL_EXPR_CONDITIONAL: printf("<conditional>\n"); break;
        case CL_EXPR_FOR: printf("<for-expression>\n"); break;
    }
}

static void dump_body(const cl_body_t *body, int indent) {
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        print_indent(indent);
        if (item->kind == CL_ITEM_ATTRIBUTE) {
            printf("%s = ", item->as.attribute->name);
            dump_expr(item->as.attribute->value, indent);
        } else {
            const cl_block_t *block = item->as.block;
            printf("%s", block->type);
            for (size_t l = 0; l < block->label_count; l++) {
                printf(" \"%s\"", block->labels[l]);
            }
            printf(" {\n");
            dump_body(block->body, indent + 1);
            print_indent(indent);
            printf("}\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Phase 2 dump: resolved values, only after cl_document_evaluate()      */
/* ------------------------------------------------------------------ */

static void dump_value(const cl_value_t *value, int indent);

static void dump_value_list(const cl_value_t *value, int indent) {
    printf("[\n");
    size_t count = cl_value_list_count(value);
    for (size_t i = 0; i < count; i++) {
        print_indent(indent + 1);
        dump_value(cl_value_list_at(value, i), indent + 1);
    }
    print_indent(indent);
    printf("]\n");
}

static void dump_value_object(const cl_value_t *value, int indent) {
    printf("{\n");
    size_t count = cl_value_object_count(value);
    for (size_t i = 0; i < count; i++) {
        print_indent(indent + 1);
        printf("%s = ", cl_value_object_key_at(value, i));
        dump_value(cl_value_object_value_at(value, i), indent + 1);
    }
    print_indent(indent);
    printf("}\n");
}

static void dump_value(const cl_value_t *value, int indent) {
    double num;
    int flag;
    switch (cl_value_kind(value)) {
        case CL_VAL_STRING: printf("\"%s\"\n", cl_value_as_string(value)); break;
        case CL_VAL_NUMBER: cl_value_as_number(value, &num); printf("%g\n", num); break;
        case CL_VAL_BOOL: cl_value_as_bool(value, &flag); printf("%s\n", flag ? "true" : "false"); break;
        case CL_VAL_NULL: printf("null\n"); break;
        case CL_VAL_LIST: dump_value_list(value, indent); break;
        case CL_VAL_OBJECT: dump_value_object(value, indent); break;
    }
}

static void dump_evaluated_body(const cl_evaluated_body_t *body, int indent) {
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        print_indent(indent);
        if (item->kind == CL_ITEM_ATTRIBUTE) {
            printf("%s = ", item->as.attribute->name);
            dump_value(item->as.attribute->value, indent);
        } else {
            const cl_evaluated_block_t *block = item->as.block;
            printf("%s", block->type);
            for (size_t l = 0; l < block->label_count; l++) {
                printf(" \"%s\"", block->labels[l]);
            }
            printf(" {\n");
            dump_evaluated_body(block->body, indent + 1);
            print_indent(indent);
            printf("}\n");
        }
    }
}

/* ------------------------------------------------------------------ */

static void try_load_and_dump(const char *path) {
    cl_error_t err;
    printf("== load: %s ==\n", path);
    cl_document_t *doc = cl_load_file(path, &err);
    if (!doc) {
        printf("  erro de parse (linha %d, coluna %d): %s\n", err.line, err.col, err.message);
        return;
    }
    dump_body(cl_document_root(doc), 1);
    cl_document_free(doc);
}

/* Builds bindings from "name=value" command-line arguments: "true"/"false"
 * become bools, anything strtod() consumes entirely becomes a number, and
 * everything else stays a string. Returns NULL on a malformed argument. */
static cl_bindings_t *parse_bindings(int count, char **args) {
    cl_bindings_t *bindings = cl_bindings_new();
    for (int i = 0; i < count; i++) {
        char *eq = strchr(args[i], '=');
        if (!eq || eq == args[i]) {
            fprintf(stderr, "binding invalido '%s' (esperado nome=valor)\n", args[i]);
            cl_bindings_free(bindings);
            return NULL;
        }
        *eq = '\0';
        const char *name = args[i];
        const char *value = eq + 1;
        char *end = NULL;
        double number = strtod(value, &end);
        if (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) {
            cl_bindings_set_bool(bindings, name, strcmp(value, "true") == 0);
        } else if (*value != '\0' && *end == '\0') {
            cl_bindings_set_number(bindings, name, number);
        } else {
            cl_bindings_set_string(bindings, name, value);
        }
    }
    return bindings;
}

static void print_schema_error(const cl_error_t *err) {
    printf("  erro de schema (linha %d, coluna %d): %s\n", err->line, err->col, err->message);
}

static void try_evaluate(const char *path, const cl_bindings_t *bindings, const cl_schema_t *schema) {
    cl_error_t err;
    printf("== evaluate: %s ==\n", path);
    cl_document_t *doc = cl_load_file(path, &err);
    if (!doc) {
        printf("  erro de parse (linha %d, coluna %d): %s\n", err.line, err.col, err.message);
        return;
    }
    /* structure (and literal types) first, then evaluated types */
    if (schema && cl_schema_validate(schema, doc, NULL, &err) != 0) {
        print_schema_error(&err);
        cl_document_free(doc);
        return;
    }
    cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, &err);
    if (!result) {
        printf("  erro de avaliacao (linha %d, coluna %d): %s\n", err.line, err.col, err.message);
        cl_document_free(doc);
        return;
    }
    if (schema && cl_schema_validate(schema, doc, result, &err) != 0) {
        print_schema_error(&err);
    } else {
        dump_evaluated_body(cl_evaluated_root(result), 1);
    }
    cl_evaluated_free(result);
    cl_document_free(doc);
}

int main(int argc, char **argv) {
    printf("cllib version %s\n\n", cl_version());

    if (argc < 2) {
        fprintf(stderr, "uso: %s <arquivo.cl> [--schema=<schema.cl>] [nome=valor ...]\n", argv[0]);
        return 1;
    }

    const char *cl_file = argv[1];
    const char *schema_file = NULL;
    char **binding_args = argv + 2;
    int binding_count = 0;
    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--schema=", 9) == 0) {
            schema_file = argv[i] + 9;
        } else {
            binding_args[binding_count++] = argv[i];
        }
    }

    cl_schema_t *schema = NULL;
    if (schema_file) {
        cl_error_t err;
        schema = cl_schema_load_file(schema_file, &err);
        if (!schema) {
            fprintf(stderr, "schema invalido %s (linha %d, coluna %d): %s\n", schema_file, err.line, err.col,
                    err.message);
            return 1;
        }
    }

    cl_bindings_t *bindings = parse_bindings(binding_count, binding_args);
    if (!bindings) {
        cl_schema_free(schema);
        return 1;
    }

    try_load_and_dump(cl_file);
    try_evaluate(cl_file, bindings, schema);

    cl_bindings_free(bindings);
    cl_schema_free(schema);
    return 0;
}
