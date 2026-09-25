#include "cl/cl.h"
#include "test_util.h"

#include <math.h>
#include <stdlib.h>

/* Every quoted string is a template, wherever it appears: a value, an
 * index, an object key or a block label. "${...}" always interpolates and
 * "$${...}" is always the literal text "${...}". */

static cl_document_t *load(const char *source) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "strings", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        fprintf(stderr, "  source: %s  parse error: %s\n", source, err.message);
    }
    return doc;
}

static cl_evaluated_t *eval_source(const char *source, const cl_bindings_t *bindings) {
    cl_document_t *doc = load(source);
    if (!doc) {
        return NULL;
    }
    cl_error_t err;
    cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, &err);
    CL_CHECK(result != NULL);
    if (!result) {
        fprintf(stderr, "  source: %s  eval error: %s\n", source, err.message);
    }
    cl_document_free(doc);
    return result;
}

/* Checks that `source` fails to evaluate with `fragment` in the message,
 * at line:col. */
static void check_eval_error(const char *source, const char *fragment, int line, int col) {
    cl_document_t *doc = load(source);
    if (!doc) {
        return;
    }
    cl_error_t err;
    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result == NULL);
    if (result) {
        cl_evaluated_free(result);
    } else {
        CL_CHECK(strstr(err.message, fragment) != NULL);
        CL_CHECK(err.line == line && err.col == col);
        if (!strstr(err.message, fragment) || err.line != line || err.col != col) {
            fprintf(stderr, "  source: %s  got %d:%d '%s'\n", source, err.line, err.col, err.message);
        }
    }
    cl_document_free(doc);
}

/* ---- AST ------------------------------------------------------------ */

static void test_object_key_kinds(void) {
    cl_document_t *doc = load("o = { a = 1, \"b c\" = 2, \"$${x}\" = 3, \"k${n}\" = 4, (n) = 5, (\"d\") = 6 }\n");
    if (!doc) {
        return;
    }
    const cl_expr_t *o = cl_body_get_attribute(cl_document_root(doc), "o")->value;
    CL_CHECK(cl_expr_object_count(o) == 6);
    CL_CHECK_STREQ(cl_expr_object_key_at(o, 0), "a");
    CL_CHECK_STREQ(cl_expr_object_key_at(o, 1), "b c");
    CL_CHECK_STREQ(cl_expr_object_key_at(o, 2), "${x}"); /* decoded */
    CL_CHECK(cl_expr_object_key_at(o, 3) == NULL);        /* computed: template */
    CL_CHECK(cl_expr_kind(cl_expr_object_key_expr_at(o, 3)) == CL_EXPR_TEMPLATE);
    CL_CHECK(cl_expr_object_key_at(o, 4) == NULL); /* computed: (expr) */
    CL_CHECK(cl_expr_kind(cl_expr_object_key_expr_at(o, 4)) == CL_EXPR_TRAVERSAL);
    CL_CHECK_STREQ(cl_expr_object_key_at(o, 5), "d"); /* ("d") has nothing to compute */
    for (size_t i = 0; i < 6; i++) {
        if (i != 3 && i != 4) {
            CL_CHECK(cl_expr_object_key_expr_at(o, i) == NULL);
        }
    }
    CL_CHECK(cl_expr_object_get(o, "${x}") != NULL);
    CL_CHECK(cl_expr_object_get(o, "k${n}") == NULL); /* computed keys never match before evaluation */
    cl_document_free(doc);
}

static void test_label_kinds(void) {
    cl_document_t *doc = load("a \"x\" \"$${y}\" {}\nb \"x\" \"web-${env}\" {}\n");
    if (!doc) {
        return;
    }
    const char *const lit[] = {"x", "${y}"};
    const cl_block_t *a = cl_body_find_block(cl_document_root(doc), "a", lit, 2);
    CL_CHECK(a != NULL);
    if (a) {
        CL_CHECK(a->label_exprs == NULL); /* every label constant */
    }
    cl_block_t **bs = NULL;
    CL_CHECK(cl_body_find_blocks(cl_document_root(doc), "b", &bs) == 1);
    if (bs) {
        const cl_block_t *b = bs[0];
        CL_CHECK_STREQ(b->labels[0], "x");
        CL_CHECK(b->labels[1] == NULL);
        CL_CHECK(b->label_exprs != NULL && b->label_exprs[0] == NULL);
        CL_CHECK(b->label_exprs != NULL && cl_expr_kind(b->label_exprs[1]) == CL_EXPR_TEMPLATE);
        free(bs);
    }
    const char *const raw[] = {"x", "web-${env}"};
    CL_CHECK(cl_body_find_block(cl_document_root(doc), "b", raw, 2) == NULL);
    cl_document_free(doc);
}

/* ---- evaluation ------------------------------------------------------ */

static void test_computed_keys(void) {
    cl_evaluated_t *result = eval_source(
        "env = \"prod\"\n"
        "n = 3\n"
        "hosts = { \"db_${env}\" = \"10.0.0.5\" }\n"
        "db = hosts[\"db_${env}\"]\n"
        "tpl = { \"$${x}\" = \"literal\" }\n"
        "v = tpl[\"$${x}\"]\n"
        "o = { (n) = \"three\", (true) = \"yes\", (\"${env}\") = 1, \"%{if n > 2}big%{endif}\" = 2 }\n",
        NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK_STREQ(cl_get_string(root, "db", NULL), "10.0.0.5");
    CL_CHECK_STREQ(cl_get_string(root, "v", NULL), "literal");
    CL_CHECK_STREQ(cl_get_string(root, "tpl[\"${x}\"]", NULL), "literal");
    CL_CHECK_STREQ(cl_get_string(root, "o[\"3\"]", NULL), "three");
    CL_CHECK_STREQ(cl_get_string(root, "o[\"true\"]", NULL), "yes");
    CL_CHECK(cl_get_int(root, "o.prod", 0) == 1);
    CL_CHECK(cl_get_int(root, "o.big", 0) == 2);
    cl_evaluated_free(result);
}

static void test_computed_keys_in_for_scope(void) {
    cl_evaluated_t *result = eval_source("x = [for s in [\"a\", \"b\"] : { \"k_${s}\" = s }]\n", NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK_STREQ(cl_get_string(root, "x[0].k_a", NULL), "a");
    CL_CHECK_STREQ(cl_get_string(root, "x[1].k_b", NULL), "b");
    cl_evaluated_free(result);
}

/* A repeated key is reported at the repeated key when it is computed, or
 * at its value otherwise (constant keys carry no position of their own). */
static void test_key_errors(void) {
    check_eval_error("o = { a = 1, a = 2 }\n", "chave 'a' duplicada em objeto", 1, 18);
    check_eval_error("k = \"a\"\no = { a = 1, (k) = 2 }\n", "chave 'a' duplicada em objeto", 2, 15);
    check_eval_error("k = \"a\"\no = { \"${k}\" = 1, a = 2 }\n", "chave 'a' duplicada em objeto", 2, 23);
    check_eval_error("o = { ([1]) = 1 }\n", "chave de objeto precisa ser string, numero ou bool", 1, 8);
    check_eval_error("o = { (null) = 1 }\n", "chave de objeto precisa ser string, numero ou bool", 1, 8);
    check_eval_error("o = { \"${nope}\" = 1 }\n", "referencia 'nope' nao encontrada", 1, 10);
}

static void test_computed_labels(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "region", "eu");
    cl_evaluated_t *result = eval_source(
        "env = \"prod\"\n"
        "server \"web-${env}\" {\n"
        "  port = 80\n"
        "  tls \"${region}\" {\n"
        "    on = true\n"
        "  }\n"
        "}\n"
        "server \"$${raw}\" {\n"
        "  port = 81\n"
        "}\n"
        "p = server.web-prod.port + 1\n",
        b);
    cl_bindings_free(b);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK(cl_get_int(root, "server.web-prod.port", 0) == 80);
    CL_CHECK(cl_get_bool(root, "server.web-prod.tls.eu.on", 0) == 1);
    CL_CHECK(cl_get_int(root, "server[\"${raw}\"].port", 0) == 81);
    CL_CHECK(cl_get_int(root, "p", 0) == 81);
    cl_evaluated_free(result);
}

static void test_label_errors(void) {
    /* the label needs `name`, which needs the block the label names */
    check_eval_error("name = server.api.port\nserver \"${name}\" {\n  port = 80\n}\n",
                     "referencia circular no rotulo do bloco 'server'", 2, 8);
    /* the label reads its own block */
    check_eval_error("server \"${server.x.port}\" {\n  port = 80\n}\nx = server.a.port\n",
                     "referencia circular no rotulo do bloco 'server'", 1, 8);
    check_eval_error("server \"${nope}\" {}\n", "referencia 'nope' nao encontrada", 1, 11);
    check_eval_error("server \"${[1]}\" {}\n", "nao e possivel converter esse valor para string", 1, 11);
}

/* A computed label only has a value once evaluated: a document that never
 * reaches it through a traversal still evaluates it when building the
 * result, so its errors surface either way. */
static void test_label_evaluated_even_if_unused(void) {
    check_eval_error("a = 1\nserver \"${missing}\" {}\n", "referencia 'missing' nao encontrada", 2, 11);
}

static void test_number_interpolation_keeps_precision(void) {
    cl_evaluated_t *result = eval_source("pi = \"${3.141592653589793}\"\nbig = \"${1e30}\"\n", NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK_STREQ(cl_get_string(root, "pi", NULL), "3.141592653589793");
    CL_CHECK_STREQ(cl_get_string(root, "big", NULL), "1e+30");
    cl_evaluated_free(result);

    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_number(b, "inf", HUGE_VAL);
    cl_document_t *doc = load("s = \"${inf}\"\n");
    if (doc) {
        cl_error_t err;
        cl_evaluated_t *r = cl_document_evaluate_with(doc, b, &err);
        CL_CHECK(r == NULL);
        if (r) {
            cl_evaluated_free(r);
        } else {
            CL_CHECK(strstr(err.message, "NaN ou infinito") != NULL);
        }
        cl_document_free(doc);
    }
    cl_bindings_free(b);
}

/* ---- writer ---------------------------------------------------------- */

static void check_round_trip(const char *source) {
    cl_document_t *doc = load(source);
    if (!doc) {
        return;
    }
    char *text = cl_document_to_string(doc);
    CL_CHECK_STREQ(text, source);
    free(text);
    cl_document_free(doc);
}

static void test_writer_round_trips(void) {
    check_round_trip("o = {\n  \"k${n}\" = 1\n  (n + 1) = 2\n  \"$${lit}\" = 3\n  \"b c\" = 4\n}\n");
    check_round_trip("server \"web-${env}\" \"$${raw}\" {\n  port = 80\n}\n");
    check_round_trip("s = \"%{if a}x%{endif}\"\nt = \"%%{if a}\"\n");
}

/* Labels and keys created through the API are literal text: a "${" in them
 * must come out escaped, or reloading would turn it into interpolation. */
static void test_api_labels_are_literal(void) {
    cl_document_t *doc = load("");
    if (!doc) {
        return;
    }
    const char *const labels[] = {"a${b}"};
    cl_body_add_block(doc, cl_document_root(doc), "x", labels, 1);
    char *text = cl_document_to_string(doc);
    CL_CHECK_STREQ(text, "x \"a$${b}\" {\n}\n");

    cl_document_t *reloaded = load(text);
    if (reloaded) {
        CL_CHECK(cl_body_find_block(cl_document_root(reloaded), "x", labels, 1) != NULL);
        cl_document_free(reloaded);
    }
    free(text);
    cl_document_free(doc);
}

/* ---- schemas ----------------------------------------------------------- */

static void check_schema_error(const char *source, const char *fragment) {
    cl_error_t err;
    cl_schema_t *schema = cl_schema_load_string(source, "schema", &err);
    CL_CHECK(schema == NULL);
    if (schema) {
        cl_schema_free(schema);
    } else {
        CL_CHECK(strstr(err.message, fragment) != NULL);
    }
}

static void test_schema_rejects_computed_names(void) {
    check_schema_error("block \"${t}\" {\n}\n", "rotulo calculado nao e permitido no schema");
    check_schema_error("block \"m\" {\n  cpu = { \"${k}\" = \"number\" }\n}\n",
                       "chave calculada nao e permitida no schema");
}

void cl_test_run_strings(void) {
    test_object_key_kinds();
    test_label_kinds();
    test_computed_keys();
    test_computed_keys_in_for_scope();
    test_key_errors();
    test_computed_labels();
    test_label_errors();
    test_label_evaluated_even_if_unused();
    test_number_interpolation_keeps_precision();
    test_writer_round_trips();
    test_api_labels_are_literal();
    test_schema_rejects_computed_names();
}
