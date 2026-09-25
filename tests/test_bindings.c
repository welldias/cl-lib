#include "cl/cl.h"
#include "test_util.h"

#include <stdlib.h>

/* Evaluates `source` with `bindings` and returns the result (NULL on
 * failure, with the error left in *err). The document is freed before
 * returning: the evaluated tree must not depend on it. */
static cl_evaluated_t *eval_with(const char *source, const cl_bindings_t *bindings, cl_error_t *err) {
    cl_document_t *doc = cl_load_string(source, "bindings", err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return NULL;
    }
    cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, err);
    cl_document_free(doc);
    return result;
}

static const cl_value_t *attr_value(cl_evaluated_t *result, const char *name) {
    cl_evaluated_attribute_t *attr = cl_evaluated_body_get_attribute(cl_evaluated_root(result), name);
    CL_CHECK(attr != NULL);
    return attr ? attr->value : NULL;
}

static double number_of(const cl_value_t *v) {
    double n = -12345;
    CL_CHECK(cl_value_as_number(v, &n) == 0);
    return n;
}

static void test_bound_names_resolve(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "env", "prod");
    cl_bindings_set_number(b, "replicas", 6);
    cl_bindings_set_bool(b, "enabled", 1);

    cl_error_t err;
    cl_evaluated_t *result = eval_with(
        "service \"api\" {\n"
        "  name     = \"api-${env}\"\n"
        "  replicas = replicas > 2 ? replicas / 2 : 1\n"
        "}\n"
        "debug = env != \"prod\" || !enabled\n"
        "summary = \"${upper(env)}:${replicas}\"\n",
        b, &err);
    cl_bindings_free(b); /* the result holds copies, not pointers into b */

    CL_CHECK(result != NULL);
    if (!result) {
        return;
    }
    const char *label = "api";
    cl_evaluated_block_t *api = cl_evaluated_body_find_block(cl_evaluated_root(result), "service", &label, 1);
    CL_CHECK(api != NULL);
    if (api) {
        CL_CHECK_STREQ(cl_value_as_string(cl_evaluated_body_get_attribute(api->body, "name")->value), "api-prod");
        CL_CHECK(number_of(cl_evaluated_body_get_attribute(api->body, "replicas")->value) == 3);
    }
    int debug = -1;
    CL_CHECK(cl_value_as_bool(attr_value(result, "debug"), &debug) == 0 && debug == 0);
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "summary")), "PROD:6");
    cl_evaluated_free(result);
}

/* Without bindings, a document that relies on them fails like any other
 * unresolved reference; NULL bindings behave like cl_document_evaluate(). */
static void test_missing_binding_is_eval_error(void) {
    cl_error_t err;
    cl_evaluated_t *result = eval_with("x = \"api-${env}\"\n", NULL, &err);
    CL_CHECK(result == NULL);
    if (!result) {
        CL_CHECK(strstr(err.message, "referencia 'env' nao encontrada") != NULL);
    } else {
        cl_evaluated_free(result);
    }

    cl_bindings_t *empty = cl_bindings_new();
    result = eval_with("x = 1\n", empty, &err);
    CL_CHECK(result != NULL);
    cl_evaluated_free(result);
    cl_bindings_free(empty);
}

/* A binding overrides a same-named TOP-LEVEL attribute both where it is
 * referenced and in the evaluated attribute itself; a same-named attribute
 * nested inside a block is untouched. */
static void test_binding_overrides_top_level_attribute(void) {
    static const char *source =
        "env = \"dev\"\n"
        "service \"api\" {\n"
        "  env  = \"inner\"\n"
        "  name = \"api-${env}\"\n"
        "}\n"
        "x = env\n";
    cl_error_t err;

    cl_evaluated_t *defaults = eval_with(source, NULL, &err);
    CL_CHECK(defaults != NULL);
    if (defaults) {
        CL_CHECK_STREQ(cl_value_as_string(attr_value(defaults, "env")), "dev");
        CL_CHECK_STREQ(cl_value_as_string(attr_value(defaults, "x")), "dev");
        cl_evaluated_free(defaults);
    }

    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "env", "prod");
    cl_evaluated_t *result = eval_with(source, b, &err);
    cl_bindings_free(b);
    CL_CHECK(result != NULL);
    if (!result) {
        return;
    }
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "env")), "prod");
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "x")), "prod");
    const char *label = "api";
    cl_evaluated_block_t *api = cl_evaluated_body_find_block(cl_evaluated_root(result), "service", &label, 1);
    CL_CHECK(api != NULL);
    if (api) {
        /* "${env}" inside the block is a top-level reference, so it sees the
         * binding; the block's own "env" attribute is not overridden */
        CL_CHECK_STREQ(cl_value_as_string(cl_evaluated_body_get_attribute(api->body, "name")->value), "api-prod");
        CL_CHECK_STREQ(cl_value_as_string(cl_evaluated_body_get_attribute(api->body, "env")->value), "inner");
    }
    cl_evaluated_free(result);
}

/* An overridden attribute's own expression is never evaluated, so a default
 * that couldn't evaluate on its own doesn't get in the way. */
static void test_overridden_attribute_expression_is_not_evaluated(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_number(b, "port", 8080);
    cl_error_t err;
    cl_evaluated_t *result = eval_with("port = missing_default\nx = port + 1\n", b, &err);
    cl_bindings_free(b);
    CL_CHECK(result != NULL);
    if (result) {
        CL_CHECK(number_of(attr_value(result, "port")) == 8080);
        CL_CHECK(number_of(attr_value(result, "x")) == 8081);
        cl_evaluated_free(result);
    }
}

/* Lookup order: for-variables, then bindings, then top-level attributes,
 * then labeled blocks. */
static void test_lookup_order(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "v", "bound");
    cl_value_t *plant = cl_bindings_object(b);
    cl_value_t *fern = cl_bindings_object(b);
    cl_bindings_object_set(b, fern, "sunlight", cl_bindings_string(b, "from-binding"));
    cl_bindings_object_set(b, plant, "fern", fern);
    cl_bindings_set(b, "plant", plant);

    cl_error_t err;
    cl_evaluated_t *result = eval_with(
        "plant \"fern\" {\n  sunlight = \"from-block\"\n}\n"
        "shadowed = [for v in [\"loop\"] : v][0]\n"
        "unshadowed = v\n"
        "sun = plant.fern.sunlight\n",
        b, &err);
    cl_bindings_free(b);
    CL_CHECK(result != NULL);
    if (!result) {
        return;
    }
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "shadowed")), "loop");
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "unshadowed")), "bound");
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "sun")), "from-binding");
    cl_evaluated_free(result);
}

/* Lists and objects built by the host combine with dynamic indexing. */
static void test_structured_bindings(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "env", "prod");

    cl_value_t *ips = cl_bindings_list(b);
    CL_CHECK(cl_bindings_list_add(b, ips, cl_bindings_string(b, "10.0.0.1")) == 0);
    CL_CHECK(cl_bindings_list_add(b, ips, cl_bindings_string(b, "10.0.0.2")) == 0);

    cl_value_t *servers = cl_bindings_object(b);
    cl_value_t *prod = cl_bindings_object(b);
    CL_CHECK(cl_bindings_object_set(b, prod, "ips", ips) == 0);
    CL_CHECK(cl_bindings_object_set(b, prod, "flag", cl_bindings_null(b)) == 0);
    CL_CHECK(cl_bindings_object_set(b, servers, "prod", prod) == 0);
    CL_CHECK(cl_bindings_set(b, "servers", servers) == 0);

    cl_error_t err;
    cl_evaluated_t *result = eval_with(
        "first = servers[env].ips[0]\n"
        "count = length(servers[env].ips)\n"
        "all   = concat(servers.prod.ips, [\"10.0.0.3\"])\n"
        "none  = servers.prod.flag == null\n",
        b, &err);
    cl_bindings_free(b);
    CL_CHECK(result != NULL);
    if (!result) {
        return;
    }
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "first")), "10.0.0.1");
    CL_CHECK(number_of(attr_value(result, "count")) == 2);
    const cl_value_t *all = attr_value(result, "all");
    CL_CHECK(cl_value_list_count(all) == 3);
    CL_CHECK_STREQ(cl_value_as_string(cl_value_list_at(all, 1)), "10.0.0.2");
    int none = 0;
    CL_CHECK(cl_value_as_bool(attr_value(result, "none"), &none) == 0 && none == 1);
    cl_evaluated_free(result);
}

static void test_binding_api_rules(void) {
    cl_bindings_t *b = cl_bindings_new();

    cl_value_t *list = cl_bindings_list(b);
    cl_value_t *obj = cl_bindings_object(b);
    cl_value_t *num = cl_bindings_number(b, 1);

    /* wrong container kind / NULL arguments */
    CL_CHECK(cl_bindings_list_add(b, obj, num) == -1);
    CL_CHECK(cl_bindings_object_set(b, list, "k", num) == -1);
    CL_CHECK(cl_bindings_list_add(b, list, NULL) == -1);
    CL_CHECK(cl_bindings_set(b, NULL, num) == -1);
    CL_CHECK(cl_bindings_set(b, "x", NULL) == -1);
    CL_CHECK(cl_bindings_set_string(b, "x", NULL) == -1);

    /* cycles are refused, directly and through nesting */
    CL_CHECK(cl_bindings_list_add(b, list, list) == -1);
    CL_CHECK(cl_bindings_object_set(b, obj, "self", obj) == -1);
    CL_CHECK(cl_bindings_list_add(b, list, obj) == 0);
    CL_CHECK(cl_bindings_object_set(b, obj, "back", list) == -1);

    /* the same value may appear in several places (a DAG, not a cycle) */
    CL_CHECK(cl_bindings_list_add(b, list, num) == 0);
    CL_CHECK(cl_bindings_object_set(b, obj, "n", num) == 0);

    /* setting an existing object key or binding name replaces it */
    CL_CHECK(cl_bindings_object_set(b, obj, "n", cl_bindings_number(b, 2)) == 0);
    CL_CHECK(cl_value_object_count(obj) == 1);
    CL_CHECK(cl_bindings_set_number(b, "x", 1) == 0);
    CL_CHECK(cl_bindings_set_number(b, "x", 2) == 0);
    CL_CHECK(cl_bindings_set(b, "list", list) == 0);

    cl_error_t err;
    cl_evaluated_t *result = eval_with("y = x\nz = list[0].n + list[1]\n", b, &err);
    CL_CHECK(result != NULL);
    if (result) {
        CL_CHECK(number_of(attr_value(result, "y")) == 2);
        CL_CHECK(number_of(attr_value(result, "z")) == 3);
        cl_evaluated_free(result);
    }

    /* the same bindings can be reused for several evaluations */
    result = eval_with("y = x * 10\n", b, &err);
    CL_CHECK(result != NULL);
    if (result) {
        CL_CHECK(number_of(attr_value(result, "y")) == 20);
        cl_evaluated_free(result);
    }

    cl_bindings_free(b);
    cl_bindings_free(NULL);
}

void cl_test_run_bindings(void) {
    test_bound_names_resolve();
    test_missing_binding_is_eval_error();
    test_binding_overrides_top_level_attribute();
    test_overridden_attribute_expression_is_not_evaluated();
    test_lookup_order();
    test_structured_bindings();
    test_binding_api_rules();
}
