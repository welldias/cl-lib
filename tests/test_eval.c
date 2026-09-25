#include "cl/cl.h"
#include "test_util.h"

/* "var = { ... }" is an attribute holding an object: "var.field" resolves by
 * evaluating the attribute and reading a key off it. */
static void test_attribute_form_resolves_traversal(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("var = {\n  name = \"Ada\"\n}\nx = var.name\n", "attr_form", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
        CL_CHECK(x != NULL);
        if (x) {
            CL_CHECK(cl_value_kind(x->value) == CL_VAL_STRING);
            CL_CHECK_STREQ(cl_value_as_string(x->value), "Ada");
        }
        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

/* "var { ... }" is an unlabeled block: traversal's fallback path needs the
 * first step to match a block LABEL, and an unlabeled block never has one,
 * so "var.name" fails to resolve even though "var" and "name" both exist. */
static void test_unlabeled_block_form_does_not_resolve_traversal(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("var {\n  name = \"Ada\"\n}\nx = var.name\n", "block_form", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result == NULL);
    if (!result) {
        CL_CHECK(strstr(err.message, "var.name") != NULL);
    } else {
        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

/* A block declared with 2+ labels (e.g. Terraform's "resource "type" "name"")
 * is addressable by consuming that many leading traversal steps as labels,
 * not just one - see cl_eval_find_block_by_labels() in cl_eval.c. */
static void test_two_label_block_is_addressable(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "resource \"aws_instance\" \"app\" {\n"
        "  id = \"i-1\"\n"
        "}\n"
        "x = resource.aws_instance.app.id\n",
        "two_label", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
        CL_CHECK(x != NULL && cl_value_kind(x->value) == CL_VAL_STRING);
        if (x) {
            CL_CHECK_STREQ(cl_value_as_string(x->value), "i-1");
        }
        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

/* When a 1-label block and a 2-label block share the same type, the
 * traversal that supplies enough matching steps for the 2-label block
 * reaches it, while a shorter traversal still reaches the 1-label one -
 * the longest matching label sequence wins, it isn't just "first found". */
static void test_block_label_resolution_prefers_longest_match(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "svc \"a\" {\n"
        "  info = \"one-label\"\n"
        "}\n"
        "svc \"a\" \"b\" {\n"
        "  info = \"two-label\"\n"
        "}\n"
        "short_path = svc.a.info\n"
        "long_path  = svc.a.b.info\n",
        "label_disambiguation", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_body_t *root = cl_evaluated_root(result);

        cl_evaluated_attribute_t *shortp = cl_evaluated_body_get_attribute(root, "short_path");
        CL_CHECK(shortp != NULL);
        if (shortp) {
            CL_CHECK_STREQ(cl_value_as_string(shortp->value), "one-label");
        }

        cl_evaluated_attribute_t *longp = cl_evaluated_body_get_attribute(root, "long_path");
        CL_CHECK(longp != NULL);
        if (longp) {
            CL_CHECK_STREQ(cl_value_as_string(longp->value), "two-label");
        }

        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

static void test_calling_unregistered_function_is_eval_error(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = nope(1)\n", "unregistered_fn", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result == NULL);
    if (!result) {
        CL_CHECK(strstr(err.message, "nope") != NULL);
    } else {
        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

static void test_builtin_functions(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(
        "a = upper(\"ada\")\n"
        "b = lower(\"ADA\")\n"
        "c = length([1, 2, 3])\n"
        "d = length(\"abcd\")\n"
        "e = concat([1, 2], [3])\n"
        "f = concat(\"a\", 1, true)\n",
        "builtins", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_body_t *root = cl_evaluated_root(result);
        double num;

        cl_evaluated_attribute_t *a = cl_evaluated_body_get_attribute(root, "a");
        CL_CHECK(a != NULL && cl_value_kind(a->value) == CL_VAL_STRING);
        if (a) {
            CL_CHECK_STREQ(cl_value_as_string(a->value), "ADA");
        }

        cl_evaluated_attribute_t *b = cl_evaluated_body_get_attribute(root, "b");
        CL_CHECK(b != NULL && cl_value_kind(b->value) == CL_VAL_STRING);
        if (b) {
            CL_CHECK_STREQ(cl_value_as_string(b->value), "ada");
        }

        cl_evaluated_attribute_t *c = cl_evaluated_body_get_attribute(root, "c");
        CL_CHECK(c != NULL && cl_value_kind(c->value) == CL_VAL_NUMBER);
        if (c) {
            cl_value_as_number(c->value, &num);
            CL_CHECK(num == 3);
        }

        cl_evaluated_attribute_t *d = cl_evaluated_body_get_attribute(root, "d");
        CL_CHECK(d != NULL && cl_value_kind(d->value) == CL_VAL_NUMBER);
        if (d) {
            cl_value_as_number(d->value, &num);
            CL_CHECK(num == 4);
        }

        /* concat() of all-list arguments concatenates the lists. */
        cl_evaluated_attribute_t *e = cl_evaluated_body_get_attribute(root, "e");
        CL_CHECK(e != NULL && cl_value_kind(e->value) == CL_VAL_LIST);
        if (e) {
            CL_CHECK(cl_value_list_count(e->value) == 3);
        }

        /* concat() with any non-list argument stringifies everything instead. */
        cl_evaluated_attribute_t *f = cl_evaluated_body_get_attribute(root, "f");
        CL_CHECK(f != NULL && cl_value_kind(f->value) == CL_VAL_STRING);
        if (f) {
            CL_CHECK_STREQ(cl_value_as_string(f->value), "a1true");
        }

        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

/* cl_body_get_attribute returns the FIRST match; a shadowed duplicate
 * attribute is unreachable through traversal, not merged or overridden. */
static void test_duplicate_top_level_attribute_first_one_wins(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = 1\nx = 2\ny = x\n", "duplicate_attr", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_attribute_t *y = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "y");
        CL_CHECK(y != NULL);
        if (y) {
            double num;
            cl_value_as_number(y->value, &num);
            CL_CHECK(num == 1);
        }
        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

/* Postfix chaining on non-identifier bases (see tests/test_parser.c) must
 * also evaluate correctly, not just parse. */
static void test_postfix_chaining_evaluates(void) {
    cl_error_t err;

    cl_document_t *call_doc = cl_load_string("x = concat([1, 2], [3])[2]\n", "postfix_call_eval", &err);
    CL_CHECK(call_doc != NULL);
    if (call_doc) {
        cl_evaluated_t *result = cl_document_evaluate(call_doc, &err);
        CL_CHECK(result != NULL);
        if (result) {
            cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
            CL_CHECK(x != NULL && cl_value_kind(x->value) == CL_VAL_NUMBER);
            if (x) {
                double num;
                cl_value_as_number(x->value, &num);
                CL_CHECK(num == 3);
            }
            cl_evaluated_free(result);
        }
        cl_document_free(call_doc);
    }

    cl_document_t *object_doc = cl_load_string("x = {a = 1, b = 2}.b\n", "postfix_object_eval", &err);
    CL_CHECK(object_doc != NULL);
    if (object_doc) {
        cl_evaluated_t *result = cl_document_evaluate(object_doc, &err);
        CL_CHECK(result != NULL);
        if (result) {
            cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
            CL_CHECK(x != NULL && cl_value_kind(x->value) == CL_VAL_NUMBER);
            if (x) {
                double num;
                cl_value_as_number(x->value, &num);
                CL_CHECK(num == 2);
            }
            cl_evaluated_free(result);
        }
        cl_document_free(object_doc);
    }

    /* Chaining onto a value that doesn't support the step is a clean
     * evaluation error, not a crash - e.g. indexing a number. */
    cl_document_t *bad_doc = cl_load_string("x = (1 + 2)[0]\n", "postfix_type_error", &err);
    CL_CHECK(bad_doc != NULL);
    if (bad_doc) {
        cl_evaluated_t *result = cl_document_evaluate(bad_doc, &err);
        CL_CHECK(result == NULL);
        if (result) {
            cl_evaluated_free(result);
        }
        cl_document_free(bad_doc);
    }
}

/* "b = a" then "a = b" (or a single "a = a") must fail cleanly instead of
 * recursing until the process crashes. */
static void test_circular_reference_is_eval_error(void) {
    cl_error_t err;

    cl_document_t *mutual_doc = cl_load_string("a = b\nb = a\nx = a\n", "circular_mutual", &err);
    CL_CHECK(mutual_doc != NULL);
    if (mutual_doc) {
        cl_evaluated_t *result = cl_document_evaluate(mutual_doc, &err);
        CL_CHECK(result == NULL);
        if (!result) {
            CL_CHECK(strstr(err.message, "circular") != NULL);
        } else {
            cl_evaluated_free(result);
        }
        cl_document_free(mutual_doc);
    }

    cl_document_t *self_doc = cl_load_string("a = a\nx = a\n", "circular_self", &err);
    CL_CHECK(self_doc != NULL);
    if (self_doc) {
        cl_evaluated_t *result = cl_document_evaluate(self_doc, &err);
        CL_CHECK(result == NULL);
        if (result) {
            cl_evaluated_free(result);
        }
        cl_document_free(self_doc);
    }
}

/* A diamond dependency (two different attributes both referencing a third)
 * is not a cycle and must still evaluate normally. */
static void test_diamond_reference_is_not_circular(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("a = 1\nb = a\nc = a\nx = b + c\n", "diamond", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
        CL_CHECK(x != NULL);
        if (x) {
            double num;
            cl_value_as_number(x->value, &num);
            CL_CHECK(num == 2);
        }
        cl_evaluated_free(result);
    }

    cl_document_free(doc);
}

/* Trim markers ("${~ expr ~}", "%{~if cond~}", ...) strip adjacent
 * whitespace from the surrounding literal text - left-of-marker trims
 * backwards into the already-accumulated literal, right-of-marker trims
 * forwards into whatever literal text follows, and it reaches through
 * multiple blank lines, not just up to the first newline. */
static void test_template_trim_markers(void) {
    cl_error_t err;

    struct {
        const char *label;
        const char *src;
        const char *expected;
    } cases[] = {
        {"left_trim", "x = \"A   ${~name}\"\nname = \"B\"\n", "AB"},
        {"right_trim", "x = \"${name~}   B\"\nname = \"A\"\n", "AB"},
        {"both_trim", "x = \"A   ${~name~}   B\"\nname = \"X\"\n", "AXB"},
        {"if_open_trim", "x = <<EOF\nA%{if true~}\n   B%{endif}\nEOF\n", "AB\n"},
        {"endif_close_trim", "x = <<EOF\nA%{if true}   \n%{~endif}B\nEOF\n", "AB\n"},
        {"else_both_trim", "x = <<EOF\n%{if false}skip%{~else~}   YES   %{~endif}\nEOF\n", "YES\n"},
        {"for_trim", "x = <<EOF\n%{for v in [1, 2, 3]~}\n${v}\n%{~endfor}\nEOF\n", "123\n"},
        {"multi_newline_trim", "x = <<EOF\nA%{if true~}\n\n   \n   B%{endif}\nEOF\n", "AB\n"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cl_document_t *doc = cl_load_string(cases[i].src, cases[i].label, &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            continue;
        }
        cl_evaluated_t *result = cl_document_evaluate(doc, &err);
        CL_CHECK(result != NULL);
        if (result) {
            cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
            CL_CHECK(x != NULL);
            if (x) {
                CL_CHECK_STREQ(cl_value_as_string(x->value), cases[i].expected);
            }
            cl_evaluated_free(result);
        }
        cl_document_free(doc);
    }
}

/* No trim marker: whitespace around an interpolation is left untouched. */
static void test_template_without_trim_markers_keeps_whitespace(void) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string("x = \"A   ${name}   B\"\nname = \"X\"\n", "no_trim", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }
    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (result) {
        cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
        CL_CHECK(x != NULL);
        if (x) {
            CL_CHECK_STREQ(cl_value_as_string(x->value), "A   X   B");
        }
        cl_evaluated_free(result);
    }
    cl_document_free(doc);
}

/* Evaluates `source` and checks that its attribute "x" rendered as a string
 * (numbers via cl_value_as_number) equals `expected`. */
static void check_eval_x(const char *source, const char *expected) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "check_eval_x", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        fprintf(stderr, "  source: %s  parse error: %s\n", source, err.message);
        return;
    }
    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result != NULL);
    if (!result) {
        fprintf(stderr, "  source: %s  eval error: %s\n", source, err.message);
        cl_document_free(doc);
        return;
    }
    cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
    CL_CHECK(x != NULL);
    if (x) {
        char actual[64] = "";
        double num;
        if (cl_value_as_number(x->value, &num) == 0) {
            snprintf(actual, sizeof(actual), "%g", num);
        } else if (cl_value_as_string(x->value)) {
            snprintf(actual, sizeof(actual), "%s", cl_value_as_string(x->value));
        }
        CL_CHECK_STREQ(actual, expected);
    }
    cl_evaluated_free(result);
    cl_document_free(doc);
}

/* Evaluates `source` and checks that it fails with a message containing
 * `fragment`. */
static void check_eval_error(const char *source, const char *fragment) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "check_eval_error", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return;
    }
    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    CL_CHECK(result == NULL);
    if (!result) {
        CL_CHECK(strstr(err.message, fragment) != NULL);
        if (!strstr(err.message, fragment)) {
            fprintf(stderr, "  source: %s  got: %s\n", source, err.message);
        }
    } else {
        cl_evaluated_free(result);
    }
    cl_document_free(doc);
}

/* "[expr]" evaluates its expression in the caller's scope and indexes by
 * the resulting value's kind: number -> list index, string -> object key. */
static void test_dynamic_index_evaluates(void) {
    static const char *prelude =
        "zones = [\"seedling\", \"bloom\", \"harvest\"]\n"
        "sizes = { small = 1, big = 9 }\n"
        "modes = { day = { humidity = 55 }, night = { humidity = 70 } }\n"
        "grid = [[1, 2, 3], [4, 5, 6]]\n"
        "current = 1\n"
        "kind = \"big\"\n"
        "is_open = true\n";
    static const struct {
        const char *expr;
        const char *expected;
    } cases[] = {
        {"zones[current]", "bloom"},
        {"sizes[kind]", "9"},
        {"modes[is_open ? \"day\" : \"night\"].humidity", "55"},
        {"zones[length(zones) - 1]", "harvest"},
        {"grid[current][current + 1]", "6"},
        {"sizes[\"${kind}\"]", "9"},
        {"{ small = 1, big = 2 }[kind]", "2"},
        {"[for i, z in zones : zones[(i + 1) % length(zones)]][2]", "seedling"},
        {"[for z in [\"big\", \"small\"] : sizes[z]][1]", "1"},
        {"[for i, row in grid : row[*]][current][0]", "4"},
        {"([{ v = [7, 8] }, { v = [9, 10] }][*].v[current])[1]", "10"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char source[512];
        snprintf(source, sizeof(source), "%sx = %s\n", prelude, cases[i].expr);
        check_eval_x(source, cases[i].expected);
    }
}

static void test_dynamic_index_errors(void) {
    static const char *prelude =
        "zones = [\"a\", \"b\", \"c\"]\n"
        "sizes = { big = 9 }\n"
        "p = \"fern\"\n"
        "plant \"fern\" {\n  sunlight = \"indirect\"\n}\n";
    static const struct {
        const char *expr;
        const char *fragment;
    } cases[] = {
        {"zones[1.5]", "deve ser inteiro"},
        {"zones[0.5 + 1]", "deve ser inteiro"},
        {"zones[10]", "fora dos limites"},
        {"zones[0 - 1]", "fora dos limites"},
        {"zones[true]", "numero ou string"},
        {"zones[null]", "numero ou string"},
        {"sizes[zones[0]]", "chave 'a' nao encontrada"},
        {"zones[p]", "nao e um objeto"},
        {"sizes[0 + 0]", "nao e uma lista"},
        {"zones[missing]", "referencia 'missing' nao encontrada"},
        /* a dynamic index never doubles as a block label (only ".label"
         * steps do), so this can't reach plant "fern" */
        {"plant[p].sunlight", "referencia 'plant' nao encontrada"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char source[512];
        snprintf(source, sizeof(source), "%sx = %s\n", prelude, cases[i].expr);
        check_eval_error(source, cases[i].fragment);
    }

    check_eval_error("zones = [1, 2]\na = zones[a]\nx = a\n", "circular");
}

void cl_test_run_eval(void) {
    test_attribute_form_resolves_traversal();
    test_unlabeled_block_form_does_not_resolve_traversal();
    test_two_label_block_is_addressable();
    test_block_label_resolution_prefers_longest_match();
    test_calling_unregistered_function_is_eval_error();
    test_builtin_functions();
    test_duplicate_top_level_attribute_first_one_wins();
    test_postfix_chaining_evaluates();
    test_circular_reference_is_eval_error();
    test_diamond_reference_is_not_circular();
    test_template_trim_markers();
    test_template_without_trim_markers_keeps_whitespace();
    test_dynamic_index_evaluates();
    test_dynamic_index_errors();
}
