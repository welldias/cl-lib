#include "cl/cl.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        CL_CHECK(strstr(err.message, "reference 'env' not found") != NULL);
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

/* ---- host functions ------------------------------------------------ */

static cl_value_t *fn_double(cl_call_t *call, void *userdata) {
    (void)userdata;
    double n;
    if (cl_value_as_number(cl_call_arg(call, 0), &n) != 0) {
        return cl_call_error(call, "argument 1 must be a number");
    }
    return cl_call_number(call, n * 2);
}

/* greet(name): "<prefix>, <name>", prefix taken from userdata. */
static cl_value_t *fn_greet(cl_call_t *call, void *userdata) {
    const char *name = cl_value_as_string(cl_call_arg(call, 0));
    if (!name) {
        return cl_call_error(call, "argument 1 must be a string");
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s, %s", (const char *)userdata, name);
    return cl_call_string(call, buf);
}

/* sum(n...): variadic. */
static cl_value_t *fn_sum(cl_call_t *call, void *userdata) {
    (void)userdata;
    double total = 0;
    for (size_t i = 0; i < cl_call_argc(call); i++) {
        double n;
        if (cl_value_as_number(cl_call_arg(call, i), &n) != 0) {
            return cl_call_error(call, "argument %zu must be a number", i + 1);
        }
        total += n;
    }
    return cl_call_number(call, total);
}

static cl_value_t *fn_first(cl_call_t *call, void *userdata) {
    (void)userdata;
    return cl_call_arg(call, 0); /* returning an argument as-is is allowed */
}

static cl_value_t *fn_shout(cl_call_t *call, void *userdata) {
    (void)userdata;
    return cl_call_string(call, "SHADOWED");
}

static cl_value_t *fn_fails_silently(cl_call_t *call, void *userdata) {
    (void)call;
    (void)userdata;
    return NULL;
}

/* Errors win even when the callback returns a value afterwards. */
static cl_value_t *fn_error_then_value(cl_call_t *call, void *userdata) {
    (void)userdata;
    cl_call_error(call, "boom %d", 42);
    return cl_call_number(call, 1);
}

/* wrap(x): { name = "<fn name>", value = x, items = [x, x], extra = <binding copy> } */
static cl_value_t *fn_wrap(cl_call_t *call, void *userdata) {
    const cl_value_t *extra = userdata; /* owned by a cl_bindings_t */
    cl_value_t *arg = cl_call_arg(call, 0);

    /* arguments are read-only: only containers made by this call can grow */
    CL_CHECK(cl_value_kind(arg) != CL_VAL_LIST || cl_call_list_add(call, arg, cl_call_null(call)) == -1);

    cl_value_t *items = cl_call_list(call);
    CL_CHECK(cl_call_list_add(call, items, arg) == 0);
    CL_CHECK(cl_call_list_add(call, items, arg) == 0);
    CL_CHECK(cl_call_list_add(call, items, items) == -1); /* cycle */
    CL_CHECK(cl_call_list_add(call, NULL, arg) == -1);

    cl_value_t *obj = cl_call_object(call);
    CL_CHECK(cl_call_list_add(call, obj, arg) == -1); /* wrong kind */
    CL_CHECK(cl_call_object_set(call, obj, "name", cl_call_string(call, cl_call_name(call))) == 0);
    CL_CHECK(cl_call_object_set(call, obj, "value", cl_call_null(call)) == 0);
    CL_CHECK(cl_call_object_set(call, obj, "value", arg) == 0); /* replaces in place */
    CL_CHECK(cl_call_object_set(call, obj, "items", items) == 0);
    CL_CHECK(cl_call_list_add(call, items, obj) == -1); /* obj reaches items: cycle */

    cl_value_t *copy = cl_call_copy(call, extra);
    CL_CHECK(cl_call_list_add(call, copy, cl_call_bool(call, 1)) == 0); /* a copy is ours to extend */
    CL_CHECK(cl_call_object_set(call, obj, "extra", copy) == 0);

    CL_CHECK(cl_call_arg(call, 1) == NULL);
    return obj;
}

static void test_host_functions(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_value_t *extra = cl_bindings_list(b);
    cl_bindings_list_add(b, extra, cl_bindings_string(b, "from-bindings"));

    CL_CHECK(cl_bindings_set_function(b, "double", 1, 1, fn_double, NULL) == 0);
    CL_CHECK(cl_bindings_set_function(b, "greet", 1, 1, fn_greet, "Hello") == 0);
    CL_CHECK(cl_bindings_set_function(b, "sum", 0, CL_FUNCTION_VARIADIC, fn_sum, NULL) == 0);
    CL_CHECK(cl_bindings_set_function(b, "first", 1, CL_FUNCTION_VARIADIC, fn_first, NULL) == 0);
    CL_CHECK(cl_bindings_set_function(b, "wrap", 1, 1, fn_wrap, extra) == 0);
    CL_CHECK(cl_bindings_set_function(b, "upper", 1, 1, fn_shout, NULL) == 0); /* shadows the built-in */
    /* a function and a value binding may share a name */
    CL_CHECK(cl_bindings_set_number(b, "double", 7) == 0);

    cl_error_t err;
    cl_evaluated_t *result = eval_with(
        "a = double(21)\n"
        "b = greet(\"Ada\")\n"
        "c = \"${greet(\"Bob\")}!\"\n"
        "d = sum()\n"
        "e = sum([1, 2, 3]...)\n"
        "f = first([1, 2], 3)\n"
        "g = upper(\"x\")\n"
        "h = lower(\"ABC\")\n" /* built-ins not shadowed still work */
        "i = double(double)\n"
        "w = wrap([5])\n",
        b, &err);
    CL_CHECK(result != NULL);
    cl_bindings_free(b); /* the result must not depend on the bindings */
    if (!result) {
        fprintf(stderr, "  %s\n", err.message);
        return;
    }

    CL_CHECK(number_of(attr_value(result, "a")) == 42);
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "b")), "Hello, Ada");
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "c")), "Hello, Bob!");
    CL_CHECK(number_of(attr_value(result, "d")) == 0);
    CL_CHECK(number_of(attr_value(result, "e")) == 6);
    CL_CHECK(cl_value_list_count(attr_value(result, "f")) == 2);
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "g")), "SHADOWED");
    CL_CHECK_STREQ(cl_value_as_string(attr_value(result, "h")), "abc");
    CL_CHECK(number_of(attr_value(result, "i")) == 14);

    char *w = cl_value_to_string(attr_value(result, "w"));
    CL_CHECK_STREQ(w, "{\n  name = \"wrap\"\n  value = [5]\n  items = [[5], [5]]\n  extra = [\"from-bindings\", true]\n}");
    free(w);

    cl_evaluated_free(result);
}

static void check_host_error(cl_bindings_t *b, const char *source, const char *fragment, int line, int col) {
    cl_error_t err;
    cl_evaluated_t *result = eval_with(source, b, &err);
    CL_CHECK(result == NULL);
    if (result) {
        cl_evaluated_free(result);
        return;
    }
    CL_CHECK(strstr(err.message, fragment) != NULL);
    if (!strstr(err.message, fragment)) {
        fprintf(stderr, "  source: %s  got: %s\n", source, err.message);
    }
    if (line) {
        CL_CHECK(err.line == line && err.col == col);
    }
}

static void test_host_function_errors(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_function(b, "double", 1, 1, fn_double, NULL);
    cl_bindings_set_function(b, "sum", 1, 3, fn_sum, NULL);
    cl_bindings_set_function(b, "silent", 0, 0, fn_fails_silently, NULL);
    cl_bindings_set_function(b, "both", 0, 0, fn_error_then_value, NULL);

    check_host_error(b, "a = 1\nx = double(\"s\")\n", "double(): argument 1 must be a number", 2, 5);
    check_host_error(b, "x = \"v${double(true)}\"\n", "double(): argument 1 must be a number", 1, 9);
    check_host_error(b, "x = double(1, 2)\n", "double() expects 1 argument, got 2", 1, 5);
    check_host_error(b, "x = sum()\n", "sum() expects 1 to 3 arguments, got 0", 0, 0);
    check_host_error(b, "x = sum(1, \"2\")\n", "sum(): argument 2 must be a number", 0, 0);
    check_host_error(b, "x = silent()\n", "silent() failed without reporting an error", 1, 5);
    check_host_error(b, "x = both()\n", "both(): boom 42", 0, 0);
    check_host_error(b, "x = nope()\n", "unknown function 'nope'", 0, 0);
    /* an argument that fails to evaluate never reaches the callback */
    check_host_error(b, "x = double(missing)\n", "missing", 0, 0);

    cl_bindings_free(b);
}

static void test_host_function_registration_rules(void) {
    cl_bindings_t *b = cl_bindings_new();

    CL_CHECK(cl_bindings_set_function(NULL, "f", 0, 0, fn_sum, NULL) == -1);
    CL_CHECK(cl_bindings_set_function(b, NULL, 0, 0, fn_sum, NULL) == -1);
    CL_CHECK(cl_bindings_set_function(b, "", 0, 0, fn_sum, NULL) == -1);
    CL_CHECK(cl_bindings_set_function(b, "f", 0, 0, NULL, NULL) == -1);
    CL_CHECK(cl_bindings_set_function(b, "f", 2, 1, fn_sum, NULL) == -1);

    /* re-registering a name replaces the function and its arity */
    CL_CHECK(cl_bindings_set_function(b, "f", 1, 1, fn_double, NULL) == 0);
    CL_CHECK(cl_bindings_set_function(b, "f", 0, CL_FUNCTION_VARIADIC, fn_sum, NULL) == 0);

    /* bindings holding only functions (no values) still reach evaluation */
    cl_error_t err;
    cl_evaluated_t *result = eval_with("x = f(1, 2, 3, 4)\n", b, &err);
    CL_CHECK(result != NULL);
    if (result) {
        CL_CHECK(number_of(attr_value(result, "x")) == 10);
        cl_evaluated_free(result);
    }

    /* the call API tolerates NULL */
    CL_CHECK(cl_call_name(NULL) == NULL);
    CL_CHECK(cl_call_argc(NULL) == 0);
    CL_CHECK(cl_call_arg(NULL, 0) == NULL);
    CL_CHECK(cl_call_number(NULL, 1) == NULL);
    CL_CHECK(cl_call_list(NULL) == NULL);
    CL_CHECK(cl_call_copy(NULL, NULL) == NULL);
    CL_CHECK(cl_call_error(NULL, "x") == NULL);

    cl_bindings_free(b);
}

void cl_test_run_bindings(void) {
    test_bound_names_resolve();
    test_missing_binding_is_eval_error();
    test_binding_overrides_top_level_attribute();
    test_overridden_attribute_expression_is_not_evaluated();
    test_lookup_order();
    test_structured_bindings();
    test_binding_api_rules();
    test_host_functions();
    test_host_function_errors();
    test_host_function_registration_rules();
}
