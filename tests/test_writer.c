#include "cl/cl.h"
#include "test_util.h"

#include <stdio.h>
#include <stdlib.h>

#ifndef CL_FIXTURES_DIR
#define CL_FIXTURES_DIR "cl"
#endif

static void test_simple_round_trip(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = 1\ny = \"a\"\n", "simple", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    char *first = cl_document_to_string(doc);
    CL_CHECK(first != NULL);

    cl_document_t *reparsed = cl_load_string(first, "simple_reparsed", &err);
    CL_CHECK(reparsed != NULL);
    if (reparsed) {
        char *second = cl_document_to_string(reparsed);
        CL_CHECK_STREQ(first, second);
        free(second);
        cl_document_free(reparsed);
    }

    free(first);
    cl_document_free(doc);
}

/* Every shipped fixture must survive a serialize -> reparse -> serialize
 * round trip with byte-identical output on the second pass (the writer's
 * canonical form must be a fixed point of itself). */
static void test_fixtures_round_trip(void) {
    static const char *fixtures[] = {"00.cl", "01.cl", "02.cl", "03.cl", "04.cl",
                                      "05.cl", "06.cl", "07.cl", "08.cl"};

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

        char *first = cl_document_to_string(doc);
        CL_CHECK(first != NULL);

        cl_document_t *reparsed = cl_load_string(first, fixtures[i], &err);
        CL_CHECK(reparsed != NULL);
        if (reparsed) {
            char *second = cl_document_to_string(reparsed);
            if (!first || !second || strcmp(first, second) != 0) {
                fprintf(stderr, "%s:%d: FAIL: round trip not idempotent for %s\n", __FILE__, __LINE__, path);
                cl_test_failures++;
            }
            free(second);
            cl_document_free(reparsed);
        } else {
            fprintf(stderr, "  (while reparsing serialized %s: %s)\n", path, err.message);
        }

        free(first);
        cl_document_free(doc);
    }
}

/* Postfix chaining onto a binary/conditional/unary base must come back
 * wrapped in defensive parens, or the reparse would silently change the
 * grammar's precedence (postfix binds tighter than unary/binary/"?:"). */
static void test_postfix_round_trip_needs_defensive_parens(void) {
    static const char *sources[] = {
        "x = upper(name).len\n",
        "x = (a + b)[0]\n",
        "x = (a ? b : c).field\n",
        "x = (!flag).field\n",
        "x = [1, 2, 3][0]\n",
        "x = {a = 1}.a\n",
        "x = f(a)[0].b.*[1]\n",
    };

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        cl_error_t err;
        cl_document_t *doc = cl_load_string(sources[i], "postfix_round_trip", &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            continue;
        }

        char *first = cl_document_to_string(doc);
        cl_document_t *reparsed = cl_load_string(first, "postfix_round_trip_2", &err);
        CL_CHECK(reparsed != NULL);
        if (reparsed) {
            char *second = cl_document_to_string(reparsed);
            CL_CHECK_STREQ(first, second);
            free(second);
            cl_document_free(reparsed);
        }
        free(first);
        cl_document_free(doc);
    }
}

static void test_dynamic_index_round_trip(void) {
    static const char *sources[] = {
        "x = zones[i]\n",
        "x = grid[row][col + 1]\n",
        "x = modes[is_open ? \"day\" : \"night\"].humidity\n",
        "x = zones[length(zones) - 1]\n",
        "x = sizes[\"${kind}\"]\n",
        "x = {a = 1}[k]\n",
        "x = [for z in zones : schedule[z]]\n",
        "x = list[*].tags[i]\n",
    };

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        cl_error_t err;
        cl_document_t *doc = cl_load_string(sources[i], "index_round_trip", &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            continue;
        }

        char *first = cl_document_to_string(doc);
        cl_document_t *reparsed = cl_load_string(first, "index_round_trip_2", &err);
        CL_CHECK(reparsed != NULL);
        if (reparsed) {
            char *second = cl_document_to_string(reparsed);
            CL_CHECK_STREQ(first, second);
            free(second);
            cl_document_free(reparsed);
        }
        free(first);
        cl_document_free(doc);
    }
}

/* Trim markers are applied destructively at template-compile time (the
 * literal parts stored in the AST are already trimmed, with no leftover
 * "~" to reproduce) - so the writer's output for a trimmed template won't
 * match the original "~"-bearing source, but it must still be a stable
 * fixed point of itself once reparsed. */
static void test_trim_marker_round_trip_is_stable(void) {
    static const char *sources[] = {
        "x = \"A   ${~name}\"\n",
        "x = \"${name~}   B\"\n",
        "x = <<EOF\nA%{if true~}\n   B%{endif}\nEOF\n",
        "x = <<EOF\n%{for v in items~}\n${v}\n%{~endfor}\nEOF\n",
    };

    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        cl_error_t err;
        cl_document_t *doc = cl_load_string(sources[i], "trim_round_trip", &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            continue;
        }

        char *first = cl_document_to_string(doc);
        cl_document_t *reparsed = cl_load_string(first, "trim_round_trip_2", &err);
        CL_CHECK(reparsed != NULL);
        if (reparsed) {
            char *second = cl_document_to_string(reparsed);
            CL_CHECK_STREQ(first, second);
            free(second);
            cl_document_free(reparsed);
        }
        free(first);
        cl_document_free(doc);
    }
}

/* Serializes `source` and returns the text (NULL on a parse error). */
static char *write_source(const char *source) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "writer", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return NULL;
    }
    char *text = cl_document_to_string(doc);
    cl_document_free(doc);
    return text;
}

/* A literal "$${" / "%%{" decodes to "${" / "%{" text, which must be
 * escaped again on output or it would reload as an interpolation. Labels
 * and object keys keep their raw text and are written back unchanged. */
static void test_template_sequences_stay_text(void) {
    const char *source =
        "a = \"$${x}\"\n"
        "b = \"%%{y} 50%\"\n"
        "c = o[\"k$${z}\"]\n"
        "o = {\n"
        "  \"a$${b}\" = 1\n"
        "}\n"
        "x \"l$${z}\" {\n"
        "}\n";
    char *text = write_source(source);
    CL_CHECK_STREQ(text, source);
    free(text);
}

/* Numbers keep every digit (not %g's 6), and values outside long long
 * don't go through an out-of-range (undefined) cast. */
static void test_numbers_keep_precision(void) {
    char *text = write_source("a = 3.14159265358979\nb = 0.1\nc = 1e30\nd = -2.5e-8\ne = 1e999\nf = 42\n");
    CL_CHECK_STREQ(text, "a = 3.14159265358979\nb = 0.1\nc = 1e+30\nd = -2.5e-08\ne = 1e999\nf = 42\n");
    free(text);

    cl_error_t err;
    cl_document_t *doc = cl_load_string("pi = 3.141592653589793\nthird = 0.3333333333333333\n", "precision", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }
    char *first = cl_document_to_string(doc);
    cl_document_t *reparsed = cl_load_string(first, "precision_reparsed", &err);
    CL_CHECK(reparsed != NULL);
    if (reparsed) {
        double pi = 0, third = 0;
        cl_expr_as_number(cl_body_get_attribute(cl_document_root(reparsed), "pi")->value, &pi);
        cl_expr_as_number(cl_body_get_attribute(cl_document_root(reparsed), "third")->value, &third);
        CL_CHECK(pi == 3.141592653589793);
        CL_CHECK(third == 0.3333333333333333);
        cl_document_free(reparsed);
    }
    free(first);
    cl_document_free(doc);
}

void cl_test_run_writer(void) {
    test_template_sequences_stay_text();
    test_numbers_keep_precision();
    test_simple_round_trip();
    test_postfix_round_trip_needs_defensive_parens();
    test_trim_marker_round_trip_is_stable();
    test_dynamic_index_round_trip();
    test_fixtures_round_trip();
}
