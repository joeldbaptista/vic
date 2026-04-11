/*
 * test_codepoint.c - unit tests for codepoint.c
 *
 * Tests:
 *   utf8_cell_width  — standalone; no struct editor needed
 *   cp_start         — snap interior byte to codepoint lead
 *   cp_next          — advance one codepoint
 *   cp_prev          — retreat one codepoint
 *   next_column      — column arithmetic (ASCII, tab, wide, control)
 *   get_column       — column of a buffer pointer
 *
 * Builds with: codepoint.c line.c utf8.c
 */
#include "testutil.h"
#include "codepoint.h"
#include "line.h"
#include <locale.h>
#include <string.h>

#define BUFSZ 256

static struct editor g;
static char buf[BUFSZ];

static void
init(const char *content)
{
    size_t n = strlen(content);
    memset(&g, 0, sizeof(g));
    memcpy(buf, content, n);
    g.text    = buf;
    g.end     = buf + n;
    g.tabstop = 8;
}

/* ---- utf8_cell_width ----------------------------------------------------- */

static void
test_cell_width_ascii(void)
{
    const char *s;

    s = "a"; CHECK(utf8_cell_width(s, s + 1) == 1);
    s = "Z"; CHECK(utf8_cell_width(s, s + 1) == 1);
    s = " "; CHECK(utf8_cell_width(s, s + 1) == 1);
}

static void
test_cell_width_multibyte(void)
{
    /* U+00E9 LATIN SMALL LETTER E WITH ACUTE: narrow (width 1) */
    const char e_acute[] = "\xc3\xa9";
    CHECK(utf8_cell_width(e_acute, e_acute + 2) == 1);

    /* U+4E2D CJK IDEOGRAPH 中: wide (width 2) */
    const char cjk[] = "\xe4\xb8\xad";
    CHECK(utf8_cell_width(cjk, cjk + 3) == 2);

    /* U+1F600 GRINNING FACE 😀: wide (width 2) */
    const char emoji[] = "\xf0\x9f\x98\x80";
    CHECK(utf8_cell_width(emoji, emoji + 4) == 2);
}

static void
test_cell_width_edge(void)
{
    const char *s = "a";

    /* empty span returns 1 */
    CHECK(utf8_cell_width(s, s) == 1);

    /* malformed lead byte falls through to return 1 */
    const char bad[] = "\x80\x80"; /* continuation as lead */
    CHECK(utf8_cell_width(bad, bad + 2) == 1);

    /* truncated 2-byte: only 1 byte available */
    const char trunc[] = "\xc3";
    CHECK(utf8_cell_width(trunc, trunc + 1) == 1);
}

/* ---- cp_start ------------------------------------------------------------ */

static void
test_cp_start(void)
{
    /* ASCII: every byte is its own codepoint start */
    init("abc\n");
    CHECK(cp_start(&g, buf + 0) == buf + 0);
    CHECK(cp_start(&g, buf + 1) == buf + 1);

    /* U+4E2D 中 (3 bytes) at offset 1: buf = "a\xe4\xb8\xad" "b\n" */
    init("a\xe4\xb8\xad" "b\n");
    CHECK(cp_start(&g, buf + 1) == buf + 1); /* lead byte */
    CHECK(cp_start(&g, buf + 2) == buf + 1); /* first continuation */
    CHECK(cp_start(&g, buf + 3) == buf + 1); /* second continuation */
    CHECK(cp_start(&g, buf + 4) == buf + 4); /* 'b', ASCII */
}

/* ---- cp_next / cp_prev --------------------------------------------------- */

static void
test_cp_next_ascii(void)
{
    init("abc\n");
    CHECK(cp_next(&g, buf + 0) == buf + 1);
    CHECK(cp_next(&g, buf + 1) == buf + 2);
    CHECK(cp_next(&g, buf + 2) == buf + 3);
    /* at end: clamp */
    CHECK(cp_next(&g, buf + 3) == buf + 4);
    CHECK(cp_next(&g, buf + 4) == buf + 4); /* = g->end */
}

static void
test_cp_next_multibyte(void)
{
    /* "a中b\n" = a(1) + 中(3) + b(1) + \n(1) = 6 bytes */
    init("a\xe4\xb8\xad" "b\n");
    CHECK(cp_next(&g, buf + 0) == buf + 1); /* skip 'a' */
    CHECK(cp_next(&g, buf + 1) == buf + 4); /* skip 中 (3 bytes) */
    CHECK(cp_next(&g, buf + 4) == buf + 5); /* skip 'b' */
}

static void
test_cp_prev_ascii(void)
{
    init("abc\n");
    CHECK(cp_prev(&g, buf + 3) == buf + 2);
    CHECK(cp_prev(&g, buf + 2) == buf + 1);
    CHECK(cp_prev(&g, buf + 1) == buf + 0);
    /* at start: clamp */
    CHECK(cp_prev(&g, buf + 0) == buf + 0);
}

static void
test_cp_prev_multibyte(void)
{
    /* "a中b\n" */
    init("a\xe4\xb8\xad" "b\n");
    CHECK(cp_prev(&g, buf + 5) == buf + 4); /* back over 'b' */
    CHECK(cp_prev(&g, buf + 4) == buf + 1); /* back over 中 */
    CHECK(cp_prev(&g, buf + 1) == buf + 0); /* back over 'a' */
}

/* ---- next_column --------------------------------------------------------- */

static void
test_next_column_ascii(void)
{
    init("abc\n");
    /* each ASCII char occupies 1 column; next_column(co) = co + 1 */
    CHECK(next_column(&g, buf + 0, 0) == 1);
    CHECK(next_column(&g, buf + 0, 5) == 6);
}

static void
test_next_column_tab(void)
{
    init("\tabc\n");
    g.tabstop = 8;
    /* tab at col 0 → col 8 */
    CHECK(next_column(&g, buf + 0, 0) == 8);
    /* tab at col 7 → col 8 */
    CHECK(next_column(&g, buf + 0, 7) == 8);
    /* tab at col 8 → col 16 */
    CHECK(next_column(&g, buf + 0, 8) == 16);
}

static void
test_next_column_wide(void)
{
    /* 中 is 3 bytes, width 2 */
    init("\xe4\xb8\xad\n");
    CHECK(next_column(&g, buf + 0, 0) == 2);
    CHECK(next_column(&g, buf + 0, 3) == 5);
}

static void
test_next_column_control(void)
{
    /* control chars display as ^X (2 columns) */
    init("\x01\n");
    CHECK(next_column(&g, buf + 0, 0) == 2);
}

/* ---- get_column ---------------------------------------------------------- */

static void
test_get_column(void)
{
    init("abc\n");
    CHECK(get_column(&g, buf + 0) == 0);
    CHECK(get_column(&g, buf + 1) == 1);
    CHECK(get_column(&g, buf + 2) == 2);
    CHECK(get_column(&g, buf + 3) == 3);

    /* tab expands: "\tabc\n", tabstop=8 */
    init("\tabc\n");
    g.tabstop = 8;
    CHECK(get_column(&g, buf + 0) == 0); /* before '\t' */
    CHECK(get_column(&g, buf + 1) == 8); /* after '\t' */
    CHECK(get_column(&g, buf + 2) == 9); /* 'a' */
}

static void
test_get_column_wide(void)
{
    /* "a中b\n": 'a'=col0, 中=col1..2, 'b'=col3 */
    init("a\xe4\xb8\xad" "b\n");
    CHECK(get_column(&g, buf + 0) == 0); /* 'a' */
    CHECK(get_column(&g, buf + 1) == 1); /* 中 */
    CHECK(get_column(&g, buf + 4) == 3); /* 'b' (中 is 2 wide) */
    CHECK(get_column(&g, buf + 5) == 4); /* '\n' */
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    /* wcwidth() returns correct widths only with a UTF-8 locale. */
    setlocale(LC_ALL, "");
    test_cell_width_ascii();
    test_cell_width_multibyte();
    test_cell_width_edge();
    test_cp_start();
    test_cp_next_ascii();
    test_cp_next_multibyte();
    test_cp_prev_ascii();
    test_cp_prev_multibyte();
    test_next_column_ascii();
    test_next_column_tab();
    test_next_column_wide();
    test_next_column_control();
    test_get_column();
    test_get_column_wide();
    SUMMARY();
}
