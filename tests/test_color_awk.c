/*
 * test_color_awk.c - unit tests for the awk syntax colorizer and for the
 * file-name matching that selects it.
 *
 * Nothing spans lines in awk, so every line returns state 0.
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_awk;
#define AWK colorizer_awk.colorize

/* ---- keywords ----------------------------------------------------------- */

static void
test_keywords(void)
{
    int st;

    st = CHECK_COLOR(AWK, 0, "BEGIN { FS = \":\" }", "KKKKK........SSS..");
    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "END { print n }",    "KKK...KKKKK....");  CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "BEGINFILE",          "KKKKKKKKK");        CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "function f(a) {",    "KKKKKKKK.......");  CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "for (k in a)",       "KKK....KK...");     CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "next",               "KKKK");             CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "switch (x) {",       "KKKKKK......");     CHECK(st == 0);
    /* built-in functions are reserved words, POSIX and gawk alike */
    st = CHECK_COLOR(AWK, 0, "n = length(s)",      "....KKKKKK...");    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "gensub(a, b, c)",    "KKKKKK.........");  CHECK(st == 0);
    /* built-in variables are ordinary names */
    st = CHECK_COLOR(AWK, 0, "NR > 1",             ".....N");           CHECK(st == 0);
    /* a name that only contains a keyword is not one */
    st = CHECK_COLOR(AWK, 0, "printer = 1",        "..........N");      CHECK(st == 0);
}

/* ---- fields ------------------------------------------------------------- */

static void
test_fields(void)
{
    int st;

    st = CHECK_COLOR(AWK, 0, "print $1",        "KKKKK.PP");        CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "print $NF",       "KKKKK.PPP");       CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "x = $(i+1)",      "....PPPPPP");      CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "$0 = \"a\"",      "PP...SSS");        CHECK(st == 0);
}

/* ---- strings ------------------------------------------------------------ */

static void
test_strings(void)
{
    int st;

    st = CHECK_COLOR(AWK, 0, "\"hi\"",          "SSSS");            CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "\"a\\\"b\"",      "SSSSSS");          CHECK(st == 0);
    /* '#' and '/' inside a string are part of it */
    st = CHECK_COLOR(AWK, 0, "s = \"#/\"",      "....SSSS");        CHECK(st == 0);
    /* an unterminated string does not carry over */
    st = CHECK_COLOR(AWK, 0, "\"open",          "SSSSS");           CHECK(st == 0);
}

/* ---- regex literals ----------------------------------------------------- */

static void
test_regex(void)
{
    int st;

    /* a pattern at the head of a line */
    st = CHECK_COLOR(AWK, 0, "/^#/ { next }",   "SSSS...KKKK..");   CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "!/x/",            ".SSS");            CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "$1 ~ /a+b/",      "PP...SSSSS");      CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "sub(/ +$/, \"\")", "KKK.SSSSS..SS.");  CHECK(st == 0);
    /* an escaped slash and a slash in a bracket expression do not close */
    st = CHECK_COLOR(AWK, 0, "/a\\/b/",         "SSSSSS");          CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "/[/]x/",          "SSSSSS");          CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "/[]/]/",          "SSSSSS");          CHECK(st == 0);
    /* a regex after a keyword that expects an operand */
    st = CHECK_COLOR(AWK, 0, "if (/x/) n++",    "KK..SSS.....");    CHECK(st == 0);
    /* a regex as a range pattern */
    st = CHECK_COLOR(AWK, 0, "/a/,/b/",         "SSS.SSS");         CHECK(st == 0);
}

static void
test_division(void)
{
    int st;

    /* '/' after an operand divides */
    st = CHECK_COLOR(AWK, 0, "x = a / b / c",   "............."); CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "x = 6 / 2",       "....N...N");     CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "x = $1 / 2",      "....PP...N");    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "x = (a) / (b) / c", ".................");
    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "x = a[i] / n / 2", "...............N");
    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "s = \"a\" / n / 2", "....SSS.......N");
    CHECK(st == 0);
    /* a postfix ++ keeps the operand */
    st = CHECK_COLOR(AWK, 0, "y = i++ / n / 2", "..............N");
    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "x /= 2",          ".....N");        CHECK(st == 0);
    /* a '/' with no close on the line is a division */
    st = CHECK_COLOR(AWK, 0, "print length / 2", "KKKKK.KKKKKK...N");
    CHECK(st == 0);
}

/* ---- comments ----------------------------------------------------------- */

static void
test_comments(void)
{
    int st;

    st = CHECK_COLOR(AWK, 0, "# note",          "CCCCCC");          CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "n++ # count",     "....CCCCCCC");     CHECK(st == 0);
    /* '#' inside a regex literal is part of it */
    st = CHECK_COLOR(AWK, 0, "/#/ # c",         "SSS.CCC");         CHECK(st == 0);
}

/* ---- gawk directives ---------------------------------------------------- */

static void
test_directives(void)
{
    int st;

    st = CHECK_COLOR(AWK, 0, "@include \"lib.awk\"", "PPPPPPPP.SSSSSSSSS");
    CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "@load \"ordchr\"",     "PPPPP.SSSSSSSS");
    CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(AWK, 0, "42",              "NN");              CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "3.14",            "NNNN");            CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "1e10",            "NNNN");            CHECK(st == 0);
    st = CHECK_COLOR(AWK, 0, "0x1F",            "NNNN");            CHECK(st == 0);
    /* a digit inside a name is part of the name */
    st = CHECK_COLOR(AWK, 0, "a1 = 2",          ".....N");          CHECK(st == 0);
}

/* ---- file name matching ------------------------------------------------- */

static void
test_colorizer_find(void)
{
    CHECK(colorizer_find("script.awk") == &colorizer_awk);
    CHECK(colorizer_find("script.gawk") == &colorizer_awk);
    CHECK(colorizer_find("/usr/share/awk/join.awk") == &colorizer_awk);
    /* case is ignored */
    CHECK(colorizer_find("REPORT.AWK") == &colorizer_awk);
    /* other languages still resolve */
    CHECK(colorizer_find("awk") == NULL);
    CHECK(colorizer_find("run.sh") != &colorizer_awk);
    CHECK(colorizer_find("Makefile") != &colorizer_awk);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keywords();
    test_fields();
    test_strings();
    test_regex();
    test_division();
    test_comments();
    test_directives();
    test_numbers();
    test_colorizer_find();
    SUMMARY();
}
