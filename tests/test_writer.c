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

void cl_test_run_writer(void) {
    test_simple_round_trip();
    test_postfix_round_trip_needs_defensive_parens();
    test_trim_marker_round_trip_is_stable();
    test_fixtures_round_trip();
}
