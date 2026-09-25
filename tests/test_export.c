#include "cl/cl.h"
#include "test_util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CL_FIXTURES_DIR
#define CL_FIXTURES_DIR "cl"
#endif

static cl_evaluated_t *eval_source(const char *source, const cl_bindings_t *bindings) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "export", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return NULL;
    }
    cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, &err);
    CL_CHECK(result != NULL);
    cl_document_free(doc);
    return result;
}

/* Checks the text a to_string/to_json call returned, then frees it. */
static void check_text(char *actual, const char *expected) {
    CL_CHECK_STREQ(actual, expected);
    free(actual);
}

static void test_values_to_string(void) {
    cl_evaluated_t *result = eval_source(
        "s     = \"say \\\"hi\\\"\\n\"\n"
        "tpl   = \"cost: $${x} and %%{y}\"\n"
        "n     = 0.1\n"
        "list  = [1, \"a\", true, null]\n"
        "empty = []\n"
        "obj   = { name = \"x\", \"with space\" = [1], nested = {} }\n",
        NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    check_text(cl_value_to_string(cl_get_value(root, "s")), "\"say \\\"hi\\\"\\n\"");
    check_text(cl_value_to_string(cl_get_value(root, "tpl")), "\"cost: $${x} and %%{y}\"");
    check_text(cl_value_to_string(cl_get_value(root, "n")), "0.1");
    check_text(cl_value_to_string(cl_get_value(root, "list")), "[1, \"a\", true, null]");
    check_text(cl_value_to_string(cl_get_value(root, "empty")), "[]");
    check_text(cl_value_to_string(cl_get_value(root, "obj")),
               "{\n  name = \"x\"\n  \"with space\" = [1]\n  nested = {}\n}");
    CL_CHECK(cl_value_to_string(NULL) == NULL);
    cl_evaluated_free(result);
}

/* Keys and labels written with "$${" hold the literal text "${", and the
 * export escapes it again so the output reloads to the same thing. */
static void test_literal_labels_and_keys(void) {
    const char *source =
        "o = {\n"
        "  \"k$${x}\" = \"v$${y}\"\n"
        "}\n"
        "x \"l$${z}\" {\n"
        "}\n";
    cl_evaluated_t *result = eval_source(source, NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK_STREQ(cl_get_string(root, "o[\"k${x}\"]", NULL), "v${y}");
    const cl_evaluated_block_t *x = cl_get_block(root, "x[\"l${z}\"]");
    CL_CHECK(x != NULL);
    check_text(cl_evaluated_to_string(result), source);
    cl_evaluated_free(result);
}

static void test_values_to_json(void) {
    cl_evaluated_t *result = eval_source(
        "s     = \"q\\\" b\\\\ \\t\\n\"\n"
        "list  = [1, 2.5, \"a\", false, null]\n"
        "empty = []\n"
        "obj   = { a = { b = [] }, \"$${k}\" = {} }\n",
        NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    check_text(cl_value_to_json(cl_get_value(root, "s")), "\"q\\\" b\\\\ \\t\\n\"");
    check_text(cl_value_to_json(cl_get_value(root, "list")), "[\n  1,\n  2.5,\n  \"a\",\n  false,\n  null\n]");
    check_text(cl_value_to_json(cl_get_value(root, "empty")), "[]");
    /* "$${k}" is the literal text ${k}, in keys like everywhere else */
    check_text(cl_value_to_json(cl_get_value(root, "obj")),
               "{\n  \"a\": {\n    \"b\": []\n  },\n  \"${k}\": {}\n}");
    CL_CHECK(cl_value_to_json(NULL) == NULL);
    cl_evaluated_free(result);
}

/* Control characters, NaN and infinities have no literal cl form, so they
 * come in through bindings. */
static void test_special_values(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "ctrl", "a\001b\037c");
    cl_bindings_set_number(b, "pos_inf", HUGE_VAL);
    cl_bindings_set_number(b, "neg_inf", -HUGE_VAL);
    volatile double zero = 0.0;
    cl_bindings_set_number(b, "nan", zero / zero);
    cl_evaluated_t *result = eval_source("ctrl = \"\"\npos_inf = 0\nneg_inf = 0\nnan = 0\n", b);
    cl_bindings_free(b);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    check_text(cl_value_to_json(cl_get_value(root, "ctrl")), "\"a\\u0001b\\u001fc\"");
    check_text(cl_value_to_json(cl_get_value(root, "pos_inf")), "null");
    check_text(cl_value_to_json(cl_get_value(root, "nan")), "null");
    check_text(cl_value_to_string(cl_get_value(root, "pos_inf")), "1e999");
    check_text(cl_value_to_string(cl_get_value(root, "neg_inf")), "-1e999");
    check_text(cl_value_to_string(cl_get_value(root, "nan")), "null");
    cl_evaluated_free(result);
}

static const char *const doc_source =
    "name = \"app\"\n"
    "full = \"${name}-${1 + 1}\"\n"
    "server \"web\" \"eu\" {\n"
    "  port = 80\n"
    "  tls {\n"
    "    on = true\n"
    "  }\n"
    "}\n"
    "empty {\n"
    "}\n";

static void test_document_to_string(void) {
    cl_evaluated_t *result = eval_source(doc_source, NULL);
    if (!result) {
        return;
    }
    check_text(cl_evaluated_to_string(result),
               "name = \"app\"\n"
               "full = \"app-2\"\n"
               "server \"web\" \"eu\" {\n"
               "  port = 80\n"
               "  tls {\n"
               "    on = true\n"
               "  }\n"
               "}\n"
               "empty {\n"
               "}\n");
    check_text(cl_evaluated_block_to_string(cl_get_block(cl_evaluated_root(result), "server.web.eu.tls")),
               "tls {\n  on = true\n}\n");
    CL_CHECK(cl_evaluated_to_string(NULL) == NULL);
    CL_CHECK(cl_evaluated_block_to_string(NULL) == NULL);
    cl_evaluated_free(result);
}

static void test_document_to_json(void) {
    cl_evaluated_t *result = eval_source(doc_source, NULL);
    if (!result) {
        return;
    }
    check_text(cl_evaluated_to_json(result),
               "{\n"
               "  \"attributes\": {\n"
               "    \"name\": \"app\",\n"
               "    \"full\": \"app-2\"\n"
               "  },\n"
               "  \"blocks\": [\n"
               "    {\n"
               "      \"type\": \"server\",\n"
               "      \"labels\": [\"web\", \"eu\"],\n"
               "      \"body\": {\n"
               "        \"attributes\": {\n"
               "          \"port\": 80\n"
               "        },\n"
               "        \"blocks\": [\n"
               "          {\n"
               "            \"type\": \"tls\",\n"
               "            \"labels\": [],\n"
               "            \"body\": {\n"
               "              \"attributes\": {\n"
               "                \"on\": true\n"
               "              },\n"
               "              \"blocks\": []\n"
               "            }\n"
               "          }\n"
               "        ]\n"
               "      }\n"
               "    },\n"
               "    {\n"
               "      \"type\": \"empty\",\n"
               "      \"labels\": [],\n"
               "      \"body\": {\n"
               "        \"attributes\": {},\n"
               "        \"blocks\": []\n"
               "      }\n"
               "    }\n"
               "  ]\n"
               "}\n");
    check_text(cl_evaluated_block_to_json(cl_get_block(cl_evaluated_root(result), "empty")),
               "{\n"
               "  \"type\": \"empty\",\n"
               "  \"labels\": [],\n"
               "  \"body\": {\n"
               "    \"attributes\": {},\n"
               "    \"blocks\": []\n"
               "  }\n"
               "}\n");
    CL_CHECK(cl_evaluated_to_json(NULL) == NULL);
    CL_CHECK(cl_evaluated_block_to_json(NULL) == NULL);
    cl_evaluated_free(result);
}

/* JSON keys must be unique: a repeated attribute keeps only its first
 * (effective) value, while the cl form keeps both. */
static void test_repeated_attribute(void) {
    cl_evaluated_t *result = eval_source("a = 1\nb = 2\na = 3\n", NULL);
    if (!result) {
        return;
    }
    check_text(cl_evaluated_to_json(result),
               "{\n  \"attributes\": {\n    \"a\": 1,\n    \"b\": 2\n  },\n  \"blocks\": []\n}\n");
    check_text(cl_evaluated_to_string(result), "a = 1\nb = 2\na = 3\n");
    cl_evaluated_free(result);
}

/* Evaluates `doc`, flattens it, then reloads and flattens that: the
 * flattened document must evaluate to itself. */
static void check_flatten_is_fixed_point(const char *label, cl_document_t *doc) {
    cl_error_t err;
    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (!result) {
        fprintf(stderr, "  (while evaluating %s: %s)\n", label, err.message);
        return;
    }
    char *first = cl_evaluated_to_string(result);
    cl_evaluated_free(result);

    cl_document_t *reloaded = cl_load_string(first, label, &err);
    CL_CHECK(reloaded != NULL);
    if (!reloaded) {
        fprintf(stderr, "  (while reloading flattened %s: %s)\n", label, err.message);
        free(first);
        return;
    }
    cl_evaluated_t *again = cl_document_evaluate(reloaded, &err);
    CL_CHECK(again != NULL);
    if (again) {
        char *second = cl_evaluated_to_string(again);
        if (!first || !second || strcmp(first, second) != 0) {
            fprintf(stderr, "%s:%d: FAIL: flattened %s does not evaluate to itself\n", __FILE__, __LINE__, label);
            cl_test_failures++;
        }
        free(second);
        cl_evaluated_free(again);
    } else {
        fprintf(stderr, "  (while evaluating flattened %s: %s)\n", label, err.message);
    }
    cl_document_free(reloaded);
    free(first);
}

static void test_flatten_fixtures(void) {
    /* the fixtures that evaluate (see test_fixtures.c) */
    static const char *fixtures[] = {"00.cl", "03.cl", "04.cl", "05.cl", "06.cl", "08.cl", "100.cl"};
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", CL_FIXTURES_DIR, fixtures[i]);
        cl_error_t err;
        cl_document_t *doc = cl_load_file(path, &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            fprintf(stderr, "  (while loading %s: %s)\n", path, err.message);
            continue;
        }
        check_flatten_is_fixed_point(path, doc);
        cl_document_free(doc);
    }
}

void cl_test_run_export(void) {
    test_values_to_string();
    test_literal_labels_and_keys();
    test_values_to_json();
    test_special_values();
    test_document_to_string();
    test_document_to_json();
    test_repeated_attribute();
    test_flatten_fixtures();
}
