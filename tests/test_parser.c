#include "cl/cl.h"
#include "test_util.h"

/* name = expr => CL_ITEM_ATTRIBUTE; name ["label"...] { ... } => CL_ITEM_BLOCK.
 * See the "var = {}" vs "var {}" discussion this suite grew out of. */
static void test_attribute_vs_block(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "var = { name = \"Ada\" }\n"
        "svc \"web\" {\n"
        "  port = 8080\n"
        "}\n",
        "attribute_vs_block", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_body_t *root = cl_document_root(doc);

    cl_attribute_t *var_attr = cl_body_get_attribute(root, "var");
    CL_CHECK(var_attr != NULL);
    if (var_attr) {
        CL_CHECK(cl_expr_kind(var_attr->value) == CL_EXPR_OBJECT);
    }
    /* "var" never shows up as a block, since it was declared with "=". */
    CL_CHECK(cl_body_find_block(root, "var", NULL, 0) == NULL);

    const char *label = "web";
    cl_block_t *svc_block = cl_body_find_block(root, "svc", &label, 1);
    CL_CHECK(svc_block != NULL);
    /* "svc" never shows up as an attribute, since it was declared as a block. */
    CL_CHECK(cl_body_get_attribute(root, "svc") == NULL);

    cl_document_free(doc);
}

static void test_operator_precedence(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = 1 + 2 * 3\n", "precedence", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(doc), "x");
    CL_CHECK(attr != NULL);
    if (attr) {
        CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_BINARY);
        cl_binary_op_t op;
        cl_expr_binary_op(attr->value, &op);
        /* "*" binds tighter than "+", so the outermost node must be the "+". */
        CL_CHECK(op == CL_OP_ADD);
        cl_expr_t *right = cl_expr_binary_right(attr->value);
        CL_CHECK(cl_expr_kind(right) == CL_EXPR_BINARY);
        cl_expr_binary_op(right, &op);
        CL_CHECK(op == CL_OP_MUL);
    }

    cl_document_free(doc);
}

static void test_parse_error_reports_location(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = \n", "bad_expr", &err);
    CL_CHECK(doc == NULL);
    CL_CHECK(err.line == 2);
    CL_CHECK(err.message[0] != '\0');
}

static void test_splat_traversal_steps(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = a.b[*].c\n", "splat", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(doc), "x");
    CL_CHECK(attr != NULL);
    if (attr) {
        CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_TRAVERSAL);
        CL_CHECK_STREQ(cl_expr_traversal_root(attr->value), "a");
        CL_CHECK(cl_expr_traversal_step_count(attr->value) == 3);

        const cl_traversal_step_t *step0 = cl_expr_traversal_step_at(attr->value, 0);
        CL_CHECK(step0->kind == CL_STEP_ATTR);
        CL_CHECK_STREQ(step0->name, "b");

        const cl_traversal_step_t *step1 = cl_expr_traversal_step_at(attr->value, 1);
        CL_CHECK(step1->kind == CL_STEP_SPLAT_FULL);

        const cl_traversal_step_t *step2 = cl_expr_traversal_step_at(attr->value, 2);
        CL_CHECK(step2->kind == CL_STEP_ATTR);
        CL_CHECK_STREQ(step2->name, "c");
    }

    cl_document_free(doc);
}

static void test_function_call(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = upper(\"a\", \"b\")\n", "call", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(doc), "x");
    CL_CHECK(attr != NULL);
    if (attr) {
        CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_CALL);
        CL_CHECK_STREQ(cl_expr_call_name(attr->value), "upper");
        CL_CHECK(cl_expr_call_arg_count(attr->value) == 2);
        CL_CHECK(cl_expr_call_expand_final(attr->value) == 0);
    }

    cl_document_free(doc);
}

static void test_heredoc_without_interpolation_is_plain_string(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = <<EOF\nhello\nEOF\n", "heredoc", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(doc), "x");
    CL_CHECK(attr != NULL);
    if (attr) {
        CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_STRING);
        CL_CHECK_STREQ(cl_expr_as_string(attr->value), "hello\n");
    }

    cl_document_free(doc);
}

static void test_conditional_and_for_expression_kinds(void) {
    cl_error_t err;

    cl_document_t *cond_doc = cl_load_string("x = true ? 1 : 2\n", "conditional", &err);
    CL_CHECK(cond_doc != NULL);
    if (cond_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(cond_doc), "x");
        CL_CHECK(attr != NULL && cl_expr_kind(attr->value) == CL_EXPR_CONDITIONAL);
        cl_document_free(cond_doc);
    }

    cl_document_t *for_doc = cl_load_string("x = [for v in [1, 2, 3]: v * 2]\n", "for_expr", &err);
    CL_CHECK(for_doc != NULL);
    if (for_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(for_doc), "x");
        CL_CHECK(attr != NULL);
        if (attr) {
            CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_FOR);
            CL_CHECK(cl_expr_for_is_object(attr->value) == 0);
        }
        cl_document_free(for_doc);
    }
}

/* Postfix chaining (".attr"/"[idx]") on bases other than a bare identifier:
 * a call result, a parenthesized expression, an object literal, a tuple
 * literal, and a for-expression each produce a CL_EXPR_POSTFIX wrapping
 * that base, instead of being terminal values as before. */
static void test_postfix_chaining_on_non_identifier_bases(void) {
    cl_error_t err;

    cl_document_t *call_doc = cl_load_string("x = upper(name).len\n", "postfix_call", &err);
    CL_CHECK(call_doc != NULL);
    if (call_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(call_doc), "x");
        CL_CHECK(attr != NULL);
        if (attr) {
            CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_POSTFIX);
            cl_expr_t *base = cl_expr_postfix_base(attr->value);
            CL_CHECK(base != NULL && cl_expr_kind(base) == CL_EXPR_CALL);
            CL_CHECK(cl_expr_postfix_step_count(attr->value) == 1);
            const cl_traversal_step_t *step = cl_expr_postfix_step_at(attr->value, 0);
            CL_CHECK(step != NULL && step->kind == CL_STEP_ATTR);
            CL_CHECK_STREQ(step->name, "len");
        }
        cl_document_free(call_doc);
    }

    cl_document_t *paren_doc = cl_load_string("x = (a + b)[0]\n", "postfix_paren", &err);
    CL_CHECK(paren_doc != NULL);
    if (paren_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(paren_doc), "x");
        CL_CHECK(attr != NULL);
        if (attr) {
            CL_CHECK(cl_expr_kind(attr->value) == CL_EXPR_POSTFIX);
            cl_expr_t *base = cl_expr_postfix_base(attr->value);
            CL_CHECK(base != NULL && cl_expr_kind(base) == CL_EXPR_BINARY);
        }
        cl_document_free(paren_doc);
    }

    cl_document_t *object_doc = cl_load_string("x = {a = 1}.a\n", "postfix_object", &err);
    CL_CHECK(object_doc != NULL);
    if (object_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(object_doc), "x");
        CL_CHECK(attr != NULL && cl_expr_kind(attr->value) == CL_EXPR_POSTFIX);
        cl_document_free(object_doc);
    }

    cl_document_t *tuple_doc = cl_load_string("x = [1, 2, 3][0]\n", "postfix_tuple", &err);
    CL_CHECK(tuple_doc != NULL);
    if (tuple_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(tuple_doc), "x");
        CL_CHECK(attr != NULL && cl_expr_kind(attr->value) == CL_EXPR_POSTFIX);
        cl_document_free(tuple_doc);
    }

    cl_document_t *for_doc = cl_load_string("x = [for v in items: v][0]\n", "postfix_for", &err);
    CL_CHECK(for_doc != NULL);
    if (for_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(for_doc), "x");
        CL_CHECK(attr != NULL && cl_expr_kind(attr->value) == CL_EXPR_POSTFIX);
        cl_document_free(for_doc);
    }

    /* An identifier root still goes through CL_EXPR_TRAVERSAL, never
     * CL_EXPR_POSTFIX - postfix is only for non-identifier bases. */
    cl_document_t *ident_doc = cl_load_string("x = a.b[0]\n", "postfix_not_ident", &err);
    CL_CHECK(ident_doc != NULL);
    if (ident_doc) {
        cl_attribute_t *attr = cl_body_get_attribute(cl_document_root(ident_doc), "x");
        CL_CHECK(attr != NULL && cl_expr_kind(attr->value) == CL_EXPR_TRAVERSAL);
        cl_document_free(ident_doc);
    }
}

/* Inside "[...]", a lone number/string literal keeps its literal step kind
 * (so existing documents parse to the exact same AST); anything else becomes
 * a CL_STEP_INDEX_EXPR holding the parsed expression. */
static void test_index_step_kinds(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = a[0][\"k\"][i][n + 1][\"${k}\"]\ny = f(v)[i]\n", "index_kinds", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_attribute_t *x = cl_body_get_attribute(cl_document_root(doc), "x");
    CL_CHECK(x != NULL && cl_expr_kind(x->value) == CL_EXPR_TRAVERSAL);
    if (x && cl_expr_traversal_step_count(x->value) == 5) {
        const cl_traversal_step_t *s0 = cl_expr_traversal_step_at(x->value, 0);
        CL_CHECK(s0->kind == CL_STEP_INDEX_NUMBER && s0->index == 0 && s0->expr == NULL);

        const cl_traversal_step_t *s1 = cl_expr_traversal_step_at(x->value, 1);
        CL_CHECK(s1->kind == CL_STEP_INDEX_STRING);
        CL_CHECK_STREQ(s1->name, "k");

        const cl_traversal_step_t *s2 = cl_expr_traversal_step_at(x->value, 2);
        CL_CHECK(s2->kind == CL_STEP_INDEX_EXPR && cl_expr_kind(s2->expr) == CL_EXPR_TRAVERSAL);

        const cl_traversal_step_t *s3 = cl_expr_traversal_step_at(x->value, 3);
        CL_CHECK(s3->kind == CL_STEP_INDEX_EXPR && cl_expr_kind(s3->expr) == CL_EXPR_BINARY);

        const cl_traversal_step_t *s4 = cl_expr_traversal_step_at(x->value, 4);
        CL_CHECK(s4->kind == CL_STEP_INDEX_EXPR && cl_expr_kind(s4->expr) == CL_EXPR_TEMPLATE);
    } else {
        CL_CHECK(x && cl_expr_traversal_step_count(x->value) == 5);
    }

    cl_attribute_t *y = cl_body_get_attribute(cl_document_root(doc), "y");
    CL_CHECK(y != NULL && cl_expr_kind(y->value) == CL_EXPR_POSTFIX);
    if (y && cl_expr_postfix_step_count(y->value) == 1) {
        CL_CHECK(cl_expr_postfix_step_at(y->value, 0)->kind == CL_STEP_INDEX_EXPR);
    }

    cl_document_free(doc);

    cl_document_t *bad = cl_load_string("x = a[i\n", "index_unclosed", &err);
    CL_CHECK(bad == NULL);
    if (!bad) {
        CL_CHECK(strstr(err.message, "']'") != NULL);
    } else {
        cl_document_free(bad);
    }
}

void cl_test_run_parser(void) {
    test_attribute_vs_block();
    test_operator_precedence();
    test_parse_error_reports_location();
    test_splat_traversal_steps();
    test_function_call();
    test_postfix_chaining_on_non_identifier_bases();
    test_heredoc_without_interpolation_is_plain_string();
    test_conditional_and_for_expression_kinds();
    test_index_step_kinds();
}
