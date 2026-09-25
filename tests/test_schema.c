#include "cl/cl.h"
#include "test_util.h"

#include <stdlib.h>

/* The "machine" rules from cl/schema/machine.schema.cl, built through the C
 * API instead of the schema file. */
static cl_schema_t *machine_schema(int strict) {
    cl_schema_t *schema = cl_schema_new();
    cl_schema_set_strict(schema, strict);
    cl_schema_block_t *machine = cl_schema_add_block(schema, "machine");
    cl_schema_block_add_attr(machine, "cpu", CL_TYPE_NUMBER, 1);
    cl_schema_block_add_attr(machine, "memory", CL_TYPE_STRING, 1);
    cl_schema_block_add_attr(machine, "tags", CL_TYPE_LIST, 0);
    cl_schema_block_t *disk = cl_schema_block_add_block(machine, "disk");
    cl_schema_block_add_attr(disk, "size", CL_TYPE_NUMBER, 1);
    static const char *kinds[] = {"ssd", "hdd", "nvme"};
    cl_schema_block_add_enum(disk, "kind", kinds, 3, 0);
    return schema;
}

/* Validates `source` against `schema`, structurally and - when it
 * evaluates - again with its evaluated result. Expects success when
 * `fragment` is NULL; otherwise expects the first failing pass to report a
 * message containing `fragment` at line:col. `with_result` selects whether
 * the failure is expected only once the result is available. */
static void check_schema(const cl_schema_t *schema, const char *source, const cl_bindings_t *bindings,
                         int with_result, const char *fragment, int line, int col) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "schema_doc", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        fprintf(stderr, "  parse error: %s\n  source: %s\n", err.message, source);
        return;
    }

    int structural = cl_schema_validate(schema, doc, NULL, &err);
    int expect_structural_failure = fragment && !with_result;
    CL_CHECK(structural == (expect_structural_failure ? -1 : 0));

    int final = structural;
    if (structural == 0) {
        cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, &err);
        CL_CHECK(result != NULL);
        if (result) {
            final = cl_schema_validate(schema, doc, result, &err);
            cl_evaluated_free(result);
        }
    }

    if (fragment) {
        CL_CHECK(final == -1);
        CL_CHECK(strstr(err.message, fragment) != NULL);
        CL_CHECK(err.line == line && err.col == col);
        if (final != -1 || !strstr(err.message, fragment) || err.line != line || err.col != col) {
            fprintf(stderr, "  source: %s  got %d:%d '%s'\n", source, err.line, err.col,
                    final == -1 ? err.message : "(valid)");
        }
    } else {
        CL_CHECK(final == 0);
        if (final != 0) {
            fprintf(stderr, "  source: %s  got %d:%d '%s'\n", source, err.line, err.col, err.message);
        }
    }
    cl_document_free(doc);
}

static void test_valid_documents(void) {
    cl_schema_t *schema = machine_schema(0);
    check_schema(schema,
                 "base = 2\n"
                 "machine \"web\" {\n"
                 "  cpu    = base * 2\n"
                 "  memory = \"8GB\"\n"
                 "  tags   = [\"a\"]\n"
                 "  disk {\n    size = 100\n  }\n"
                 "  disk {\n    size = 200\n    kind = \"hdd\"\n  }\n"
                 "}\n",
                 NULL, 0, NULL, 0, 0);
    /* labels are not checked: any number of them matches the same rule */
    check_schema(schema, "machine {\n  cpu = 1\n  memory = \"x\"\n}\n", NULL, 0, NULL, 0, 0);
    check_schema(schema, "machine \"a\" \"b\" {\n  cpu = 1\n  memory = \"x\"\n}\n", NULL, 0, NULL, 0, 0);
    /* top-level attributes and unregistered blocks are free when not strict */
    check_schema(schema, "anything = 1\nserver \"x\" {\n  whatever = true\n}\n", NULL, 0, NULL, 0, 0);
    cl_schema_free(schema);
}

static void test_structural_errors(void) {
    cl_schema_t *schema = machine_schema(0);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  gpu = 1\n}\n", NULL, 0,
                 "atributo 'gpu' nao permitido em bloco 'machine'", 4, 3);
    check_schema(schema, "x = 1\n\nmachine \"a\" {\n  cpu = 1\n}\n", NULL, 0,
                 "atributo obrigatorio 'memory' ausente em bloco 'machine'", 3, 1);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  cpu = 2\n  memory = \"x\"\n}\n", NULL, 0,
                 "atributo 'cpu' duplicado em bloco 'machine'", 3, 3);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  nic {\n  }\n}\n", NULL, 0,
                 "bloco 'nic' nao permitido dentro de 'machine'", 4, 3);
    /* sub-blocks are validated recursively with their own rules */
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    kind = \"ssd\"\n  }\n}\n", NULL,
                 0, "atributo obrigatorio 'size' ausente em bloco 'disk'", 4, 3);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    size = 1\n    iops = 3\n  }\n}\n",
                 NULL, 0, "atributo 'iops' nao permitido em bloco 'disk'", 6, 5);
    /* a registered sub-block type is only allowed where it was declared */
    check_schema(schema, "disk {\n  size = 1\n}\n", NULL, 0, NULL, 0, 0);
    cl_schema_free(schema);
}

static void test_type_errors(void) {
    cl_schema_t *schema = machine_schema(0);
    /* literals are checked without evaluating */
    check_schema(schema, "machine \"a\" {\n  cpu = \"4\"\n  memory = \"x\"\n}\n", NULL, 0,
                 "atributo 'cpu' em bloco 'machine' deve ser number, mas e string", 2, 3);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  tags = {a = 1}\n}\n", NULL, 0,
                 "deve ser list, mas e object", 4, 3);
    check_schema(schema, "machine \"a\" {\n  cpu = null\n  memory = \"x\"\n}\n", NULL, 0, "deve ser number, mas e null",
                 2, 3);
    /* expressions only once the evaluated result is available */
    check_schema(schema, "n = \"4\"\nmachine \"a\" {\n  cpu = n\n  memory = \"x\"\n}\n", NULL, 1,
                 "deve ser number, mas e string", 3, 3);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"${1 + 1}GB\"\n  tags = concat(\"a\", \"b\")\n}\n",
                 NULL, 1, "atributo 'tags' em bloco 'machine' deve ser list, mas e string", 4, 3);
    check_schema(schema, "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    size = true ? \"big\" : 1\n  }\n}\n",
                 NULL, 1, "atributo 'size' em bloco 'disk' deve ser number, mas e string", 5, 5);

    /* values injected by the host are checked the same way */
    cl_bindings_t *bad = cl_bindings_new();
    cl_bindings_set_string(bad, "c", "four");
    check_schema(schema, "machine \"a\" {\n  cpu = c\n  memory = \"x\"\n}\n", bad, 1, "deve ser number, mas e string", 2,
                 3);
    cl_bindings_free(bad);
    cl_bindings_t *good = cl_bindings_new();
    cl_bindings_set_number(good, "c", 4);
    check_schema(schema, "machine \"a\" {\n  cpu = c\n  memory = \"x\"\n}\n", good, 0, NULL, 0, 0);
    cl_bindings_free(good);
    cl_schema_free(schema);

    /* "any" accepts every kind, null included */
    cl_schema_t *any_schema = cl_schema_new();
    cl_schema_block_add_attr(cl_schema_add_block(any_schema, "box"), "v", CL_TYPE_ANY, 1);
    static const char *any_values[] = {"null", "1", "\"s\"", "true", "[1]", "{a = 1}", "upper(\"x\")"};
    for (size_t i = 0; i < sizeof(any_values) / sizeof(any_values[0]); i++) {
        char source[64];
        snprintf(source, sizeof(source), "box {\n  v = %s\n}\n", any_values[i]);
        check_schema(any_schema, source, NULL, 0, NULL, 0, 0);
    }
    cl_schema_free(any_schema);
}

static void test_strict_mode(void) {
    cl_schema_t *schema = machine_schema(1);
    check_schema(schema, "x = 1\nmachine \"a\" {\n  cpu = 1\n  memory = \"x\"\n}\n", NULL, 0, NULL, 0, 0);
    check_schema(schema, "x = 1\nserver \"x\" {\n  a = 1\n}\n", NULL, 0, "tipo de bloco 'server' nao registrado no schema",
                 2, 1);
    cl_schema_free(schema);
}

static void test_schema_api(void) {
    cl_schema_t *schema = cl_schema_new();
    cl_schema_block_t *first = cl_schema_add_block(schema, "b0");
    CL_CHECK(first != NULL);
    CL_CHECK(cl_schema_add_block(schema, "b0") == NULL);
    CL_CHECK(cl_schema_add_block(NULL, "x") == NULL);
    CL_CHECK(cl_schema_block_add_block(first, "inner") != NULL);
    CL_CHECK(cl_schema_block_add_block(first, "inner") == NULL);
    CL_CHECK(cl_schema_block_add_attr(first, "a", CL_TYPE_NUMBER, 0) == 0);
    CL_CHECK(cl_schema_block_add_attr(first, "a", CL_TYPE_STRING, 0) == -1);
    CL_CHECK(cl_schema_block_add_attr(NULL, "a", CL_TYPE_STRING, 0) == -1);

    /* rule pointers stay valid while the arrays holding them grow */
    for (int i = 1; i < 100; i++) {
        char name[16];
        snprintf(name, sizeof(name), "b%d", i);
        CL_CHECK(cl_schema_add_block(schema, name) != NULL);
        snprintf(name, sizeof(name), "a%d", i);
        CL_CHECK(cl_schema_block_add_attr(first, name, CL_TYPE_ANY, 0) == 0);
    }
    CL_CHECK(cl_schema_block_add_attr(first, "late", CL_TYPE_BOOL, 1) == 0);
    check_schema(schema, "b0 {\n  a = 1\n  a99 = null\n}\n", NULL, 0, "atributo obrigatorio 'late' ausente em bloco 'b0'",
                 1, 1);
    check_schema(schema, "b0 {\n  a = 1\n  late = false\n  inner {\n  }\n}\nb42 {\n}\n", NULL, 0, NULL, 0, 0);

    cl_error_t err;
    CL_CHECK(cl_schema_validate(NULL, NULL, NULL, &err) == -1);
    cl_schema_free(schema);
    cl_schema_free(NULL);
}

/* The schema file must produce exactly the same decisions as the rules
 * built in C by machine_schema(). */
static void test_schema_file_matches_c_api(void) {
    cl_error_t err;
    cl_schema_t *from_file = cl_schema_load_file(CL_FIXTURES_DIR "/schema/machine.schema.cl", &err);
    CL_CHECK(from_file != NULL);
    if (!from_file) {
        fprintf(stderr, "  schema load error %d:%d: %s\n", err.line, err.col, err.message);
        return;
    }

    cl_document_t *doc = cl_load_file(CL_FIXTURES_DIR "/schema/machine.cl", &err);
    CL_CHECK(doc != NULL);
    if (doc) {
        cl_evaluated_t *result = cl_document_evaluate(doc, &err);
        CL_CHECK(result != NULL);
        CL_CHECK(cl_schema_validate(from_file, doc, NULL, &err) == 0);
        CL_CHECK(cl_schema_validate(from_file, doc, result, &err) == 0);
        cl_evaluated_free(result);
        cl_document_free(doc);
    }

    cl_schema_t *from_c = machine_schema(1);
    static const char *sources[] = {
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n}\n",
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  gpu = 1\n}\n",
        "machine \"a\" {\n  cpu = 1\n}\n",
        "machine \"a\" {\n  cpu = \"1\"\n  memory = \"x\"\n}\n",
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  tags = 1\n}\n",
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n  }\n}\n",
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    size = 1\n    kind = 2\n  }\n}\n",
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    size = 1\n    kind = \"hdd\"\n  }\n}\n",
        "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    size = 1\n    kind = \"tape\"\n  }\n}\n",
        "server \"x\" {\n}\n",
    };
    for (size_t i = 0; i < sizeof(sources) / sizeof(sources[0]); i++) {
        cl_document_t *d = cl_load_string(sources[i], "compare", &err);
        CL_CHECK(d != NULL);
        if (!d) {
            continue;
        }
        cl_error_t file_err = {0};
        cl_error_t c_err = {0};
        int file_rc = cl_schema_validate(from_file, d, NULL, &file_err);
        int c_rc = cl_schema_validate(from_c, d, NULL, &c_err);
        CL_CHECK(file_rc == c_rc);
        CL_CHECK_STREQ(file_err.message, c_err.message);
        cl_document_free(d);
    }
    cl_schema_free(from_c);
    cl_schema_free(from_file);
}

static void check_schema_load_error(const char *source, const char *fragment, int line, int col) {
    cl_error_t err;
    cl_schema_t *schema = cl_schema_load_string(source, "bad_schema", &err);
    CL_CHECK(schema == NULL);
    if (schema) {
        cl_schema_free(schema);
        return;
    }
    CL_CHECK(strstr(err.message, fragment) != NULL);
    CL_CHECK(err.line == line && err.col == col);
    if (!strstr(err.message, fragment) || err.line != line || err.col != col) {
        fprintf(stderr, "  source: %s  got %d:%d '%s'\n", source, err.line, err.col, err.message);
    }
}

static void test_schema_file_errors(void) {
    check_schema_load_error("block \"m\" {\n  cpu = { type = \"number\", requred = true }\n}\n",
                            "chave 'requred' desconhecida na regra do atributo 'cpu'", 2, 38);
    check_schema_load_error("block \"m\" {\n  cpu = \"integer\"\n}\n", "tipo 'integer' invalido", 2, 9);
    check_schema_load_error("block \"m\" {\n  cpu = { type = 1 }\n}\n", "tipo deve ser uma string literal", 2, 18);
    check_schema_load_error("block \"m\" {\n  cpu = { required = \"yes\" }\n}\n", "'required' deve ser true ou false", 2,
                            22);
    check_schema_load_error("block \"m\" {\n  cpu = number\n}\n", "regra do atributo 'cpu' deve ser", 2, 3);
    check_schema_load_error("block \"m\" {\n  cpu = \"${t}\"\n}\n", "regra do atributo 'cpu' deve ser", 2, 3);
    check_schema_load_error("block {\n}\n", "precisa de exatamente 1 rotulo", 1, 1);
    check_schema_load_error("block \"a\" \"b\" {\n}\n", "precisa de exatamente 1 rotulo", 1, 1);
    check_schema_load_error("block \"m\" {\n}\nblock \"m\" {\n}\n", "tipo de bloco 'm' duplicado no schema", 3, 1);
    check_schema_load_error("block \"m\" {\n  block \"d\" {\n  }\n  block \"d\" {\n  }\n}\n",
                            "tipo de bloco 'd' duplicado no schema", 4, 3);
    check_schema_load_error("block \"m\" {\n  a = \"any\"\n  a = \"list\"\n}\n", "atributo 'a' duplicado no schema", 3, 3);
    check_schema_load_error("version = 1\n", "item de topo 'version' desconhecido no schema", 1, 1);
    check_schema_load_error("strict = \"yes\"\n", "'strict' deve ser true ou false", 1, 1);
    check_schema_load_error("rule \"m\" {\n}\n", "esperado bloco 'block' no schema, encontrado 'rule'", 1, 1);
    check_schema_load_error("block \"m\" {\n  rule \"d\" {\n  }\n}\n", "esperado bloco 'block'", 2, 3);
    check_schema_load_error("block \"m\" {\n", "esperado '}'", 2, 1);

    check_schema_load_error("block \"m\" {\n  k = []\n}\n", "lista de valores do atributo 'k' nao pode ser vazia", 2, 7);
    check_schema_load_error("block \"m\" {\n  k = [\"a\", 1]\n}\n", "valores do atributo 'k' devem ser strings literais", 2,
                            13);
    check_schema_load_error("block \"m\" {\n  k = [\"a\", \"${x}\"]\n}\n", "devem ser strings literais", 2, 13);
    check_schema_load_error("block \"m\" {\n  k = [\"a\", \"b\", \"a\"]\n}\n", "valor 'a' repetido no atributo 'k'", 2,
                            18);
    check_schema_load_error("block \"m\" {\n  k = { values = \"a\" }\n}\n", "'values' deve ser uma lista de strings", 2,
                            18);
    check_schema_load_error("block \"m\" {\n  k = { type = \"number\", values = [\"a\"] }\n}\n",
                            "'values' so pode ser usado com type = \"string\"", 2, 16);
    check_schema_load_error("block \"m\" {\n  k = true\n}\n", "uma lista de valores", 2, 3);

    cl_error_t err;
    CL_CHECK(cl_schema_load_file("/nonexistent/schema.cl", &err) == NULL);

    /* full form: explicit string type, required, values */
    cl_schema_t *full = cl_schema_load_string(
        "block \"fan\" {\n  speed = { type = \"string\", required = true, values = [\"low\", \"high\"] }\n}\n", "full",
        &err);
    CL_CHECK(full != NULL);
    if (full) {
        check_schema(full, "fan {\n}\n", NULL, 0, "atributo obrigatorio 'speed' ausente", 1, 1);
        check_schema(full, "fan {\n  speed = \"mid\"\n}\n", NULL, 0, "deve ser um de: low, high; mas e 'mid'", 2, 3);
        check_schema(full, "fan {\n  speed = \"low\"\n}\n", NULL, 0, NULL, 0, 0);
        cl_schema_free(full);
    }

    /* "strict" is only special at the top level: inside a block it's an
     * ordinary attribute rule */
    cl_schema_t *schema = cl_schema_load_string("strict = false\nblock \"m\" {\n  strict = \"bool\"\n}\n", "ok", &err);
    CL_CHECK(schema != NULL);
    if (schema) {
        check_schema(schema, "m {\n  strict = true\n}\nother {\n}\n", NULL, 0, NULL, 0, 0);
        cl_schema_free(schema);
    }
}

/* An enum is a string attribute restricted to a fixed set of values. */
static void test_enum_values(void) {
    cl_schema_t *schema = machine_schema(0);
    static const char *prefix = "machine \"a\" {\n  cpu = 1\n  memory = \"x\"\n  disk {\n    size = 1\n";
    char source[256];

    snprintf(source, sizeof(source), "%s    kind = \"nvme\"\n  }\n}\n", prefix);
    check_schema(schema, source, NULL, 0, NULL, 0, 0);

    /* a literal outside the set is caught without evaluating */
    snprintf(source, sizeof(source), "%s    kind = \"sata\"\n  }\n}\n", prefix);
    check_schema(schema, source, NULL, 0, "atributo 'kind' em bloco 'disk' deve ser um de: ssd, hdd, nvme; mas e 'sata'",
                 6, 5);

    /* matching is exact: no case folding */
    snprintf(source, sizeof(source), "%s    kind = \"SSD\"\n  }\n}\n", prefix);
    check_schema(schema, source, NULL, 0, "mas e 'SSD'", 6, 5);

    /* a non-string is a type error, reported as such */
    snprintf(source, sizeof(source), "%s    kind = 2\n  }\n}\n", prefix);
    check_schema(schema, source, NULL, 0, "deve ser string, mas e number", 6, 5);

    /* expressions (and bindings) are checked once the result is available */
    snprintf(source, sizeof(source), "%s    kind = lower(\"HDD\")\n  }\n}\n", prefix);
    check_schema(schema, source, NULL, 0, NULL, 0, 0);
    snprintf(source, sizeof(source), "%s    kind = \"${k}d\"\n  }\n}\n", prefix);
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_string(b, "k", "ss");
    check_schema(schema, source, b, 0, NULL, 0, 0);
    cl_bindings_set_string(b, "k", "fl");
    check_schema(schema, source, b, 1, "deve ser um de: ssd, hdd, nvme; mas e 'fld'", 6, 5);
    cl_bindings_free(b);
    cl_schema_free(schema);

    /* required enum */
    cl_schema_t *req = cl_schema_new();
    static const char *levels[] = {"low", "high"};
    CL_CHECK(cl_schema_block_add_enum(cl_schema_add_block(req, "fan"), "speed", levels, 2, 1) == 0);
    check_schema(req, "fan {\n}\n", NULL, 0, "atributo obrigatorio 'speed' ausente em bloco 'fan'", 1, 1);
    check_schema(req, "fan {\n  speed = \"high\"\n}\n", NULL, 0, NULL, 0, 0);
    cl_schema_free(req);

    /* API rules */
    cl_schema_t *api = cl_schema_new();
    cl_schema_block_t *blk = cl_schema_add_block(api, "b");
    static const char *repeated[] = {"a", "b", "a"};
    static const char *with_null[] = {"a", NULL};
    static const char *ok[] = {"a"};
    CL_CHECK(cl_schema_block_add_enum(blk, "e", repeated, 3, 0) == -1);
    CL_CHECK(cl_schema_block_add_enum(blk, "e", with_null, 2, 0) == -1);
    CL_CHECK(cl_schema_block_add_enum(blk, "e", ok, 0, 0) == -1);
    CL_CHECK(cl_schema_block_add_enum(blk, "e", NULL, 1, 0) == -1);
    CL_CHECK(cl_schema_block_add_enum(NULL, "e", ok, 1, 0) == -1);
    CL_CHECK(cl_schema_block_add_enum(blk, "e", ok, 1, 0) == 0);
    CL_CHECK(cl_schema_block_add_enum(blk, "e", ok, 1, 0) == -1);
    CL_CHECK(cl_schema_block_add_attr(blk, "e", CL_TYPE_ANY, 0) == -1);

    /* a long list is truncated in the message, never overflowing it */
    const char *many[40];
    char names[40][8];
    for (int i = 0; i < 40; i++) {
        snprintf(names[i], sizeof(names[i]), "v%02d", i);
        many[i] = names[i];
    }
    CL_CHECK(cl_schema_block_add_enum(blk, "long", many, 40, 0) == 0);
    check_schema(api, "b {\n  long = \"nope\"\n}\n", NULL, 0, "deve ser um de: v00, v01", 2, 3);
    cl_schema_free(api);
}

void cl_test_run_schema(void) {
    test_valid_documents();
    test_structural_errors();
    test_type_errors();
    test_strict_mode();
    test_enum_values();
    test_schema_api();
    test_schema_file_matches_c_api();
    test_schema_file_errors();
}
