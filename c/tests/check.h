#ifndef MAELYS_CLI_TESTS_CHECK_H
#define MAELYS_CLI_TESTS_CHECK_H

#include <stdio.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 0; \
    } \
} while (0)

#define RUN(test) do { \
    int passed_ = test(); \
    (void)fprintf(stderr, "%s %s\n", passed_ ? "PASS" : "FAIL", #test); \
    if (!passed_) failures++; \
} while (0)

#endif
