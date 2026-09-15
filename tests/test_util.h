#ifndef CL_TEST_UTIL_H
#define CL_TEST_UTIL_H

#include <stdio.h>
#include <string.h>

extern int cl_test_failures;

#define CL_CHECK(cond)                                                                 \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "%s:%d: FAIL: %s\n", __FILE__, __LINE__, #cond);           \
            cl_test_failures++;                                                        \
        }                                                                              \
    } while (0)

#define CL_CHECK_STREQ(actual, expected)                                               \
    do {                                                                               \
        const char *cl_actual_ = (actual);                                             \
        const char *cl_expected_ = (expected);                                         \
        if (!cl_actual_ || !cl_expected_ || strcmp(cl_actual_, cl_expected_) != 0) {   \
            fprintf(stderr, "%s:%d: FAIL: %s == \"%s\", expected \"%s\"\n", __FILE__,  \
                    __LINE__, #actual, cl_actual_ ? cl_actual_ : "(null)",              \
                    cl_expected_ ? cl_expected_ : "(null)");                            \
            cl_test_failures++;                                                        \
        }                                                                              \
    } while (0)

#endif /* CL_TEST_UTIL_H */
