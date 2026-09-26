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
 *   csi_len          — length and kind of a CSI escape sequence
 *   esc_skip         — step over an escape run in 'color-escape' mode
 *   esc_snap_fwd     — leave an escape run forwards (cursor placement)
 *   esc_snap_bwd     — leave an escape run backwards (cursor placement)
 *
 * The last group also checks that get_column counts an escape run as zero
 * columns while 'color-escape' is on and as its caret notation while it is
 * off, and that cp_next/cp_prev keep stepping exactly one codepoint in both
 * modes — format_line depends on cp_next(p) - p being a byte length.
 *
 * Builds with: codepoint.c line.c utf8.c compat.c
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

/* ---- csi_len ------------------------------------------------------------- */

static void
test_csi_len_sgr(void)
{
    const char *s;
    int sgr;

    /* Plain colour set, and the two spellings of a full reset. */
    sgr = 0; s = "\033[31mx";  CHECK(csi_len(s, s + 6, &sgr) == 5); CHECK(sgr == 1);
    sgr = 0; s = "\033[m";     CHECK(csi_len(s, s + 3, &sgr) == 3); CHECK(sgr == 1);
    sgr = 0; s = "\033[0m";    CHECK(csi_len(s, s + 4, &sgr) == 4); CHECK(sgr == 1);

    /* Multiple parameters, and a 256-colour sequence. */
    sgr = 0; s = "\033[1;38;5;245m";
    CHECK(csi_len(s, s + 13, &sgr) == 13);
    CHECK(sgr == 1);
}

static void
test_csi_len_non_sgr(void)
{
    const char *s;
    int sgr;

    /* Recognised as CSI, but not SGR: these must never be passed through. */
    sgr = 1; s = "\033[2J";  CHECK(csi_len(s, s + 4, &sgr) == 4); CHECK(sgr == 0);
    sgr = 1; s = "\033[H";   CHECK(csi_len(s, s + 3, &sgr) == 3); CHECK(sgr == 0);
    sgr = 1; s = "\033[10;5H"; CHECK(csi_len(s, s + 7, &sgr) == 7); CHECK(sgr == 0);
}

static void
test_csi_len_rejects(void)
{
    const char *s;

    /* Not an escape at all. */
    s = "abc";       CHECK(csi_len(s, s + 3, NULL) == 0);
    /* ESC not followed by '['. */
    s = "\033]0;t";  CHECK(csi_len(s, s + 5, NULL) == 0);
    /* Truncated: no final byte before end. */
    s = "\033[31";   CHECK(csi_len(s, s + 4, NULL) == 0);
    /* Bare ESC at the very end. */
    s = "\033";      CHECK(csi_len(s, s + 1, NULL) == 0);
    /* A newline is in none of the byte classes, so the scan stops there. */
    s = "\033[31\nm"; CHECK(csi_len(s, s + 6, NULL) == 0);
}

/* ---- color-escape mode: escapes occupy no column ------------------------- */

static void
test_esc_skip(void)
{
    /* "ab<esc>cd": esc_skip steps over a run start, and leaves text alone. */
    init("ab\033[31mcd\n");
    g.color_escape = 1;
    CHECK(esc_skip(&g, buf + 0) == buf + 0); /* 'a' — not an escape */
    CHECK(esc_skip(&g, buf + 2) == buf + 7); /* past "\033[31m" */

    /* Adjacent sequences are absorbed into one run. */
    init("a\033[0m\033[1;31mb\n");
    g.color_escape = 1;
    CHECK(esc_skip(&g, buf + 1) == buf + 12); /* past both sequences */

    /* With the mode off nothing is skipped. */
    init("ab\033[31mcd\n");
    g.color_escape = 0;
    CHECK(esc_skip(&g, buf + 2) == buf + 2);
}

static void
test_get_column_escape(void)
{
    /* "ab\033[31mcd\033[mef\n" renders as "abcdef" — six columns. */
    init("ab\033[31mcd\033[mef\n");
    g.color_escape = 1;
    CHECK(get_column(&g, buf + 0) == 0);  /* 'a' */
    CHECK(get_column(&g, buf + 1) == 1);  /* 'b' */
    CHECK(get_column(&g, buf + 7) == 2);  /* 'c', after a 5-byte escape */
    CHECK(get_column(&g, buf + 8) == 3);  /* 'd' */
    CHECK(get_column(&g, buf + 12) == 4); /* 'e', after a 3-byte escape */
    CHECK(get_column(&g, buf + 13) == 5); /* 'f' */

    /* With the mode off the same bytes show as "^[[31m" and so on. */
    init("ab\033[31mcd\033[mef\n");
    g.color_escape = 0;
    CHECK(get_column(&g, buf + 7) == 8);  /* 'c': 2 text + 6 for "^[[31m" */
}

static void
test_cp_next_prev_unchanged_by_mode(void)
{
    /*
     * cp_next(p) - p must stay the byte length of the codepoint at p even
     * with the mode on: format_line relies on that to copy one character.
     */
    init("ab\033[31mcd\n");
    g.color_escape = 1;
    CHECK(cp_next(&g, buf + 1) == buf + 2); /* 'b' -> the ESC byte */
    CHECK(cp_next(&g, buf + 2) == buf + 3); /* ESC -> '[' */
    CHECK(cp_prev(&g, buf + 7) == buf + 6); /* 'c' -> the escape's 'm' */
}

static void
test_esc_snap_fwd(void)
{
    char *p;

    init("ab\033[31mcd\033[mef\n");
    g.color_escape = 1;
    CHECK(esc_snap_fwd(&g, buf + 0) == buf + 0); /* 'a' — ordinary text */
    CHECK(esc_snap_fwd(&g, buf + 2) == buf + 7); /* on the run -> 'c' */
    CHECK(esc_snap_fwd(&g, buf + 4) == buf + 7); /* inside the run -> 'c' */
    CHECK(esc_snap_fwd(&g, buf + 9) == buf + 12); /* second run -> 'e' */

    /* Adjacent sequences count as one run. */
    init("a\033[0m\033[1;31mb\n");
    g.color_escape = 1;
    CHECK(esc_snap_fwd(&g, buf + 3) == buf + 12); /* inside the first -> 'b' */

    /* With the mode off nothing moves. */
    init("ab\033[31mcd\n");
    g.color_escape = 0;
    CHECK(esc_snap_fwd(&g, buf + 4) == buf + 4);

    /* Stepping as dot_right does yields the six visible columns in order. */
    init("ab\033[31mcd\033[mef\n");
    g.color_escape = 1;
    p = esc_snap_fwd(&g, buf);
    CHECK(get_column(&g, p) == 0);
    p = esc_snap_fwd(&g, cp_next(&g, p)); CHECK(get_column(&g, p) == 1);
    p = esc_snap_fwd(&g, cp_next(&g, p)); CHECK(get_column(&g, p) == 2);
    p = esc_snap_fwd(&g, cp_next(&g, p)); CHECK(get_column(&g, p) == 3);
    p = esc_snap_fwd(&g, cp_next(&g, p)); CHECK(get_column(&g, p) == 4);
    p = esc_snap_fwd(&g, cp_next(&g, p)); CHECK(get_column(&g, p) == 5);
    CHECK(p == buf + 13); /* 'f' */
}

static void
test_esc_snap_bwd(void)
{
    char *p;

    init("ab\033[31mcd\033[mef\n");
    g.color_escape = 1;
    CHECK(esc_snap_bwd(&g, buf + 8) == buf + 8);  /* 'd' — ordinary text */
    CHECK(esc_snap_bwd(&g, buf + 9) == buf + 8);  /* on the run -> 'd' */
    CHECK(esc_snap_bwd(&g, buf + 10) == buf + 8); /* inside the run -> 'd' */
    CHECK(esc_snap_bwd(&g, buf + 3) == buf + 1);  /* first run -> 'b' */

    /* A run that begins the line has nothing visible before it, so its own
     * start is returned — the column the following character occupies. */
    init("\033[31mred\n");
    g.color_escape = 1;
    CHECK(esc_snap_bwd(&g, buf + 2) == buf + 0);
    CHECK(get_column(&g, buf + 0) == 0);
    CHECK(get_column(&g, buf + 5) == 0); /* 'r' shares that column */

    /* With the mode off nothing moves. */
    init("ab\033[31mcd\n");
    g.color_escape = 0;
    CHECK(esc_snap_bwd(&g, buf + 4) == buf + 4);

    /* Stepping as dot_left does walks back down the visible columns. */
    init("ab\033[31mcd\033[mef\n");
    g.color_escape = 1;
    p = buf + 13; /* 'f' */
    CHECK(get_column(&g, p) == 5);
    p = esc_snap_bwd(&g, cp_prev(&g, p)); CHECK(get_column(&g, p) == 4);
    p = esc_snap_bwd(&g, cp_prev(&g, p)); CHECK(get_column(&g, p) == 3);
    p = esc_snap_bwd(&g, cp_prev(&g, p)); CHECK(get_column(&g, p) == 2);
    p = esc_snap_bwd(&g, cp_prev(&g, p)); CHECK(get_column(&g, p) == 1);
    p = esc_snap_bwd(&g, cp_prev(&g, p)); CHECK(get_column(&g, p) == 0);
    CHECK(p == buf + 0); /* 'a' */
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
    test_csi_len_sgr();
    test_csi_len_non_sgr();
    test_csi_len_rejects();
    test_esc_skip();
    test_get_column_escape();
    test_cp_next_prev_unchanged_by_mode();
    test_esc_snap_fwd();
    test_esc_snap_bwd();
    SUMMARY();
}
