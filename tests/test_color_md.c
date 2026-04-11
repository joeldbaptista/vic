/*
 * test_color_md.c - unit tests for the Markdown syntax colorizer.
 *
 * Cross-line states (mirrors color_md.c):
 *   0  MD_NORMAL
 *   1  MD_CODE_BLOCK  (inside a fenced code block)
 *   2  MD_HTML_CMT    (inside an HTML comment)
 *
 * ATTR mapping used by the colorizer (see color_md.c header):
 *   ATTR_KEYWORD  — headings, bold, horizontal rules
 *   ATTR_COMMENT  — blockquotes, HTML comments
 *   ATTR_STRING   — inline code, fenced code block content
 *   ATTR_PREPROC  — italic, link label [...]
 *   ATTR_NUMBER   — link URL (...)
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_md;
#define MD colorizer_md.colorize

/* ---- headings ----------------------------------------------------------- */

static void
test_headings(void)
{
    int st;

    st = CHECK_COLOR(MD, 0, "# Title",     "KKKKKKK");   CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "## Section",  "KKKKKKKKKK"); CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "### Sub",     "KKKKKKK");   CHECK(st == 0);
    /* # not followed by space: not a heading */
    st = CHECK_COLOR(MD, 0, "#nospace",    "........");  CHECK(st == 0);
}

/* ---- blockquote --------------------------------------------------------- */

static void
test_blockquote(void)
{
    int st;

    st = CHECK_COLOR(MD, 0, "> quoted",    "CCCCCCCC"); CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, ">",           "C");        CHECK(st == 0);
}

/* ---- horizontal rule ---------------------------------------------------- */

static void
test_horiz_rule(void)
{
    int st;

    st = CHECK_COLOR(MD, 0, "---",         "KKK"); CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "***",         "KKK"); CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "___",         "KKK"); CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "- - -",       "KKKKK"); CHECK(st == 0);
    /* fewer than 3 chars: not a rule */
    st = CHECK_COLOR(MD, 0, "--",          ".."); CHECK(st == 0);
}

/* ---- fenced code block -------------------------------------------------- */

static void
test_fenced_code(void)
{
    int st;

    /* opening fence: colored as ATTR_STRING, state → MD_CODE_BLOCK = 1 */
    st = CHECK_COLOR(MD, 0, "```",         "SSS");        CHECK(st == 1);
    st = CHECK_COLOR(MD, 0, "```c",        "SSSS");       CHECK(st == 1);
    /* content line: all ATTR_STRING */
    st = CHECK_COLOR(MD, 1, "int x = 0;",  "SSSSSSSSSS"); CHECK(st == 1);
    /* closing fence: ATTR_STRING, state → MD_NORMAL = 0 */
    st = CHECK_COLOR(MD, 1, "```",         "SSS");        CHECK(st == 0);

    /* tilde fence variant */
    st = CHECK_COLOR(MD, 0, "~~~",         "SSS");        CHECK(st == 1);
    st = CHECK_COLOR(MD, 1, "content",     "SSSSSSS");    CHECK(st == 1);
    st = CHECK_COLOR(MD, 1, "~~~",         "SSS");        CHECK(st == 0);
}

/* ---- inline code -------------------------------------------------------- */

static void
test_inline_code(void)
{
    int st;

    st = CHECK_COLOR(MD, 0, "`code`",      "SSSSSS");    CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "x `y` z",    "..SSS..");   CHECK(st == 0);
}

/* ---- bold and italic ---------------------------------------------------- */

static void
test_bold(void)
{
    int st;

    st = CHECK_COLOR(MD, 0, "**bold**",    "KKKKKKKK");  CHECK(st == 0);
    st = CHECK_COLOR(MD, 0, "__bold__",    "KKKKKKKK");  CHECK(st == 0);
}

static void
test_italic(void)
{
    int st;

    st = CHECK_COLOR(MD, 0, "*italic*",    "PPPPPPPP");  CHECK(st == 0);
}

/* ---- links -------------------------------------------------------------- */

static void
test_links(void)
{
    int st;

    /* [label](url): label → ATTR_PREPROC, url → ATTR_NUMBER */
    st = CHECK_COLOR(MD, 0, "[hi](url)",   "PPPPNNNNN"); CHECK(st == 0);
    /* label only, no url part */
    st = CHECK_COLOR(MD, 0, "[ref]",       "PPPPP");     CHECK(st == 0);
}

/* ---- HTML comment ------------------------------------------------------- */

static void
test_html_comment(void)
{
    int st;

    /* single-line comment */
    st = CHECK_COLOR(MD, 0, "<!-- hi -->",  "CCCCCCCCCCC"); CHECK(st == 0);
    /* open comment: state → MD_HTML_CMT = 2 */
    st = CHECK_COLOR(MD, 0, "<!-- open",   "CCCCCCCCC"); CHECK(st == 2);
    /* continuation */
    st = CHECK_COLOR(MD, 2, "middle",      "CCCCCC");    CHECK(st == 2);
    /* close */
    st = CHECK_COLOR(MD, 2, "end -->",     "CCCCCCC");   CHECK(st == 0);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_headings();
    test_blockquote();
    test_horiz_rule();
    test_fenced_code();
    test_inline_code();
    test_bold();
    test_italic();
    test_links();
    test_html_comment();
    SUMMARY();
}
