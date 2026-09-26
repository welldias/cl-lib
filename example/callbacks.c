/* cl_callbacks_example - how to expose C functions (host functions) to a
 * document through cl_bindings_set_function().
 *
 * Covers: fixed and variadic arity, reading arguments of every kind,
 * returning strings/numbers/lists/objects, returning an argument as-is,
 * userdata (host state shared with the callback), cl_call_copy() for
 * values owned by the bindings, reporting errors with cl_call_error(), and
 * replacing a built-in.
 *
 * Every document is embedded as a string, so the program runs from any
 * working directory. */
#include "cl/cl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Callbacks                                                            */
/* ------------------------------------------------------------------ */

/* slug("Hello World") -> "hello-world": one string in, one string out. */
static cl_value_t *fn_slug(cl_call_t *call, void *userdata) {
    (void)userdata;
    const char *s = cl_value_as_string(cl_call_arg(call, 0));
    if (!s) {
        return cl_call_error(call, "argument 1 must be a string");
    }
    char buf[256];
    size_t n = 0;
    for (; s[n] && n + 1 < sizeof(buf); n++) {
        buf[n] = s[n] == ' ' ? '-' : (char)tolower((unsigned char)s[n]);
    }
    buf[n] = '\0';
    return cl_call_string(call, buf);
}

/* sum(1, 2, 3) or sum(list...) -> 6: variadic, any number of numbers. */
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

/* cidr_hosts("10.0.0.0", 3) -> ["10.0.0.1", "10.0.0.2", "10.0.0.3"]:
 * builds a new list. */
static cl_value_t *fn_cidr_hosts(cl_call_t *call, void *userdata) {
    (void)userdata;
    const char *base = cl_value_as_string(cl_call_arg(call, 0));
    double count;
    unsigned a, b, c, d;
    if (!base || sscanf(base, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return cl_call_error(call, "argument 1 must be an IPv4 address");
    }
    if (cl_value_as_number(cl_call_arg(call, 1), &count) != 0 || count < 0 || count > 254) {
        return cl_call_error(call, "argument 2 must be a number between 0 and 254");
    }
    cl_value_t *list = cl_call_list(call);
    for (unsigned i = 1; i <= (unsigned)count; i++) {
        char ip[32];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a, b, c, d + i);
        cl_call_list_add(call, list, cl_call_string(call, ip));
    }
    return list;
}

/* endpoint("api", 8080) -> { host = "api", port = 8080, url = "..." }:
 * builds a new object, reusing an argument as one of its values. */
static cl_value_t *fn_endpoint(cl_call_t *call, void *userdata) {
    (void)userdata;
    cl_value_t *host = cl_call_arg(call, 0);
    double port;
    if (!cl_value_as_string(host)) {
        return cl_call_error(call, "argument 1 must be a string");
    }
    if (cl_value_as_number(cl_call_arg(call, 1), &port) != 0) {
        return cl_call_error(call, "argument 2 must be a number");
    }
    char url[256];
    snprintf(url, sizeof(url), "http://%s:%.0f", cl_value_as_string(host), port);

    cl_value_t *object = cl_call_object(call);
    cl_call_object_set(call, object, "host", host); /* arguments may be added as they are */
    cl_call_object_set(call, object, "port", cl_call_number(call, port));
    cl_call_object_set(call, object, "url", cl_call_string(call, url));
    return object;
}

/* first_non_empty(a, b, ...) returns one of its arguments unchanged: no
 * copy needed. */
static cl_value_t *fn_first_non_empty(cl_call_t *call, void *userdata) {
    (void)userdata;
    for (size_t i = 0; i < cl_call_argc(call); i++) {
        cl_value_t *arg = cl_call_arg(call, i);
        const char *s = cl_value_as_string(arg);
        if (s && *s) {
            return arg;
        }
    }
    return cl_call_null(call);
}

/* ---- userdata: host state the callback reads and writes ---------------- */

/* secret("db") looks the name up in a table owned by the host program and
 * counts how many secrets the document asked for. */
typedef struct secret_store {
    const char *const *names;
    const char *const *values;
    size_t count;
    int lookups;
} secret_store_t;

static cl_value_t *fn_secret(cl_call_t *call, void *userdata) {
    secret_store_t *store = userdata;
    const char *name = cl_value_as_string(cl_call_arg(call, 0));
    if (!name) {
        return cl_call_error(call, "argument 1 must be a string");
    }
    for (size_t i = 0; i < store->count; i++) {
        if (strcmp(store->names[i], name) == 0) {
            store->lookups++;
            return cl_call_string(call, store->values[i]);
        }
    }
    return cl_call_error(call, "unknown secret '%s'", name);
}

/* region_defaults() returns a value built once with cl_bindings_*() and
 * passed as userdata. It belongs to the bindings, not to this call, so it
 * must go through cl_call_copy(). */
static cl_value_t *fn_region_defaults(cl_call_t *call, void *userdata) {
    const cl_value_t *defaults = userdata;
    return cl_call_copy(call, defaults);
}

/* upper() replaced: a host function shadows the built-in of the same name.
 * This one also adds a marker so the difference is visible. */
static cl_value_t *fn_upper_loud(cl_call_t *call, void *userdata) {
    (void)userdata;
    const char *s = cl_value_as_string(cl_call_arg(call, 0));
    if (!s) {
        return cl_call_error(call, "argument 1 must be a string");
    }
    char buf[256];
    size_t n = 0;
    for (; s[n] && n + 2 < sizeof(buf); n++) {
        buf[n] = (char)toupper((unsigned char)s[n]);
    }
    buf[n++] = '!';
    buf[n] = '\0';
    return cl_call_string(call, buf);
}

/* ------------------------------------------------------------------ */
/* Running documents                                                    */
/* ------------------------------------------------------------------ */

static const char *const DOCUMENT =
    "title = \"Release Notes 2026\"\n"
    "sizes = [10, 20, 30]\n"
    "\n"
    "page {\n"
    "  path  = \"/posts/${slug(title)}\"\n"
    "  total = sum(1, 2, 3)\n"
    "  disk  = sum(sizes...)            # a trailing ... expands the list\n"
    "  none  = sum()                    # variadic: zero arguments is fine\n"
    "}\n"
    "\n"
    "service \"api\" {\n"
    "  hosts    = cidr_hosts(\"10.0.0.0\", 3)\n"
    "  endpoint = endpoint(\"api\", 8080)\n"
    "  url      = endpoint(\"api\", 8080).url   # results chain like any value\n"
    "  owner    = first_non_empty(\"\", \"\", \"platform-team\")\n"
    "  password = secret(\"db\")\n"
    "  token    = secret(\"api_token\")\n"
    "  region   = region_defaults()\n"
    "  zone     = region_defaults().zones[0]\n"
    "  banner   = upper(\"deployed\")\n"
    "}\n";

/* Registers every callback above. `store` and the bindings themselves are
 * the userdata of two of them. */
static cl_bindings_t *make_bindings(secret_store_t *store) {
    cl_bindings_t *b = cl_bindings_new();

    /*                           name               min max                    fn                  userdata */
    cl_bindings_set_function(b, "slug",            1, 1,                     fn_slug,            NULL);
    cl_bindings_set_function(b, "sum",             0, CL_FUNCTION_VARIADIC,  fn_sum,             NULL);
    cl_bindings_set_function(b, "cidr_hosts",      2, 2,                     fn_cidr_hosts,      NULL);
    cl_bindings_set_function(b, "endpoint",        2, 2,                     fn_endpoint,        NULL);
    cl_bindings_set_function(b, "first_non_empty", 1, CL_FUNCTION_VARIADIC,  fn_first_non_empty, NULL);
    cl_bindings_set_function(b, "secret",          1, 1,                     fn_secret,          store);
    cl_bindings_set_function(b, "upper",           1, 1,                     fn_upper_loud,      NULL);

    /* A value built with the bindings and handed to the callback. */
    cl_value_t *defaults = cl_bindings_object(b);
    cl_value_t *zones = cl_bindings_list(b);
    cl_bindings_list_add(b, zones, cl_bindings_string(b, "sa-east-1a"));
    cl_bindings_list_add(b, zones, cl_bindings_string(b, "sa-east-1b"));
    cl_bindings_object_set(b, defaults, "name", cl_bindings_string(b, "sa-east-1"));
    cl_bindings_object_set(b, defaults, "zones", zones);
    cl_bindings_set_function(b, "region_defaults", 0, 0, fn_region_defaults, defaults);

    return b;
}

static void run(const char *name, const char *source, const cl_bindings_t *bindings) {
    cl_error_t err;
    printf("-- %s\n", name);
    cl_document_t *doc = cl_load_string(source, name, &err);
    if (!doc) {
        printf("  parse error (line %d, column %d): %s\n", err.line, err.col, err.message);
        return;
    }
    cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, &err);
    if (!result) {
        /* cl_call_error() and arity errors land here, at the call's position */
        printf("  evaluation error (line %d, column %d): %s\n", err.line, err.col, err.message);
        cl_document_free(doc);
        return;
    }
    char *text = cl_evaluated_to_string(result);
    fputs(text, stdout);
    free(text);
    cl_evaluated_free(result);
    cl_document_free(doc);
}

int main(void) {
    static const char *const secret_names[] = {"db", "api_token"};
    static const char *const secret_values[] = {"s3cr3t", "tok-42"};
    secret_store_t store = {secret_names, secret_values, 2, 0};

    cl_bindings_t *bindings = make_bindings(&store);

    printf("== host functions ==\n");
    run("document.cl", DOCUMENT, bindings);
    printf("  (secret() was called %d times)\n", store.lookups);

    /* Failures stop the evaluation and point at the offending call. */
    printf("\n== errors ==\n");
    run("wrong arity", "x = slug(\"a\", \"b\")\n", bindings);
    run("cl_call_error from the callback", "x = secret(\"missing\")\n", bindings);
    run("bad argument type", "x = sum(1, \"two\")\n", bindings);
    run("function not registered", "x = unknown_fn(1)\n", bindings);

    /* The callbacks only run during cl_document_evaluate_with(), so the
     * bindings can be freed as soon as it returns. */
    cl_bindings_free(bindings);

    /* Without the bindings, upper() is the built-in again. */
    printf("\n== without bindings ==\n");
    run("built-in upper", "banner = upper(\"deployed\")\n", NULL);

    return 0;
}
