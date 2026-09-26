#include "cl/cl.h"
#include "test_util.h"

#include <stdlib.h>

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
        {"zones[1.5]", "must be an integer"},
        {"zones[0.5 + 1]", "must be an integer"},
        {"zones[10]", "out of range"},
        {"zones[0 - 1]", "out of range"},
        {"zones[true]", "a number or a string"},
        {"zones[null]", "a number or a string"},
        {"sizes[zones[0]]", "key 'a' not found"},
        {"zones[p]", "is not an object"},
        {"sizes[0 + 0]", "is not a list"},
        {"zones[missing]", "reference 'missing' not found"},
        /* a dynamic index never doubles as a block label (only ".label"
         * steps do), so this can't reach plant "fern" */
        {"plant[p].sunlight", "reference 'plant' not found"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char source[512];
        snprintf(source, sizeof(source), "%sx = %s\n", prelude, cases[i].expr);
        check_eval_error(source, cases[i].fragment);
    }

    check_eval_error("zones = [1, 2]\na = zones[a]\nx = a\n", "circular");
}

/* Each case evaluates "x = <expr>" and compares cl_value_to_string(x). */
static void test_builtin_function_results(void) {
    static const struct {
        const char *expr;
        const char *expected;
    } cases[] = {
        /* strings */
        {"trim(\"??hi!?\", \"?!\")", "\"hi\""},
        {"trimspace(\"  \\t hi \\n\")", "\"hi\""},
        {"trimprefix(\"v1.2\", \"v\")", "\"1.2\""},
        {"trimprefix(\"1.2\", \"v\")", "\"1.2\""},
        {"trimsuffix(\"app.log\", \".log\")", "\"app\""},
        {"replace(\"a-b-c\", \"-\", \"::\")", "\"a::b::c\""},
        {"split(\",\", \"a,b,,c\")", "[\"a\", \"b\", \"\", \"c\"]"},
        {"split(\",\", \"\")", "[\"\"]"},
        {"join(\"-\", [1, true, \"z\"])", "\"1-true-z\""},
        {"join(\",\", [])", "\"\""},
        {"substr(\"hello world\", 0, 5)", "\"hello\""},
        {"substr(\"hello world\", -5, -1)", "\"world\""},
        {"substr(\"abc\", 1, 99)", "\"bc\""},
        {"startswith(\"prod-api\", \"prod\")", "true"},
        {"endswith(\"prod-api\", \"prod\")", "false"},
        {"strcontains(\"prod-api\", \"d-a\")", "true"},
        {"format(\"%s=%d (%.2f%%)\", \"x\", 42, 3.14159)", "\"x=42 (3.14%)\""},
        {"format(\"%f\", 1.5)", "\"1.500000\""},
        {"format(\"no verbs\")", "\"no verbs\""},
        {"upper(1.5)", "\"1.5\""},
        /* numbers */
        {"min(3, -1, 2)", "-1"},
        {"max([4, 9, 2]...)", "9"},
        {"abs(-2.5)", "2.5"},
        {"floor(-1.5)", "-2"},
        {"ceil(1.2)", "2"},
        {"round(2.5)", "3"},
        {"round(-2.5)", "-3"},
        {"pow(2, 10)", "1024"},
        {"parseint(\"ff\", 16)", "255"},
        {"parseint(\"-101\", 2)", "-5"},
        /* collections */
        {"keys({z = 1, a = 2})", "[\"z\", \"a\"]"},
        {"values({z = 1, a = 2})", "[1, 2]"},
        {"lookup({a = 1}, \"a\")", "1"},
        {"lookup({a = 1}, \"b\", \"def\")", "\"def\""},
        {"merge({a = 1, b = 2}, {b = 3, c = 4})", "{\n  a = 1\n  b = 3\n  c = 4\n}"},
        {"contains([1, [2], \"x\"], [2])", "true"},
        {"contains([1, 2], \"1\")", "false"},
        {"element([\"a\", \"b\", \"c\"], 4)", "\"b\""},
        {"slice([1, 2, 3, 4], 1, 3)", "[2, 3]"},
        {"slice([1, 2], 2, 2)", "[]"},
        {"reverse([1, 2, 3])", "[3, 2, 1]"},
        {"distinct([1, 2, 1, \"1\", 2])", "[1, 2, \"1\"]"},
        {"flatten([1, [2, [3, [4]]], []])", "[1, 2, 3, 4]"},
        {"range(3)", "[0, 1, 2]"},
        {"range(1, 4)", "[1, 2, 3]"},
        {"range(3, 0)", "[3, 2, 1]"},
        {"range(10, 0, -3)", "[10, 7, 4, 1]"},
        {"range(0, 1, 0.25)", "[0, 0.25, 0.5, 0.75]"},
        {"range(5, 1, 1)", "[]"},
        {"zipmap([\"a\", \"b\", \"a\"], [1, 2, 3])", "{\n  a = 3\n  b = 2\n}"},
        /* types and conversion */
        {"type(null)", "\"null\""},
        {"type({})", "\"object\""},
        {"type([])", "\"list\""},
        {"tostring(12)", "\"12\""},
        {"tostring(null)", "null"},
        {"tonumber(\"1.5e2\")", "150"},
        {"tonumber(7)", "7"},
        {"tobool(\"false\")", "false"},
        {"coalesce(null, null, \"x\", \"y\")", "\"x\""},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char source[256];
        snprintf(source, sizeof(source), "x = %s\n", cases[i].expr);
        cl_error_t err;
        cl_document_t *doc = cl_load_string(source, "builtin_results", &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            fprintf(stderr, "  parse error in: %s  %s\n", source, err.message);
            continue;
        }
        cl_evaluated_t *result = cl_document_evaluate(doc, &err);
        CL_CHECK(result != NULL);
        if (result) {
            cl_evaluated_attribute_t *x = cl_evaluated_body_get_attribute(cl_evaluated_root(result), "x");
            char *text = x ? cl_value_to_string(x->value) : NULL;
            CL_CHECK_STREQ(text, cases[i].expected);
            free(text);
            cl_evaluated_free(result);
        } else {
            fprintf(stderr, "  eval error in: %s  %s\n", source, err.message);
        }
        cl_document_free(doc);
    }
}

static void test_builtin_function_errors(void) {
    static const struct {
        const char *source;
        const char *fragment;
    } cases[] = {
        {"x = upper()\n", "upper() expects 1 argument, got 0"},
        {"x = lookup({})\n", "lookup() expects 2 to 3 arguments, got 1"},
        {"x = concat()\n", "concat() expects at least 1 argument, got 0"},
        {"x = upper([1])\n", "upper() argument 1 must be a string, got list"},
        {"x = length(1)\n", "length() argument 1 must be a string, list or object, got number"},
        {"x = join(\",\", [1, [2]])\n", "join() list element 2 must be a string, got list"},
        {"x = replace(\"a\", \"\", \"b\")\n", "replace() argument 2 must not be empty"},
        {"x = split(\"\", \"a\")\n", "split() argument 1 must not be empty"},
        {"x = substr(\"abc\", 4, 1)\n", "out of range"},
        {"x = substr(\"abc\", 1.5, 1)\n", "substr() argument 2 must be an integer"},
        {"x = format(\"%s %s\", 1)\n", "more verbs than arguments"},
        {"x = format(\"%s\", 1, 2)\n", "1 more argument(s) than verbs"},
        {"x = format(\"%x\", 1)\n", "does not support the verb '%x'"},
        {"x = format(\"%d\", 1.5)\n", "format() argument 2 must be an integer"},
        {"x = format(\"50%\")\n", "incomplete"},
        {"x = min(1, \"2\")\n", "min() argument 2 must be a number, got string"},
        {"x = pow(-8, 0.5)\n", "not a real number"},
        {"x = parseint(\"12z\", 10)\n", "cannot parse"},
        {"x = parseint(\"1\", 1)\n", "base must be between 2 and 36"},
        {"x = keys([1])\n", "keys() argument 1 must be an object, got list"},
        {"x = lookup({a = 1}, \"b\")\n", "lookup() key 'b' not found"},
        {"x = merge({}, 1)\n", "merge() argument 2 must be an object"},
        {"x = element([], 0)\n", "empty list"},
        {"x = element([1], -1)\n", "must not be negative"},
        {"x = slice([1, 2], 1, 3)\n", "out of bounds"},
        {"x = range(0, 1, 0)\n", "step must not be zero"},
        {"x = range(1e12)\n", "more than"},
        {"x = zipmap([\"a\"], [])\n", "1 keys but 0 values"},
        {"x = tonumber(\"12abc\")\n", "cannot convert \"12abc\""},
        {"x = tonumber(\" 1\")\n", "cannot convert"},
        {"x = tobool(\"yes\")\n", "cannot convert \"yes\" to a bool"},
        {"x = coalesce(null)\n", "only null"},
        {"x = nope(1)\n", "unknown function 'nope'"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        check_eval_error(cases[i].source, cases[i].fragment);
    }
}

/* Expressions inside "${...}"/"%{...}" are re-parsed from a substring, but
 * errors about them must still point at their real place in the file. */
static void test_template_errors_report_real_position(void) {
    static const struct {
        const char *source;
        int line;
        int col;
    } cases[] = {
        {"a = 1\nservice \"api\" {\n  name = \"api-${env}\"\n}\n", 3, 17},
        {"x = \"${1} and ${missing}\"\n", 1, 17},
        {"x = <<EOF\nline one\n  ${nope}\nEOF\n", 3, 5},
        {"x = \"${upper(\"a ${nope}\")}\"\n", 1, 19},
        {"x = <<EOF\n%{if 1}\nyes\n%{endif}\nEOF\n", 2, 6},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cl_error_t err;
        cl_document_t *doc = cl_load_string(cases[i].source, "template_position", &err);
        CL_CHECK(doc != NULL);
        if (!doc) {
            continue;
        }
        cl_evaluated_t *result = cl_document_evaluate(doc, &err);
        CL_CHECK(result == NULL);
        if (!result) {
            CL_CHECK(err.line == cases[i].line && err.col == cases[i].col);
            if (err.line != cases[i].line || err.col != cases[i].col) {
                fprintf(stderr, "  case %zu: got %d:%d, expected %d:%d\n", i, err.line, err.col, cases[i].line,
                        cases[i].col);
            }
        } else {
            cl_evaluated_free(result);
        }
        cl_document_free(doc);
    }

    /* parse errors inside a template use the same mapping */
    cl_error_t err;
    cl_document_t *bad = cl_load_string("x = \"${1 +}\"\n", "template_parse_position", &err);
    CL_CHECK(bad == NULL);
    if (!bad) {
        CL_CHECK(err.line == 1 && err.col == 11);
    } else {
        cl_document_free(bad);
    }
}

void cl_test_run_eval(void) {
    test_attribute_form_resolves_traversal();
    test_unlabeled_block_form_does_not_resolve_traversal();
    test_two_label_block_is_addressable();
    test_block_label_resolution_prefers_longest_match();
    test_calling_unregistered_function_is_eval_error();
    test_builtin_functions();
    test_builtin_function_results();
    test_builtin_function_errors();
    test_duplicate_top_level_attribute_first_one_wins();
    test_postfix_chaining_evaluates();
    test_circular_reference_is_eval_error();
    test_diamond_reference_is_not_circular();
    test_template_trim_markers();
    test_template_without_trim_markers_keeps_whitespace();
    test_dynamic_index_evaluates();
    test_dynamic_index_errors();
    test_template_errors_report_real_position();
}
