/* cl_schema_example - how to validate documents with a block schema.
 *
 * Shows both ways of building a schema (in C and from a .cl schema text),
 * the two validation passes (structure before evaluation, evaluated types
 * after it), the kinds of problems a schema reports, and reading the
 * values of a document once it is known to be valid.
 *
 * Every document is embedded as a string, so the program runs from any
 * working directory. */
#include "cl/cl.h"

#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Building a schema                                                    */
/* ------------------------------------------------------------------ */

/* The "machine" rules written with the C API. */
static cl_schema_t *build_schema_in_c(void) {
    cl_schema_t *schema = cl_schema_new();
    cl_schema_set_strict(schema, 1); /* unregistered top-level blocks are errors */

    cl_schema_block_t *machine = cl_schema_add_block(schema, "machine");
    cl_schema_block_add_attr(machine, "cpu", CL_TYPE_NUMBER, 1);    /* required */
    cl_schema_block_add_attr(machine, "memory", CL_TYPE_STRING, 1); /* required */
    cl_schema_block_add_attr(machine, "tags", CL_TYPE_LIST, 0);     /* optional */
    cl_schema_block_add_attr(machine, "meta", CL_TYPE_ANY, 0);      /* anything, null included */

    cl_schema_block_t *disk = cl_schema_block_add_block(machine, "disk");
    cl_schema_block_add_attr(disk, "size", CL_TYPE_NUMBER, 1);
    static const char *const kinds[] = {"ssd", "hdd", "nvme"};
    cl_schema_block_add_enum(disk, "kind", kinds, sizeof(kinds) / sizeof(kinds[0]), 0);

    return schema;
}

/* The same rules as a .cl schema text (cl_schema_load_file() reads the
 * same format from disk). */
static const char *const SCHEMA_SOURCE =
    "strict = true\n"
    "\n"
    "block \"machine\" {\n"
    "  cpu    = { type = \"number\", required = true }\n"
    "  memory = { type = \"string\", required = true }\n"
    "  tags   = \"list\"\n"
    "  meta   = \"any\"\n"
    "\n"
    "  block \"disk\" {\n"
    "    size = { type = \"number\", required = true }\n"
    "    kind = [\"ssd\", \"hdd\", \"nvme\"]\n"
    "  }\n"
    "}\n";

/* ------------------------------------------------------------------ */
/* Validating                                                           */
/* ------------------------------------------------------------------ */

/* The usual pipeline: load, check structure, evaluate, check evaluated
 * types. Returns the evaluated result (the caller frees it and *out_doc)
 * or NULL after printing the first problem. */
static cl_evaluated_t *load_and_validate(const cl_schema_t *schema, const char *name, const char *source,
                                         cl_document_t **out_doc) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, name, &err);
    if (!doc) {
        printf("  parse error (line %d, column %d): %s\n", err.line, err.col, err.message);
        return NULL;
    }

    /* Pass 1 - before evaluation: which blocks/attributes may appear,
     * required attributes, and the types of literal values. Cheap, and it
     * catches most mistakes without running any expression. */
    if (cl_schema_validate(schema, doc, NULL, &err) != 0) {
        printf("  schema error (line %d, column %d): %s\n", err.line, err.col, err.message);
        cl_document_free(doc);
        return NULL;
    }

    cl_evaluated_t *result = cl_document_evaluate(doc, &err);
    if (!result) {
        printf("  evaluation error (line %d, column %d): %s\n", err.line, err.col, err.message);
        cl_document_free(doc);
        return NULL;
    }

    /* Pass 2 - after evaluation: the type of every value, including those
     * computed by expressions ("cpu = base * 2") or injected by bindings. */
    if (cl_schema_validate(schema, doc, result, &err) != 0) {
        printf("  schema error (line %d, column %d): %s\n", err.line, err.col, err.message);
        cl_evaluated_free(result);
        cl_document_free(doc);
        return NULL;
    }

    *out_doc = doc;
    return result;
}

static void check(const cl_schema_t *schema, const char *name, const char *source) {
    printf("-- %s\n", name);
    cl_document_t *doc = NULL;
    cl_evaluated_t *result = load_and_validate(schema, name, source, &doc);
    if (result) {
        printf("  valid\n");
        cl_evaluated_free(result);
        cl_document_free(doc);
    }
}

/* ------------------------------------------------------------------ */
/* Documents                                                            */
/* ------------------------------------------------------------------ */

static const char *const VALID_DOC =
    "base_cpu = 2              # top-level attributes are never checked\n"
    "\n"
    "machine \"web\" {\n"
    "  cpu    = base_cpu * 2\n"
    "  memory = \"8GB\"\n"
    "  tags   = [\"frontend\", \"public\"]\n"
    "  disk {\n"
    "    size = 100\n"
    "    kind = \"ssd\"\n"
    "  }\n"
    "}\n"
    "\n"
    "machine \"db\" {\n"
    "  cpu    = 8\n"
    "  memory = \"32GB\"\n"
    "  meta   = null\n"
    "  disk { size = 500 }\n"
    "  disk {\n"
    "    size = 2000\n"
    "    kind = \"hdd\"\n"
    "  }\n"
    "}\n";

/* Each of these breaks exactly one rule. */
static const struct {
    const char *name;
    const char *source;
} INVALID_DOCS[] = {
    {"unknown attribute",
     "machine \"a\" {\n  cpu = 1\n  memory = \"1GB\"\n  gpu = 1\n}\n"},
    {"missing required attribute",
     "machine \"a\" {\n  cpu = 1\n}\n"},
    {"attribute repeated",
     "machine \"a\" {\n  cpu = 1\n  cpu = 2\n  memory = \"1GB\"\n}\n"},
    {"wrong literal type (caught before evaluation)",
     "machine \"a\" {\n  cpu = \"four\"\n  memory = \"1GB\"\n}\n"},
    {"wrong evaluated type (caught only after evaluation)",
     "n = \"four\"\nmachine \"a\" {\n  cpu = n\n  memory = \"1GB\"\n}\n"},
    {"null outside an 'any' attribute",
     "machine \"a\" {\n  cpu = null\n  memory = \"1GB\"\n}\n"},
    {"value not in the enum",
     "machine \"a\" {\n  cpu = 1\n  memory = \"1GB\"\n  disk {\n    size = 1\n    kind = \"tape\"\n  }\n}\n"},
    {"sub-block not declared",
     "machine \"a\" {\n  cpu = 1\n  memory = \"1GB\"\n  nic { speed = 10 }\n}\n"},
    {"unregistered top-level block (strict schema)",
     "machine \"a\" {\n  cpu = 1\n  memory = \"1GB\"\n}\nnetwork \"lan\" {}\n"},
};

/* ------------------------------------------------------------------ */
/* Reading a validated document                                         */
/* ------------------------------------------------------------------ */

/* Once the schema passed, required attributes are known to exist with the
 * right type, so the getters' defaults only matter for optional ones. */
static void print_machines(cl_evaluated_t *result) {
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    cl_block_iter_t it;
    const cl_evaluated_block_t *machine;
    cl_block_iter_init(&it, root, "machine");
    while ((machine = cl_block_iter_next(&it)) != NULL) {
        const cl_evaluated_body_t *body = machine->body;
        printf("  machine %s: cpu=%ld memory=%s tags=%zu\n", machine->labels[0], cl_get_int(body, "cpu", 0),
               cl_get_string(body, "memory", ""), cl_get_count(body, "tags"));

        cl_block_iter_t disks;
        const cl_evaluated_block_t *disk;
        cl_block_iter_init(&disks, body, "disk");
        while ((disk = cl_block_iter_next(&disks)) != NULL) {
            printf("    disk size=%ld kind=%s\n", cl_get_int(disk->body, "size", 0),
                   cl_get_string(disk->body, "kind", "(default)"));
        }
    }
}

int main(void) {
    cl_error_t err;

    /* 1. A schema built in C. */
    cl_schema_t *schema = build_schema_in_c();

    printf("== valid document ==\n");
    cl_document_t *doc = NULL;
    cl_evaluated_t *result = load_and_validate(schema, "valid.cl", VALID_DOC, &doc);
    if (result) {
        print_machines(result);
        cl_evaluated_free(result);
        cl_document_free(doc);
    }

    printf("\n== invalid documents ==\n");
    for (size_t i = 0; i < sizeof(INVALID_DOCS) / sizeof(INVALID_DOCS[0]); i++) {
        check(schema, INVALID_DOCS[i].name, INVALID_DOCS[i].source);
    }
    cl_schema_free(schema);

    /* 2. The same rules loaded from schema text: same results. */
    printf("\n== schema loaded from .cl text ==\n");
    schema = cl_schema_load_string(SCHEMA_SOURCE, "machine.schema.cl", &err);
    if (!schema) {
        printf("  invalid schema (line %d, column %d): %s\n", err.line, err.col, err.message);
        return 1;
    }
    check(schema, "valid.cl", VALID_DOC);
    check(schema, INVALID_DOCS[0].name, INVALID_DOCS[0].source);
    cl_schema_free(schema);

    /* 3. A mistake in the schema itself is reported when loading it. */
    printf("\n== broken schema text ==\n");
    schema = cl_schema_load_string("block \"machine\" {\n  cpu = \"integer\"\n}\n", "broken.schema.cl", &err);
    if (!schema) {
        printf("  invalid schema (line %d, column %d): %s\n", err.line, err.col, err.message);
    }
    cl_schema_free(schema);

    /* 4. A non-strict schema ignores block types it does not know. */
    printf("\n== non-strict schema ==\n");
    schema = build_schema_in_c();
    cl_schema_set_strict(schema, 0);
    check(schema, INVALID_DOCS[8].name, INVALID_DOCS[8].source);
    cl_schema_free(schema);

    return 0;
}
