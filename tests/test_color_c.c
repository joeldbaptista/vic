/*
 * test_color_c.c - unit tests for the C/C++ syntax colorizer.
 *
 * Cross-line states (numeric values, mirrors color_c.c internals):
 *   0  CS_NORMAL
 *   1  CS_BLOCK_CMT
 *   2  CS_BLOCK_CMT_STAR
 *   3  CS_PREPROC_CONT
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_c;
extern const struct colorizer colorizer_cpp;

#define C   colorizer_c.colorize
#define CPP colorizer_cpp.colorize

/* ---- keywords ----------------------------------------------------------- */

static void
test_keywords(void)
{
    int st;

    st = CHECK_COLOR(C, 0, "int",        "KKK");        CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "void",       "KKKK");       CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "return",     "KKKKKK");     CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "static",     "KKKKKK");     CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "struct",     "KKKKKK");     CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "sizeof",     "KKKKKK");     CHECK(st == 0);
    /* identifier that is NOT a keyword */
    st = CHECK_COLOR(C, 0, "foo",        "...");        CHECK(st == 0);
    /* keyword adjacent to punctuation */
    st = CHECK_COLOR(C, 0, "if(x)",      "KK...");      CHECK(st == 0);
    /* two keywords separated by whitespace */
    st = CHECK_COLOR(C, 0, "int x",      "KKK..");      CHECK(st == 0);
}

static void
test_keywords_cpp_only(void)
{
    int st;

    /* "class" is a C++ keyword but not a C keyword */
    st = CHECK_COLOR(CPP, 0, "class",    "KKKKK");      CHECK(st == 0);
    st = CHECK_COLOR(C,   0, "class",    ".....");      CHECK(st == 0);
    /* "namespace" likewise */
    st = CHECK_COLOR(CPP, 0, "namespace", "KKKKKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(C,   0, "namespace", ".........");  CHECK(st == 0);
}

/* ---- line comment ------------------------------------------------------- */

static void
test_line_comment(void)
{
    int st;

    st = CHECK_COLOR(C, 0, "// comment",     "CCCCCCCCCC");   CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "//",             "CC");           CHECK(st == 0);
    /* comment following code */
    st = CHECK_COLOR(C, 0, "x = 1; // note", "....N..CCCCCCC"); CHECK(st == 0);
    /* single slash alone is NOT a comment */
    st = CHECK_COLOR(C, 0, "a / b",          ".....");         CHECK(st == 0);
}

/* ---- block comment ------------------------------------------------------ */

static void
test_block_comment_single_line(void)
{
    int st;

    st = CHECK_COLOR(C, 0, "/* hi */",   "CCCCCCCC"); CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "/**/",       "CCCC");     CHECK(st == 0);
    /* block comment then code on same line */
    st = CHECK_COLOR(C, 0, "/* x */ y",  "CCCCCCC.."); CHECK(st == 0);
}

static void
test_block_comment_multiline(void)
{
    int st;

    /* open — state becomes CS_BLOCK_CMT = 1 */
    st = CHECK_COLOR(C, 0, "/* open",     "CCCCCCC"); CHECK(st == 1);
    /* continuation — still in comment */
    st = CHECK_COLOR(C, 1, "middle line", "CCCCCCCCCCC"); CHECK(st == 1);
    /* close — state returns to CS_NORMAL = 0 */
    st = CHECK_COLOR(C, 1, "end */",      "CCCCCC"); CHECK(st == 0);
    /* close with trailing code (close is at byte 6, then " x;" = 3 normal) */
    st = CHECK_COLOR(C, 1, "done */ x;",  "CCCCCCC..."); CHECK(st == 0);

    /* line ending with '*': state becomes CS_BLOCK_CMT_STAR = 2 */
    st = CHECK_COLOR(C, 0, "/* note *",   "CCCCCCCCC"); CHECK(st == 2);
    /* next char '/' closes: only the '/' is comment, rest is normal */
    st = CHECK_COLOR(C, 2, "/ more",      "C....."); CHECK(st == 0);
}

/* ---- string literals ---------------------------------------------------- */

static void
test_strings(void)
{
    int st;

    st = CHECK_COLOR(C, 0, "\"hi\"",          "SSSS");  CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "\"hello world\"", "SSSSSSSSSSSSS"); CHECK(st == 0);
    /* escaped double-quote inside string */
    st = CHECK_COLOR(C, 0, "\"a\\\"b\"",      "SSSSSS"); CHECK(st == 0);
    /* escaped backslash */
    st = CHECK_COLOR(C, 0, "\"a\\\\b\"",      "SSSSSS"); CHECK(st == 0);
    /* string in expression */
    st = CHECK_COLOR(C, 0, "p = \"ok\";",     "....SSSS."); CHECK(st == 0);
}

static void
test_char_literals(void)
{
    int st;

    st = CHECK_COLOR(C, 0, "'a'",    "SSS");  CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "'\\n'",  "SSSS"); CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "'\\t'",  "SSSS"); CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "'\\\\'", "SSSS"); CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(C, 0, "42",     "NN");     CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "0",      "N");      CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "0xFF",   "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "0xDEAD", "NNNNNN"); CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "3.14",   "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "1e10",   "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "1.5e-3", "NNNNNN"); CHECK(st == 0);
    /* integer suffixes */
    st = CHECK_COLOR(C, 0, "42UL",   "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "10u",    "NNN");     CHECK(st == 0);
    /* float suffix */
    st = CHECK_COLOR(C, 0, "3.14f",  "NNNNN");  CHECK(st == 0);
    /* number in expression */
    st = CHECK_COLOR(C, 0, "x = 0;", "....N."); CHECK(st == 0);
}

/* ---- preprocessor ------------------------------------------------------- */

static void
test_preprocessor(void)
{
    int st;

    /* whole line is PREPROC when # is first non-whitespace */
    st = CHECK_COLOR(C, 0, "#include <stdio.h>", "PPPPPPPPPPPPPPPPPP"); CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "#define FOO 1",      "PPPPPPPPPPPPP");     CHECK(st == 0);
    st = CHECK_COLOR(C, 0, "#if 0",              "PPPPP");             CHECK(st == 0);
    /* indented # still triggers preproc — spaces before # stay normal */
    st = CHECK_COLOR(C, 0, "  #pragma once",     "..PPPPPPPPPPPP");    CHECK(st == 0);
    /* # not at start of line is NOT preproc */
    st = CHECK_COLOR(C, 0, "x = a # b",          ".........");           CHECK(st == 0);
}

static void
test_preprocessor_continuation(void)
{
    int st;

    /* trailing backslash → CS_PREPROC_CONT = 3 */
    st = CHECK_COLOR(C, 0, "#define M(x) \\",  "PPPPPPPPPPPPPP"); CHECK(st == 3);
    /* continuation line */
    st = CHECK_COLOR(C, 3, "    (x) * (x)",    "PPPPPPPPPPPPP");  CHECK(st == 0);
    /* continuation line itself ends with backslash → stays in PREPROC_CONT */
    st = CHECK_COLOR(C, 0, "#define A \\",     "PPPPPPPPPPP"); CHECK(st == 3);
    st = CHECK_COLOR(C, 3, "    1 + \\",       "PPPPPPPPP");     CHECK(st == 3);
    st = CHECK_COLOR(C, 3, "    2",            "PPPPP");         CHECK(st == 0);
}

/* ---- mixed lines -------------------------------------------------------- */

static void
test_mixed(void)
{
    int st;

    /* declaration with comment */
    st = CHECK_COLOR(C, 0, "int n = 0; /* count */",
                           "KKK.....N..CCCCCCCCCCC");
    CHECK(st == 0);

    /* string followed by comment */
    st = CHECK_COLOR(C, 0, "\"s\"; // note",
                           "SSS..CCCCCCC");
    CHECK(st == 0);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keywords();
    test_keywords_cpp_only();
    test_line_comment();
    test_block_comment_single_line();
    test_block_comment_multiline();
    test_strings();
    test_char_literals();
    test_numbers();
    test_preprocessor();
    test_preprocessor_continuation();
    test_mixed();
    SUMMARY();
}
