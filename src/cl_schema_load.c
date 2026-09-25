/* Builds a cl_schema_t from a .cl schema file. The file is an ordinary cl
 * document read only structurally (never evaluated), then turned into
 * rules exclusively through the public schema API in cl_schema.c. */

#include "cl_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cl_schema_load_fail(cl_error_t *err, int line, int col, const char *fmt, ...) {
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

static int cl_schema_parse_type(const char *name, cl_schema_type_t *out) {
    static const struct {
        const char *name;
        cl_schema_type_t type;
    } types[] = {
        {"any", CL_TYPE_ANY},       {"string", CL_TYPE_STRING}, {"number", CL_TYPE_NUMBER},
        {"bool", CL_TYPE_BOOL},     {"list", CL_TYPE_LIST},     {"object", CL_TYPE_OBJECT},
    };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        if (strcmp(types[i].name, name) == 0) {
            *out = types[i].type;
            return 0;
        }
    }
    return -1;
}

static int cl_schema_load_type(const cl_expr_t *expr, cl_schema_type_t *out, cl_error_t *err) {
    const char *name = cl_expr_as_string(expr);
    if (!name) {
        return cl_schema_load_fail(err, expr->line, expr->col, "tipo deve ser uma string literal");
    }
    if (cl_schema_parse_type(name, out) != 0) {
        return cl_schema_load_fail(err, expr->line, expr->col,
                                   "tipo '%s' invalido (use any, string, number, bool, list ou object)", name);
    }
    return 0;
}

/* Reads an enum's allowed values: a non-empty list of distinct string
 * literals. On success *out_values is a malloc'd array (caller frees it)
 * pointing into the schema document's own strings. */
static int cl_schema_load_enum(const cl_expr_t *list, const char *attr_name, const char ***out_values,
                               size_t *out_count, cl_error_t *err) {
    if (cl_expr_kind(list) != CL_EXPR_TUPLE) {
        return cl_schema_load_fail(err, list->line, list->col, "'values' deve ser uma lista de strings");
    }
    size_t count = cl_expr_tuple_count(list);
    if (count == 0) {
        return cl_schema_load_fail(err, list->line, list->col, "lista de valores do atributo '%s' nao pode ser vazia",
                                   attr_name);
    }
    const char **values = malloc(count * sizeof(const char *));
    if (!values) {
        abort();
    }
    for (size_t i = 0; i < count; i++) {
        const cl_expr_t *item = cl_expr_tuple_at(list, i);
        values[i] = cl_expr_as_string(item);
        if (!values[i]) {
            free(values);
            return cl_schema_load_fail(err, item->line, item->col,
                                       "valores do atributo '%s' devem ser strings literais", attr_name);
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(values[i], values[j]) == 0) {
                cl_schema_load_fail(err, item->line, item->col, "valor '%s' repetido no atributo '%s'", values[i],
                                    attr_name);
                free(values);
                return -1;
            }
        }
    }
    *out_values = values;
    *out_count = count;
    return 0;
}

/* One attribute rule, in one of three forms:
 *   "list"                                   - just the type
 *   ["ssd", "hdd"]                           - an enum of strings
 *   { type = "...", required = ..., values = [...] }, all keys optional */
static int cl_schema_load_attr(cl_schema_block_t *block, const cl_attribute_t *attr, cl_error_t *err) {
    const cl_expr_t *value = attr->value;
    cl_schema_type_t type = CL_TYPE_ANY;
    const cl_expr_t *type_expr = NULL;
    const cl_expr_t *values_expr = NULL;
    int required = 0;

    if (cl_expr_kind(value) == CL_EXPR_STRING) {
        if (cl_schema_load_type(value, &type, err) != 0) {
            return -1;
        }
    } else if (cl_expr_kind(value) == CL_EXPR_TUPLE) {
        values_expr = value;
    } else if (cl_expr_kind(value) == CL_EXPR_OBJECT) {
        for (size_t i = 0; i < cl_expr_object_count(value); i++) {
            const char *key = cl_expr_object_key_at(value, i);
            const cl_expr_t *item = cl_expr_object_value_at(value, i);
            if (!key) {
                const cl_expr_t *key_expr = cl_expr_object_key_expr_at(value, i);
                return cl_schema_load_fail(err, key_expr->line, key_expr->col,
                                           "chave calculada nao e permitida no schema");
            }
            if (strcmp(key, "type") == 0) {
                if (cl_schema_load_type(item, &type, err) != 0) {
                    return -1;
                }
                type_expr = item;
            } else if (strcmp(key, "required") == 0) {
                if (cl_expr_as_bool(item, &required) != 0) {
                    return cl_schema_load_fail(err, item->line, item->col, "'required' deve ser true ou false");
                }
            } else if (strcmp(key, "values") == 0) {
                values_expr = item;
            } else {
                return cl_schema_load_fail(err, item->line, item->col,
                                           "chave '%s' desconhecida na regra do atributo '%s'", key, attr->name);
            }
        }
    } else {
        return cl_schema_load_fail(err, attr->line, attr->col,
                                   "regra do atributo '%s' deve ser um nome de tipo, uma lista de valores ou um "
                                   "objeto { type, required, values }",
                                   attr->name);
    }

    int rc;
    if (values_expr) {
        if (type_expr && type != CL_TYPE_STRING) {
            return cl_schema_load_fail(err, type_expr->line, type_expr->col,
                                       "'values' so pode ser usado com type = \"string\"");
        }
        const char **values = NULL;
        size_t count = 0;
        if (cl_schema_load_enum(values_expr, attr->name, &values, &count, err) != 0) {
            return -1;
        }
        rc = cl_schema_block_add_enum(block, attr->name, values, count, required);
        free(values);
    } else {
        rc = cl_schema_block_add_attr(block, attr->name, type, required);
    }
    if (rc != 0) {
        return cl_schema_load_fail(err, attr->line, attr->col, "atributo '%s' duplicado no schema", attr->name);
    }
    return 0;
}

static int cl_schema_load_body(cl_schema_block_t *block, const cl_body_t *body, cl_error_t *err);

/* A `block "<type>" { ... }` declaration, at the top level (parent == NULL,
 * registered on `schema`) or nested inside another rule. */
static int cl_schema_load_block(cl_schema_t *schema, cl_schema_block_t *parent, const cl_block_t *decl,
                                cl_error_t *err) {
    if (strcmp(decl->type, "block") != 0) {
        return cl_schema_load_fail(err, decl->line, decl->col,
                                   "esperado bloco 'block' no schema, encontrado '%s'", decl->type);
    }
    if (decl->label_count != 1) {
        return cl_schema_load_fail(err, decl->line, decl->col,
                                   "bloco 'block' precisa de exatamente 1 rotulo (o tipo do bloco)");
    }
    const char *type = decl->labels[0];
    if (!type) {
        return cl_schema_load_fail(err, decl->line, decl->col, "rotulo calculado nao e permitido no schema");
    }
    cl_schema_block_t *rule = parent ? cl_schema_block_add_block(parent, type) : cl_schema_add_block(schema, type);
    if (!rule) {
        return cl_schema_load_fail(err, decl->line, decl->col, "tipo de bloco '%s' duplicado no schema", type);
    }
    return cl_schema_load_body(rule, decl->body, err);
}

static int cl_schema_load_body(cl_schema_block_t *block, const cl_body_t *body, cl_error_t *err) {
    for (size_t i = 0; i < body->count; i++) {
        const cl_body_item_t *item = &body->items[i];
        int rc = item->kind == CL_ITEM_ATTRIBUTE ? cl_schema_load_attr(block, item->as.attribute, err)
                                                 : cl_schema_load_block(NULL, block, item->as.block, err);
        if (rc != 0) {
            return -1;
        }
    }
    return 0;
}

static cl_schema_t *cl_schema_from_document(const cl_document_t *doc, cl_error_t *err) {
    cl_schema_t *schema = cl_schema_new();
    const cl_body_t *root = doc->root;

    for (size_t i = 0; i < root->count; i++) {
        const cl_body_item_t *item = &root->items[i];
        int rc = 0;
        if (item->kind == CL_ITEM_BLOCK) {
            rc = cl_schema_load_block(schema, NULL, item->as.block, err);
        } else {
            const cl_attribute_t *attr = item->as.attribute;
            int strict = 0;
            if (strcmp(attr->name, "strict") != 0) {
                rc = cl_schema_load_fail(err, attr->line, attr->col, "item de topo '%s' desconhecido no schema",
                                         attr->name);
            } else if (cl_expr_as_bool(attr->value, &strict) != 0) {
                rc = cl_schema_load_fail(err, attr->line, attr->col, "'strict' deve ser true ou false");
            } else {
                cl_schema_set_strict(schema, strict);
            }
        }
        if (rc != 0) {
            cl_schema_free(schema);
            return NULL;
        }
    }
    return schema;
}

cl_schema_t *cl_schema_load_string(const char *source, const char *source_name, cl_error_t *err) {
    cl_document_t *doc = cl_load_string(source, source_name, err);
    if (!doc) {
        return NULL;
    }
    cl_schema_t *schema = cl_schema_from_document(doc, err);
    cl_document_free(doc);
    return schema;
}

cl_schema_t *cl_schema_load_file(const char *path, cl_error_t *err) {
    cl_document_t *doc = cl_load_file(path, err);
    if (!doc) {
        return NULL;
    }
    cl_schema_t *schema = cl_schema_from_document(doc, err);
    cl_document_free(doc);
    return schema;
}
