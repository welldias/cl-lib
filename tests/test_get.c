#include "cl/cl.h"
#include "test_util.h"

#include <stdlib.h>

static const char *const doc_source =
    "name     = \"demo\"\n"
    "port     = 8080\n"
    "neg      = -3\n"
    "ratio    = 0.5\n"
    "huge     = 1e30\n"
    "debug    = true\n"
    "nothing  = null\n"
    "computed = \"${name}-${port}\"\n"
    "tags     = { Name = \"web\", \"a.b\" = \"dotted\" }\n"
    "ips      = [\"10.0.0.1\", \"10.0.0.2\"]\n"
    "disks    = [{ size = 100 }, { size = 200 }]\n"
    "mode     = \"safe\"\n"
    "shout    = \"SAFE\"\n"
    "ports    = [80, 443, 8080]\n"
    "ratios   = [0.5, 1.5]\n"
    "fracs    = [1, 2.5]\n"
    "mixed    = [1, \"a\"]\n"
    "empty    = []\n"
    "env      = { A = \"1\", B = \"2\" }\n"
    "\n"
    "server \"web\" {\n"
    "  port = 80\n"
    "  tls {\n"
    "    enabled = true\n"
    "  }\n"
    "}\n"
    "server \"api\" {\n"
    "  port = 81\n"
    "}\n"
    "server {\n"
    "  port = 82\n"
    "}\n"
    "\n"
    "resource \"aws_instance\" \"app\" {\n"
    "  ami = \"ami-2\"\n"
    "}\n"
    "resource \"aws_instance\" {\n"
    "  ami = \"ami-1\"\n"
    "}\n"
    "\n"
    "shadow = { x = 1 }\n"
    "shadow \"x\" {\n"
    "  y = 2\n"
    "}\n"
    "\n"
    "machine \"m1\" {\n"
    "  cpu   = 4\n"
    "  label = \"m1\"\n"
    "  disk {\n"
    "    size = 10\n"
    "  }\n"
    "  nic \"eth0\" {}\n"
    "  disk {\n"
    "    size = 20\n"
    "  }\n"
    "  disk {\n"
    "    size = 30\n"
    "  }\n"
    "}\n";

static cl_evaluated_t *eval_source(const char *source, const cl_bindings_t *bindings) {
    cl_error_t err;
    cl_document_t *doc = cl_load_string(source, "get", &err);
    CL_CHECK(doc != NULL);
    if (!doc) {
        return NULL;
    }
    cl_evaluated_t *result = cl_document_evaluate_with(doc, bindings, &err);
    CL_CHECK(result != NULL);
    cl_document_free(doc);
    return result;
}

static size_t iter_count(const cl_evaluated_body_t *base, const char *path) {
    cl_block_iter_t it;
    size_t n = 0;
    cl_block_iter_init(&it, base, path);
    while (cl_block_iter_next(&it)) {
        n++;
    }
    return n;
}

static void test_top_level_values(const cl_evaluated_body_t *root) {
    CL_CHECK_STREQ(cl_get_string(root, "name", "x"), "demo");
    CL_CHECK(cl_get_int(root, "port", 0) == 8080);
    CL_CHECK(cl_get_int(root, "neg", 0) == -3);
    CL_CHECK(cl_get_number(root, "ratio", 0) == 0.5);
    CL_CHECK(cl_get_number(root, "huge", 0) == 1e30);
    CL_CHECK(cl_get_bool(root, "debug", 0) == 1);
    CL_CHECK_STREQ(cl_get_string(root, "computed", NULL), "demo-8080");
}

static void test_values_inside_values(const cl_evaluated_body_t *root) {
    CL_CHECK_STREQ(cl_get_string(root, "tags.Name", NULL), "web");
    CL_CHECK_STREQ(cl_get_string(root, "tags[\"Name\"]", NULL), "web");
    CL_CHECK_STREQ(cl_get_string(root, "tags[\"a.b\"]", NULL), "dotted");
    CL_CHECK_STREQ(cl_get_string(root, "[\"tags\"].Name", NULL), "web");
    CL_CHECK_STREQ(cl_get_string(root, "ips[1]", NULL), "10.0.0.2");
    CL_CHECK(cl_get_int(root, "disks[1].size", 0) == 200);
    CL_CHECK(cl_get_string(root, "ips[2]", NULL) == NULL);
    CL_CHECK(cl_get_string(root, "tags.missing", NULL) == NULL);

    const cl_value_t *ips = cl_get_list(root, "ips");
    CL_CHECK(ips != NULL && cl_value_list_count(ips) == 2);
    CL_CHECK(cl_get_list(root, "name") == NULL);
    CL_CHECK(cl_get_list(root, "missing") == NULL);

    const cl_value_t *tags = cl_get_value(root, "tags");
    CL_CHECK(tags != NULL && cl_value_kind(tags) == CL_VAL_OBJECT);
}

static void test_blocks_by_labels(const cl_evaluated_body_t *root) {
    CL_CHECK(cl_get_int(root, "server.web.port", 0) == 80);
    CL_CHECK(cl_get_int(root, "server.api.port", 0) == 81);
    CL_CHECK(cl_get_int(root, "server.port", 0) == 82); /* unlabeled block */
    CL_CHECK(cl_get_bool(root, "server.web.tls.enabled", 0) == 1);

    /* The two-label block wins when the path supplies both labels; the
     * one-label block is still reachable when it doesn't. */
    CL_CHECK_STREQ(cl_get_string(root, "resource.aws_instance.app.ami", NULL), "ami-2");
    CL_CHECK_STREQ(cl_get_string(root, "resource.aws_instance.ami", NULL), "ami-1");

    /* An attribute wins over a block of the same name. */
    CL_CHECK(cl_get_int(root, "shadow.x", 0) == 1);
    CL_CHECK(cl_get_int(root, "shadow.x.y", 0) == 0);
    CL_CHECK(cl_get_block(root, "shadow.x") == NULL);

    const cl_evaluated_block_t *web = cl_get_block(root, "server.web");
    CL_CHECK(web != NULL);
    if (web) {
        CL_CHECK(web->label_count == 1);
        CL_CHECK_STREQ(web->labels[0], "web");
        CL_CHECK(cl_get_int(web->body, "port", 0) == 80); /* relative read */
        CL_CHECK(cl_get_bool(web->body, "tls.enabled", 0) == 1);
    }
    const cl_evaluated_block_t *plain = cl_get_block(root, "server");
    CL_CHECK(plain != NULL && plain->label_count == 0);
    CL_CHECK(cl_get_block(root, "server.nope") == NULL);
    CL_CHECK(cl_get_block(root, "server.web.port") == NULL); /* a value, not a block */
    CL_CHECK(cl_get_int(root, "server.web", 7) == 7);        /* a block, not a value */
}

static void test_defaults(const cl_evaluated_body_t *root) {
    /* missing */
    CL_CHECK_STREQ(cl_get_string(root, "missing", "fallback"), "fallback");
    CL_CHECK(cl_get_string(root, "missing", NULL) == NULL);
    CL_CHECK(cl_get_int(root, "missing", 42) == 42);
    CL_CHECK(cl_get_number(root, "missing", 1.5) == 1.5);
    CL_CHECK(cl_get_bool(root, "missing", 1) == 1);
    /* null */
    CL_CHECK_STREQ(cl_get_string(root, "nothing", "d"), "d");
    CL_CHECK(cl_get_int(root, "nothing", 5) == 5);
    CL_CHECK(cl_get_value(root, "nothing") == NULL);
    /* wrong type */
    CL_CHECK(cl_get_int(root, "name", 9) == 9);
    CL_CHECK_STREQ(cl_get_string(root, "port", "d"), "d");
    CL_CHECK(cl_get_bool(root, "port", 0) == 0);
    CL_CHECK(cl_get_number(root, "debug", 2.5) == 2.5);
    /* numbers that are not a long */
    CL_CHECK(cl_get_int(root, "ratio", -1) == -1);
    CL_CHECK(cl_get_int(root, "huge", -1) == -1);
}

static void test_malformed_paths(const cl_evaluated_body_t *root) {
    const char *bad[] = {"", "port.", ".port", "a..b", "ips[", "ips[x]", "ips[1", "tags[\"Name", "tags[\"Name\"",
                         "[0]", "port[0]", "name.x"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CL_CHECK(cl_get_int(root, bad[i], -7) == -7);
        CL_CHECK(cl_get_value(root, bad[i]) == NULL);
        CL_CHECK(cl_get_block(root, bad[i]) == NULL);
    }
    CL_CHECK(cl_get_int(root, NULL, -7) == -7);
    CL_CHECK(cl_get_int(NULL, "port", -7) == -7);
    CL_CHECK(cl_get_block(NULL, "server") == NULL);
}

static void test_iterator(const cl_evaluated_body_t *root) {
    cl_block_iter_t it;
    const cl_evaluated_block_t *disk;
    long sizes[3] = {0, 0, 0};
    size_t n = 0;
    cl_block_iter_init(&it, root, "machine.m1.disk");
    while ((disk = cl_block_iter_next(&it)) != NULL) {
        if (n < 3) {
            sizes[n] = cl_get_int(disk->body, "size", 0);
        }
        n++;
    }
    CL_CHECK(n == 3);
    CL_CHECK(sizes[0] == 10 && sizes[1] == 20 && sizes[2] == 30);
    CL_CHECK(cl_block_iter_next(&it) == NULL); /* stays exhausted */

    CL_CHECK(iter_count(root, "machine.m1.nic") == 1);
    CL_CHECK(iter_count(root, "machine.m1.gpu") == 0);
    CL_CHECK(iter_count(root, "machine.m2.disk") == 0); /* missing prefix */
    CL_CHECK(iter_count(root, "server") == 3);          /* directly in base */
    CL_CHECK(iter_count(root, "[\"server\"]") == 3);
    CL_CHECK(iter_count(root, "tags.Name") == 0);       /* prefix is a value */
    CL_CHECK(iter_count(root, "machine..disk") == 0);
    CL_CHECK(iter_count(root, "machine.m1.disk[0]") == 0);
    CL_CHECK(iter_count(root, "") == 0);
    CL_CHECK(iter_count(NULL, "server") == 0);

    const cl_evaluated_block_t *m1 = cl_get_block(root, "machine.m1");
    CL_CHECK(m1 != NULL && iter_count(m1->body, "disk") == 3);
}

static void test_kind_has_count(const cl_evaluated_body_t *root) {
    CL_CHECK(cl_get_kind(root, "name") == CL_GET_STRING);
    CL_CHECK(cl_get_kind(root, "port") == CL_GET_NUMBER);
    CL_CHECK(cl_get_kind(root, "debug") == CL_GET_BOOL);
    CL_CHECK(cl_get_kind(root, "ips") == CL_GET_LIST);
    CL_CHECK(cl_get_kind(root, "tags") == CL_GET_OBJECT);
    CL_CHECK(cl_get_kind(root, "server.web") == CL_GET_BLOCK);
    CL_CHECK(cl_get_kind(root, "nothing") == CL_GET_MISSING);
    CL_CHECK(cl_get_kind(root, "missing") == CL_GET_MISSING);
    CL_CHECK(cl_get_kind(root, "a..b") == CL_GET_MISSING);

    CL_CHECK(cl_has(root, "port") == 1);
    CL_CHECK(cl_has(root, "server.web.tls") == 1);
    CL_CHECK(cl_has(root, "nothing") == 0);
    CL_CHECK(cl_has(root, "missing") == 0);
    CL_CHECK(cl_has(NULL, "port") == 0);

    CL_CHECK(cl_get_count(root, "ips") == 2);
    CL_CHECK(cl_get_count(root, "tags") == 2);
    CL_CHECK(cl_get_count(root, "empty") == 0);
    CL_CHECK(cl_get_count(root, "server") == 3); /* blocks, not the unlabeled one's body */
    CL_CHECK(cl_get_count(root, "machine.m1.disk") == 3);
    CL_CHECK(cl_get_count(root, "name") == 0);
    CL_CHECK(cl_get_count(root, "missing") == 0);
    CL_CHECK(cl_get_count(root, NULL) == 0);
}

static void test_enum(const cl_evaluated_body_t *root) {
    static const char *const modes[] = {"fast", "safe", "debug"};
    CL_CHECK(cl_get_enum(root, "mode", modes, 3, -1) == 1);
    CL_CHECK(cl_get_enum(root, "shout", modes, 3, -1) == -1); /* case-sensitive */
    CL_CHECK(cl_get_enum(root, "name", modes, 3, -1) == -1);  /* not in the list */
    CL_CHECK(cl_get_enum(root, "port", modes, 3, -1) == -1);  /* not a string */
    CL_CHECK(cl_get_enum(root, "missing", modes, 3, -1) == -1);
    CL_CHECK(cl_get_enum(root, "mode", modes, 1, -1) == -1);  /* only "fast" allowed */
    CL_CHECK(cl_get_enum(root, "mode", NULL, 0, -1) == -1);
}

static void test_list_arrays(const cl_evaluated_body_t *root) {
    const char *strs[3] = {NULL, NULL, "untouched"};
    CL_CHECK(cl_get_strings(root, "ips", strs, 3) == 2);
    CL_CHECK_STREQ(strs[0], "10.0.0.1");
    CL_CHECK_STREQ(strs[1], "10.0.0.2");
    CL_CHECK_STREQ(strs[2], "untouched");

    const char *one[2] = {NULL, "untouched"};
    CL_CHECK(cl_get_strings(root, "ips", one, 1) == 2); /* count tells it didn't fit */
    CL_CHECK_STREQ(one[0], "10.0.0.1");
    CL_CHECK_STREQ(one[1], "untouched");
    CL_CHECK(cl_get_strings(root, "ips", NULL, 0) == 2);
    CL_CHECK(cl_get_strings(root, "mixed", strs, 3) == 0);
    CL_CHECK(cl_get_strings(root, "name", strs, 3) == 0);
    CL_CHECK(cl_get_strings(root, "empty", strs, 3) == 0);

    long ints[4] = {-1, -1, -1, -1};
    CL_CHECK(cl_get_ints(root, "ports", ints, 4) == 3);
    CL_CHECK(ints[0] == 80 && ints[1] == 443 && ints[2] == 8080 && ints[3] == -1);
    long untouched[2] = {-1, -1};
    CL_CHECK(cl_get_ints(root, "fracs", untouched, 2) == 0); /* 2.5 is not a long */
    CL_CHECK(untouched[0] == -1 && untouched[1] == -1);
    CL_CHECK(cl_get_ints(root, "ratios", untouched, 2) == 0);
    CL_CHECK(cl_get_ints(root, "mixed", untouched, 2) == 0);

    double nums[2] = {0, 0};
    CL_CHECK(cl_get_numbers(root, "ratios", nums, 2) == 2);
    CL_CHECK(nums[0] == 0.5 && nums[1] == 1.5);
    CL_CHECK(cl_get_numbers(root, "mixed", nums, 2) == 0);
    CL_CHECK(cl_get_numbers(root, "ips", nums, 2) == 0);
}

static size_t attr_count(const cl_evaluated_body_t *base, const char *path) {
    cl_attr_iter_t it;
    size_t n = 0;
    cl_attr_iter_init(&it, base, path);
    while (cl_attr_iter_next(&it, NULL, NULL)) {
        n++;
    }
    return n;
}

static void test_attr_iterator(const cl_evaluated_body_t *root) {
    cl_attr_iter_t it;
    const char *key = NULL;
    const cl_value_t *value = NULL;

    /* block body: attributes only, nested blocks skipped */
    cl_attr_iter_init(&it, root, "machine.m1");
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 1);
    CL_CHECK_STREQ(key, "cpu");
    CL_CHECK(cl_value_kind(value) == CL_VAL_NUMBER);
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 1);
    CL_CHECK_STREQ(key, "label");
    CL_CHECK_STREQ(cl_value_as_string(value), "m1");
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 0);
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 0); /* stays exhausted */

    /* object value */
    cl_attr_iter_init(&it, root, "env");
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 1);
    CL_CHECK_STREQ(key, "A");
    CL_CHECK_STREQ(cl_value_as_string(value), "1");
    CL_CHECK(cl_attr_iter_next(&it, &key, NULL) == 1);
    CL_CHECK_STREQ(key, "B");
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 0);

    /* base itself, null values included */
    int saw_null = 0;
    cl_attr_iter_init(&it, root, "");
    CL_CHECK(cl_attr_iter_next(&it, &key, &value) == 1);
    CL_CHECK_STREQ(key, "name");
    while (cl_attr_iter_next(&it, &key, &value)) {
        if (strcmp(key, "nothing") == 0 && cl_value_kind(value) == CL_VAL_NULL) {
            saw_null = 1;
        }
    }
    CL_CHECK(saw_null);
    CL_CHECK(attr_count(root, NULL) == attr_count(root, ""));

    CL_CHECK(attr_count(root, "server.web") == 1);
    CL_CHECK(attr_count(root, "name") == 0);    /* a string: nothing to walk */
    CL_CHECK(attr_count(root, "ips") == 0);     /* a list either */
    CL_CHECK(attr_count(root, "missing") == 0);
    CL_CHECK(attr_count(NULL, "env") == 0);
}

static void test_formatted_paths(const cl_evaluated_body_t *root) {
    CL_CHECK(cl_get_intf(root, 0, "server.%s.port", "api") == 81);
    CL_CHECK(cl_get_intf(root, -1, "server.%s.port", "nope") == -1);
    CL_CHECK_STREQ(cl_get_stringf(root, NULL, "tags[\"%s\"]", "a.b"), "dotted");
    CL_CHECK_STREQ(cl_get_stringf(root, "d", "ips[%d]", 9), "d");
    CL_CHECK(cl_get_boolf(root, 0, "server.%s.tls.enabled", "web") == 1);
    CL_CHECK(cl_get_blockf(root, "machine.%s", "m1") != NULL);
    CL_CHECK(cl_get_blockf(root, "machine.%s", "m2") == NULL);
}

/* A path longer than the formatting stack buffer still resolves. */
static void test_long_formatted_path(void) {
    char name[301];
    memset(name, 'k', 300);
    name[300] = '\0';
    char source[400];
    snprintf(source, sizeof(source), "%s = 7\n", name);
    cl_evaluated_t *result = eval_source(source, NULL);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK(cl_get_intf(root, 0, "%s", name) == 7);
    CL_CHECK_STREQ(cl_get_stringf(root, "d", "%s.x", name), "d");
    cl_evaluated_free(result);
}

static void test_bound_values(void) {
    cl_bindings_t *b = cl_bindings_new();
    cl_bindings_set_number(b, "replicas", 6);
    cl_bindings_set_string(b, "env", "prod");
    cl_evaluated_t *result = eval_source(
        "env = \"dev\"\n"
        "service \"api\" {\n"
        "  replicas = replicas / 2\n"
        "  name     = \"api-${env}\"\n"
        "}\n",
        b);
    cl_bindings_free(b);
    if (!result) {
        return;
    }
    const cl_evaluated_body_t *root = cl_evaluated_root(result);
    CL_CHECK_STREQ(cl_get_string(root, "env", NULL), "prod");
    CL_CHECK(cl_get_int(root, "service.api.replicas", 0) == 3);
    CL_CHECK_STREQ(cl_get_string(root, "service.api.name", NULL), "api-prod");
    cl_evaluated_free(result);
}

void cl_test_run_get(void) {
    cl_evaluated_t *result = eval_source(doc_source, NULL);
    if (result) {
        const cl_evaluated_body_t *root = cl_evaluated_root(result);
        test_top_level_values(root);
        test_values_inside_values(root);
        test_blocks_by_labels(root);
        test_defaults(root);
        test_malformed_paths(root);
        test_iterator(root);
        test_kind_has_count(root);
        test_enum(root);
        test_list_arrays(root);
        test_attr_iterator(root);
        test_formatted_paths(root);
        cl_evaluated_free(result);
    }
    test_long_formatted_path();
    test_bound_values();
}
