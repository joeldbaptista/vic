/*
 * test_color_sh.c - unit tests for the shell syntax colorizer.
 *
 * Cross-line states (mirrors color_sh.c):
 *   0  SH_NORMAL
 *   1  SH_DQUOTE  (inside a double-quoted string continuing across lines)
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_sh;
#define SH colorizer_sh.colorize

/* ---- keywords ----------------------------------------------------------- */

static void
test_keywords(void)
{
    int st;

    st = CHECK_COLOR(SH, 0, "if",       "KK");     CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "then",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "else",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "elif",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "fi",       "KK");     CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "for",      "KKK");    CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "while",    "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "do",       "KK");     CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "done",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "case",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "esac",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "function", "KKKKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "return",   "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "local",    "KKKKK");  CHECK(st == 0);
    /* identifier that is NOT a keyword */
    st = CHECK_COLOR(SH, 0, "echo",     "....");   CHECK(st == 0);
    /* keyword followed by space stays keyword */
    st = CHECK_COLOR(SH, 0, "if x",     "KK..");   CHECK(st == 0);
}

/* ---- comment ------------------------------------------------------------ */

static void
test_comment(void)
{
    int st;

    st = CHECK_COLOR(SH, 0, "# comment",     "CCCCCCCCC"); CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "#",             "C");         CHECK(st == 0);
    /* comment following code — '1' before space is colored as number */
    st = CHECK_COLOR(SH, 0, "x=1 # note",    "..N.CCCCCC"); CHECK(st == 0);
}

/* ---- variable expansion ------------------------------------------------- */

static void
test_variables(void)
{
    int st;

    /* plain $VAR */
    st = CHECK_COLOR(SH, 0, "$FOO",     "PPPP");    CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "$x",       "PP");      CHECK(st == 0);
    /* ${VAR} brace expansion */
    st = CHECK_COLOR(SH, 0, "${FOO}",   "PPPPPP"); CHECK(st == 0);
    /* $(cmd) command substitution */
    st = CHECK_COLOR(SH, 0, "$(pwd)",   "PPPPPP");  CHECK(st == 0);
    /* $ not followed by word char: just the $ is colored */
    st = CHECK_COLOR(SH, 0, "a$",       ".P");      CHECK(st == 0);
    /* variable in context */
    st = CHECK_COLOR(SH, 0, "x=$HOME",  "..PPPPP"); CHECK(st == 0);
}

/* ---- single-quoted strings ---------------------------------------------- */

static void
test_single_quoted(void)
{
    int st;

    /* no expansion inside single quotes */
    st = CHECK_COLOR(SH, 0, "'hello'",      "SSSSSSS"); CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "'no $expand'", "SSSSSSSSSSSS"); CHECK(st == 0);
    /* single quotes in context */
    st = CHECK_COLOR(SH, 0, "x='val'",      "..SSSSS"); CHECK(st == 0);
}

/* ---- double-quoted strings ---------------------------------------------- */

static void
test_double_quoted(void)
{
    int st;

    /* plain double-quoted: all ATTR_STRING */
    st = CHECK_COLOR(SH, 0, "\"hello\"",        "SSSSSSS"); CHECK(st == 0);
    /* variable inside double-quoted: ATTR_PREPROC for the $.. part */
    st = CHECK_COLOR(SH, 0, "\"$NAME\"",        "SPPPPPS");  CHECK(st == 0);
    /* ${VAR} inside double-quoted */
    st = CHECK_COLOR(SH, 0, "\"${X}\"",         "SPPPPS");   CHECK(st == 0);
    /* backslash escape inside (6 chars: " a \ n b ") */
    st = CHECK_COLOR(SH, 0, "\"a\\nb\"",        "SSSSSS");  CHECK(st == 0);
    /* escaped quote at end of line — the \" must be colored STRING, not NORMAL */
    st = CHECK_COLOR(SH, 0, "\"a\\\"",          "SSSS");    CHECK(st == 1);
}

static void
test_double_quoted_multiline(void)
{
    int st;

    /* open double-quoted without close: state → SH_DQUOTE = 1 */
    st = CHECK_COLOR(SH, 0, "\"open",       "SSSSS");  CHECK(st == 1);
    /* continuation: variable inside cross-line string */
    st = CHECK_COLOR(SH, 1, "$VAR",         "PPPP");   CHECK(st == 1);
    /* plain continuation */
    st = CHECK_COLOR(SH, 1, "more text",    "SSSSSSSSS"); CHECK(st == 1);
    /* close */
    st = CHECK_COLOR(SH, 1, "close\"",      "SSSSSS");    CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    /* integer literal not followed by a word char */
    st = CHECK_COLOR(SH, 0, "42",    "NN");    CHECK(st == 0);
    st = CHECK_COLOR(SH, 0, "0",     "N");     CHECK(st == 0);
    /* number followed by non-word: still colored */
    st = CHECK_COLOR(SH, 0, "42;",   "NN.");   CHECK(st == 0);
    /* number adjacent to identifier: NOT colored (looks like word) */
    st = CHECK_COLOR(SH, 0, "a1b",   "...");   CHECK(st == 0);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keywords();
    test_comment();
    test_variables();
    test_single_quoted();
    test_double_quoted();
    test_double_quoted_multiline();
    test_numbers();
    SUMMARY();
}
