#include "cl/cl.h"
#include "test_util.h"

#include <stdlib.h>

static void test_get_attribute_returns_first_match_only(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = 1\nx = 2\n", "get_attribute", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(doc), "x");
    CL_CHECK(attr != NULL);
    if (attr) {
        double num;
        cl_expr_as_number(attr->value, &num);
        CL_CHECK(num == 1);
    }
    CL_CHECK(cl_body_get_attribute(cl_document_root(doc), "missing") == NULL);

    cl_document_free(doc);
}

/* Label count must match exactly: a 0-label lookup does not find a 1-label
 * block and vice versa, even when the type name matches. */
static void test_find_block_requires_exact_label_count(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "unlabeled {\n"
        "  a = 1\n"
        "}\n"
        "labeled \"x\" {\n"
        "  a = 2\n"
        "}\n",
        "label_count", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_body_t *root = cl_document_root(doc);

    CL_CHECK(cl_body_find_block(root, "unlabeled", NULL, 0) != NULL);
    const char *empty_label = "x";
    CL_CHECK(cl_body_find_block(root, "unlabeled", &empty_label, 1) == NULL);

    CL_CHECK(cl_body_find_block(root, "labeled", NULL, 0) == NULL);
    const char *label = "x";
    CL_CHECK(cl_body_find_block(root, "labeled", &label, 1) != NULL);
    const char *wrong_label = "y";
    CL_CHECK(cl_body_find_block(root, "labeled", &wrong_label, 1) == NULL);

    cl_document_free(doc);
}

/* Unlike attributes (where duplicates shadow each other), same-typed blocks
 * are all preserved and enumerable via cl_body_find_blocks. */
static void test_find_blocks_returns_every_match(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "srv \"a\" {\n"
        "  port = 1\n"
        "}\n"
        "srv \"b\" {\n"
        "  port = 2\n"
        "}\n"
        "other \"c\" {\n"
        "  port = 3\n"
        "}\n",
        "find_blocks", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_block_t **matches = NULL;
    size_t count = cl_body_find_blocks(cl_document_root(doc), "srv", &matches);
    CL_CHECK(count == 2);
    if (matches) {
        CL_CHECK_STREQ(matches[0]->labels[0], "a");
        CL_CHECK_STREQ(matches[1]->labels[0], "b");
        free(matches);
    }

    cl_document_free(doc);
}

void cl_test_run_navigate(void) {
    test_get_attribute_returns_first_match_only();
    test_find_block_requires_exact_label_count();
    test_find_blocks_returns_every_match();
}
