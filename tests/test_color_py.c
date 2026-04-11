/*
 * test_color_py.c - unit tests for the Python syntax colorizer.
 *
 * Cross-line states (mirrors color_py.c):
 *   0  PY_NORMAL
 *   1  PY_TRIPLE_DQ  (inside """)
 *   2  PY_TRIPLE_SQ  (inside ''')
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_py;
#define PY colorizer_py.colorize

/* ---- keywords ----------------------------------------------------------- */

static void
test_keywords(void)
{
    int st;

    st = CHECK_COLOR(PY, 0, "if",       "KK");     CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "elif",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "else",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "for",      "KKK");    CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "while",    "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "def",      "KKK");    CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "class",    "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "return",   "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "import",   "KKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "and",      "KKK");    CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "or",       "KK");     CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "not",      "KKK");    CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "True",     "KKKK");   CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "False",    "KKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "None",     "KKKK");   CHECK(st == 0);
    /* identifier that is NOT a keyword */
    st = CHECK_COLOR(PY, 0, "foo",      "...");    CHECK(st == 0);
    /* keyword in context */
    st = CHECK_COLOR(PY, 0, "if x:",    "KK...");  CHECK(st == 0);
}

/* ---- line comment ------------------------------------------------------- */

static void
test_comment(void)
{
    int st;

    st = CHECK_COLOR(PY, 0, "# comment",     "CCCCCCCCC"); CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "#",             "C");         CHECK(st == 0);
    /* comment after code */
    st = CHECK_COLOR(PY, 0, "x = 1  # note", "....N..CCCCCC"); CHECK(st == 0);
}

/* ---- strings ------------------------------------------------------------ */

static void
test_single_quoted(void)
{
    int st;

    st = CHECK_COLOR(PY, 0, "'hi'",        "SSSS");  CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "'a\\'b'",     "SSSSSS"); CHECK(st == 0);
    /* single-quoted in assignment */
    st = CHECK_COLOR(PY, 0, "x = 'ok'",   "....SSSS"); CHECK(st == 0);
}

static void
test_double_quoted(void)
{
    int st;

    st = CHECK_COLOR(PY, 0, "\"hi\"",      "SSSS");  CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "\"a\\\"b\"",  "SSSSSS"); CHECK(st == 0);
}

static void
test_string_prefixes(void)
{
    int st;

    /* r"..." raw string */
    st = CHECK_COLOR(PY, 0, "r\"raw\"",    "SSSSSS"); CHECK(st == 0);
    /* b"..." bytes */
    st = CHECK_COLOR(PY, 0, "b\"bytes\"",  "SSSSSSSS"); CHECK(st == 0);
    /* f"..." f-string */
    st = CHECK_COLOR(PY, 0, "f\"fmt\"",    "SSSSSS");  CHECK(st == 0);
    /* u"..." unicode */
    st = CHECK_COLOR(PY, 0, "u\"uni\"",    "SSSSSS");  CHECK(st == 0);
    /* rb"..." combined */
    st = CHECK_COLOR(PY, 0, "rb\"rb\"",    "SSSSSS"); CHECK(st == 0);
    /* prefix followed by non-quote: treated as identifier */
    st = CHECK_COLOR(PY, 0, "bar",         "...");    CHECK(st == 0);
}

static void
test_triple_dq(void)
{
    int st;

    /* open triple-quoted string: state → PY_TRIPLE_DQ = 1 */
    st = CHECK_COLOR(PY, 0, "x = \"\"\"start", "....SSSSSSSS"); CHECK(st == 1);
    /* continuation line — scan_triple_close finds no """, entire line is S */
    st = CHECK_COLOR(PY, 1, "middle line",     "SSSSSSSSSSS"); CHECK(st == 1);
    /* close — everything up to and including """ is string */
    st = CHECK_COLOR(PY, 1, "end\"\"\"",       "SSSSSS");      CHECK(st == 0);
    /* close with trailing code */
    st = CHECK_COLOR(PY, 1, "end\"\"\" + x",   "SSSSSS...."); CHECK(st == 0);

    /* triple on single line: open and close on same line */
    st = CHECK_COLOR(PY, 0, "\"\"\"inline\"\"\"", "SSSSSSSSSSSS"); CHECK(st == 0);
}

static void
test_triple_sq(void)
{
    int st;

    /* open triple single-quoted string: state → PY_TRIPLE_SQ = 2 */
    st = CHECK_COLOR(PY, 0, "'''start", "SSSSSSSS"); CHECK(st == 2);
    st = CHECK_COLOR(PY, 2, "content",  "SSSSSSS"); CHECK(st == 2);
    st = CHECK_COLOR(PY, 2, "end'''",   "SSSSSS");  CHECK(st == 0);
}

/* ---- decorators --------------------------------------------------------- */

static void
test_decorators(void)
{
    int st;

    st = CHECK_COLOR(PY, 0, "@property",        "PPPPPPPPP");    CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "@staticmethod",    "PPPPPPPPPPPPP"); CHECK(st == 0);
    /* decorator stops at '.': @app=P*4, .route(=normal*7, "/"=S*3, )=normal */
    st = CHECK_COLOR(PY, 0, "@app.route(\"/\")", "PPPP.......SSS."); CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(PY, 0, "42",       "NN");     CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "0xFF",     "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "0o77",     "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "0b101",    "NNNNN");  CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "3.14",     "NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "1e10",     "NNNN");   CHECK(st == 0);
    /* complex suffix */
    st = CHECK_COLOR(PY, 0, "1j",       "NN");     CHECK(st == 0);
    st = CHECK_COLOR(PY, 0, "2.5J",     "NNNN");   CHECK(st == 0);
    /* underscore separator */
    st = CHECK_COLOR(PY, 0, "1_000",    "NNNNN");  CHECK(st == 0);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keywords();
    test_comment();
    test_single_quoted();
    test_double_quoted();
    test_string_prefixes();
    test_triple_dq();
    test_triple_sq();
    test_decorators();
    test_numbers();
    SUMMARY();
}
