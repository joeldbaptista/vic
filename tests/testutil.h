/*
 * testutil.h - minimal unit-test assertion macros.
 *
 * Usage in a test file:
 *
 *   #include "testutil.h"
 *
 *   static void test_foo(void) { CHECK(1 + 1 == 2); }
 *
 *   int main(void) { test_foo(); SUMMARY(); }
 *
 * CHECK(expr)   — assert expr is true; print location and expression on failure.
 * SUMMARY()     — print pass/fail count and return the right exit code.
 *                 Must be the last statement in main().
 */
#ifndef TESTS_TESTUTIL_H
#define TESTS_TESTUTIL_H

#include <stdio.h>
#include <stdlib.h>

static int t_run  = 0;
static int t_fail = 0;

#define CHECK(expr)                                                          \
    do {                                                                     \
        t_run++;                                                             \
        if (!(expr)) {                                                       \
            t_fail++;                                                        \
            fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__,      \
                    #expr);                                                  \
        }                                                                    \
    } while (0)

#define SUMMARY()                                                            \
    do {                                                                     \
        if (t_fail)                                                          \
            fprintf(stderr, "FAIL %s  (%d/%d)\n", __FILE__,                \
                    t_fail, t_run);                                          \
        else                                                                 \
            fprintf(stderr, "PASS %s  (%d)\n", __FILE__, t_run);           \
        return t_fail ? 1 : 0;                                              \
    } while (0)

#endif /* TESTS_TESTUTIL_H */
