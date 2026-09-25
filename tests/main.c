#include "test_util.h"

int cl_test_failures = 0;

void cl_test_run_parser(void);
void cl_test_run_eval(void);
void cl_test_run_navigate(void);
void cl_test_run_writer(void);
void cl_test_run_fixtures(void);
void cl_test_run_bindings(void);

int main(int argc, char **argv) {
    const char *group = argc > 1 ? argv[1] : NULL;

    if (!group || strcmp(group, "parser") == 0) {
        cl_test_run_parser();
    }
    if (!group || strcmp(group, "eval") == 0) {
        cl_test_run_eval();
    }
    if (!group || strcmp(group, "navigate") == 0) {
        cl_test_run_navigate();
    }
    if (!group || strcmp(group, "writer") == 0) {
        cl_test_run_writer();
    }
    if (!group || strcmp(group, "fixtures") == 0) {
        cl_test_run_fixtures();
    }
    if (!group || strcmp(group, "bindings") == 0) {
        cl_test_run_bindings();
    }

    if (cl_test_failures > 0) {
        fprintf(stderr, "\n%d assertion(s) failed\n", cl_test_failures);
        return 1;
    }
    printf("ok (group=%s)\n", group ? group : "all");
    return 0;
}
