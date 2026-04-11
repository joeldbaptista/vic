/*
 * test_color_sql.c - unit tests for the SQL syntax colorizer.
 *
 * Cross-line states (mirrors color_sql.c):
 *   0  SQL_NORMAL
 *   1  SQL_BLOCK_CMT       (inside slash-star comment)
 *   2  SQL_BLOCK_CMT_STAR  (inside comment, last byte was '*')
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_sql;
#define SQL colorizer_sql.colorize

/* ---- keywords (case-insensitive) --------------------------------------- */

static void
test_keywords(void)
{
    int st;

    /* uppercase */
    st = CHECK_COLOR(SQL, 0, "SELECT",    "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "FROM",      "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "WHERE",     "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "INSERT",    "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "UPDATE",    "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "DELETE",    "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "CREATE",    "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "TABLE",     "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "INDEX",     "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "JOIN",      "KKKK");   CHECK(st == 0);
    /* lowercase — SQL is case-insensitive */
    st = CHECK_COLOR(SQL, 0, "select",    "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "from",      "KKKK");   CHECK(st == 0);
    /* mixed case */
    st = CHECK_COLOR(SQL, 0, "Select",    "KKKKKK"); CHECK(st == 0);
    /* identifier that is NOT a keyword */
    st = CHECK_COLOR(SQL, 0, "my_table",  "........"); CHECK(st == 0);
}

/* ---- line comment ------------------------------------------------------- */

static void
test_line_comment(void)
{
    int st;

    st = CHECK_COLOR(SQL, 0, "-- comment",   "CCCCCCCCCC"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "--",           "CC");         CHECK(st == 0);
    /* comment following a statement — '1' before space is colored as number */
    st = CHECK_COLOR(SQL, 0, "x = 1 -- note", "....N.CCCCCCC"); CHECK(st == 0);
    /* single dash alone is NOT a comment */
    st = CHECK_COLOR(SQL, 0, "a - b",         ".....");        CHECK(st == 0);
}

/* ---- block comment ------------------------------------------------------ */

static void
test_block_comment_single_line(void)
{
    int st;

    st = CHECK_COLOR(SQL, 0, "/* hi */",  "CCCCCCCC"); CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "/**/",      "CCCC");     CHECK(st == 0);
    /* block comment then code */
    st = CHECK_COLOR(SQL, 0, "/* x */ y", "CCCCCCC.."); CHECK(st == 0);
}

static void
test_block_comment_multiline(void)
{
    int st;

    /* open: state → SQL_BLOCK_CMT = 1 */
    st = CHECK_COLOR(SQL, 0, "/* open",      "CCCCCCC"); CHECK(st == 1);
    /* continuation */
    st = CHECK_COLOR(SQL, 1, "middle",       "CCCCCC");  CHECK(st == 1);
    /* close: state → SQL_NORMAL = 0 */
    st = CHECK_COLOR(SQL, 1, "end */",       "CCCCCC");  CHECK(st == 0);

    /* line ending with '*': state → SQL_BLOCK_CMT_STAR = 2 */
    st = CHECK_COLOR(SQL, 0, "/* x *",       "CCCCCC");  CHECK(st == 2);
    /* next char '/' closes: only the '/' is comment, rest is normal */
    st = CHECK_COLOR(SQL, 2, "/ rest",       "C.....");  CHECK(st == 0);
}

/* ---- string literals ---------------------------------------------------- */

static void
test_single_quoted(void)
{
    int st;

    st = CHECK_COLOR(SQL, 0, "'value'",      "SSSSSSS"); CHECK(st == 0);
    /* '' escaped quote inside string */
    st = CHECK_COLOR(SQL, 0, "'it''s'",      "SSSSSSS"); CHECK(st == 0);
    /* string in expression */
    st = CHECK_COLOR(SQL, 0, "x = 'ok'",    "....SSSS"); CHECK(st == 0);
}

static void
test_double_quoted_identifier(void)
{
    int st;

    /* double-quoted identifier (ANSI SQL) */
    st = CHECK_COLOR(SQL, 0, "\"my col\"",   "SSSSSSSS"); CHECK(st == 0);
    /* "" escaped double-quote inside */
    st = CHECK_COLOR(SQL, 0, "\"a\"\"b\"",   "SSSSSS");   CHECK(st == 0);
}

static void
test_backtick_identifier(void)
{
    int st;

    /* backtick-quoted identifier (MySQL) */
    st = CHECK_COLOR(SQL, 0, "`col`",        "SSSSS");    CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "`my col`",     "SSSSSSSS"); CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(SQL, 0, "42",       "NN");     CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "0xFF",     "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "3.14",     "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(SQL, 0, "1e5",      "NNN");    CHECK(st == 0);
    /* number in expression */
    st = CHECK_COLOR(SQL, 0, "n = 0",    "....N");  CHECK(st == 0);
}

/* ---- mixed query -------------------------------------------------------- */

static void
test_mixed(void)
{
    int st;

    /* SELECT x FROM t -- comment */
    st = CHECK_COLOR(SQL, 0,
                     "SELECT x FROM t",
                     "KKKKKK...KKKK..");
    CHECK(st == 0);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keywords();
    test_line_comment();
    test_block_comment_single_line();
    test_block_comment_multiline();
    test_single_quoted();
    test_double_quoted_identifier();
    test_backtick_identifier();
    test_numbers();
    test_mixed();
    SUMMARY();
}
