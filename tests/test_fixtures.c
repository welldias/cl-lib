#include "cl/cl.h"
#include "test_util.h"

#include <stdio.h>

#ifndef CL_FIXTURES_DIR
#define CL_FIXTURES_DIR "cl"
#endif

/* Expected outcome of cl_document_evaluate() for each shipped fixture,
 * verified by hand against the current contents of the fixtures below.
 * 01.cl, 02.cl and 07.cl are *supposed* to fail: they use undefined
 * top-level references (01/07: "var.*" with no "var" declared anywhere;
 * 02: a bareword "type = string", which this agnostic engine resolves as a
 * reference to a nonexistent identifier "string", not a type keyword). */
typedef struct {
    const char *file;
    int expect_eval_ok;
} fixture_case_t;

static const fixture_case_t kFixtures[] = {
    {"00.cl", 1}, {"01.cl", 0}, {"02.cl", 0}, {"03.cl", 1}, {"04.cl", 1},
    {"05.cl", 1}, {"06.cl", 1}, {"07.cl", 0}, {"08.cl", 1},
};

static void test_all_fixtures_parse(void) {
    for (size_t i = 0; i < sizeof(kFixtures) / sizeof(kFixtures[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", CL_FIXTURES_DIR, kFixtures[i].file);

        cl_error_t err;
        cl_document_t *doc = cl_load_file(path, &err);
        if (!doc) {
            fprintf(stderr, "%s:%d: FAIL: %s failed to parse: %s\n", __FILE__, __LINE__, path, err.message);
            cl_test_failures++;
            continue;
        }
        cl_document_free(doc);
    }
}

static void test_fixtures_evaluate_as_expected(void) {
    for (size_t i = 0; i < sizeof(kFixtures) / sizeof(kFixtures[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", CL_FIXTURES_DIR, kFixtures[i].file);

        cl_error_t err;
        cl_document_t *doc = cl_load_file(path, &err);
        if (!doc) {
            fprintf(stderr, "%s:%d: FAIL: %s failed to parse: %s\n", __FILE__, __LINE__, path, err.message);
            cl_test_failures++;
            continue;
        }

        cl_evaluated_t *result = cl_document_evaluate(doc, &err);
        int ok = result != NULL;
        if (ok != kFixtures[i].expect_eval_ok) {
            fprintf(stderr, "%s:%d: FAIL: %s evaluate() ok=%d, expected %d (err: %s)\n", __FILE__, __LINE__,
                    path, ok, kFixtures[i].expect_eval_ok, ok ? "(n/a)" : err.message);
            cl_test_failures++;
        }
        if (result) {
            cl_evaluated_free(result);
        }
        cl_document_free(doc);
    }
}

void cl_test_run_fixtures(void) {
    test_all_fixtures_parse();
    test_fixtures_evaluate_as_expected();
}
