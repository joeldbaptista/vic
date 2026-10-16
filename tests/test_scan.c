/*
 * test_scan.c - unit tests for scan.c
 *
 * scan.c exports one function, skip_thing(), the primitive every word
 * motion (w/b/e/W/B/E) is built from.  It reads the text through text.h
 * and steps by codepoint through codepoint.h, so this test links the real
 * piece tree and needs no stubs.
 *
 * The scan types are a private enum in scan.c, so they are repeated here.
 * Keep the two in step.
 *
 *   S_BEFORE_WS  stop on the last byte before whitespace  (the `e` motion)
 *   S_TO_WS      stop on the first whitespace byte
 *   S_OVER_WS    skip a whitespace run
 *   S_END_PUNCT  stop on the last byte of a punctuation run
 *   S_END_ALNUM  stop on the last byte of an alnum/underscore run
 *
 * S_BEFORE_WS, S_END_PUNCT and S_END_ALNUM test the byte adjacent to p in
 * the direction of travel, so they stop one short of the boundary.
 * S_TO_WS and S_OVER_WS test the byte at p itself.
 */
#include "testutil.h"
#include "scan.h"
#include "pt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

enum {
    S_BEFORE_WS = 1,
    S_TO_WS     = 2,
    S_OVER_WS   = 3,
    S_END_PUNCT = 4,
    S_END_ALNUM = 5
};

#define FWD 1
#define BWD (-1)

static struct editor g;

static void
init(const char *content)
{
    size_t n = strlen(content);
    char *copy = NULL;

    if (n) {
        copy = malloc(n + 1);
        if (!copy)
            abort();
        memcpy(copy, content, n + 1);
    }
    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt      = ptnew(copy, n);
    g.tabstop = 8;
}

/* ---- S_TO_WS: forward to the first whitespace ---------------------------- */

static void
test_to_ws_forward(void)
{
    /* f0 o1 o2 ' '3 b4 a5 r6 \n7 */
    init("foo bar\n");
    CHECK(skip_thing(&g, 0, 1, FWD, S_TO_WS) == 3);
    CHECK(skip_thing(&g, 1, 1, FWD, S_TO_WS) == 3);
    CHECK(skip_thing(&g, 2, 1, FWD, S_TO_WS) == 3);
    /* Already on whitespace: nothing to do. */
    CHECK(skip_thing(&g, 3, 1, FWD, S_TO_WS) == 3);
    /* The sentinel '\n' counts as the whitespace that ends the last word. */
    CHECK(skip_thing(&g, 4, 1, FWD, S_TO_WS) == 7);
}

static void
test_to_ws_backward(void)
{
    init("foo bar\n");
    /* Backwards the scan stops on the whitespace it reaches. */
    CHECK(skip_thing(&g, 6, 1, BWD, S_TO_WS) == 3);
    CHECK(skip_thing(&g, 4, 1, BWD, S_TO_WS) == 3);
    /* Already on whitespace: it stays. */
    CHECK(skip_thing(&g, 3, 1, BWD, S_TO_WS) == 3);
    /* The start of the buffer stops the scan. */
    CHECK(skip_thing(&g, 2, 1, BWD, S_TO_WS) == 0);
    CHECK(skip_thing(&g, 0, 1, BWD, S_TO_WS) == 0);
}

/* ---- S_OVER_WS: skip a whitespace run ------------------------------------ */

static void
test_over_ws(void)
{
    /* f0 o1 o2 ' '3 ' '4 b5 a6 r7 ... */
    init("foo  bar baz\n");
    CHECK(skip_thing(&g, 3, 1, FWD, S_OVER_WS) == 5);
    CHECK(skip_thing(&g, 4, 1, FWD, S_OVER_WS) == 5);
    /* Not on whitespace: it stays. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_OVER_WS) == 0);
    CHECK(skip_thing(&g, 5, 1, FWD, S_OVER_WS) == 5);
    /* Backwards over the same run. */
    CHECK(skip_thing(&g, 4, 1, BWD, S_OVER_WS) == 2);
    CHECK(skip_thing(&g, 3, 1, BWD, S_OVER_WS) == 2);
}

static void
test_over_ws_leading_indent(void)
{
    /* ' '0 ' '1 l2 e3 a4 d5 \n6 */
    init("  lead\n");
    CHECK(skip_thing(&g, 0, 1, FWD, S_OVER_WS) == 2);
    CHECK(skip_thing(&g, 1, 1, FWD, S_OVER_WS) == 2);
    /* Backwards from inside the indent: the buffer start stops it. */
    CHECK(skip_thing(&g, 1, 1, BWD, S_OVER_WS) == 0);
}

static void
test_over_ws_tabs(void)
{
    init("\t\tx\n");
    /* A tab is whitespace too, so the run is skipped the same way. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_OVER_WS) == 2);
}

/* ---- S_BEFORE_WS: the `e` motion ----------------------------------------- */

static void
test_before_ws_forward(void)
{
    init("foo bar\n");
    /* Stops on the last byte of the word, not on the whitespace. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_BEFORE_WS) == 2);
    CHECK(skip_thing(&g, 1, 1, FWD, S_BEFORE_WS) == 2);
    /* Already there: it stays. */
    CHECK(skip_thing(&g, 2, 1, FWD, S_BEFORE_WS) == 2);
    /* From the whitespace it carries on to the end of the next word,
     * because the test looks at the byte ahead. */
    CHECK(skip_thing(&g, 3, 1, FWD, S_BEFORE_WS) == 6);
}

static void
test_before_ws_backward(void)
{
    init("foo bar\n");
    /* Backwards it stops on the first byte of the word: the `b` motion. */
    CHECK(skip_thing(&g, 6, 1, BWD, S_BEFORE_WS) == 4);
    CHECK(skip_thing(&g, 5, 1, BWD, S_BEFORE_WS) == 4);
    CHECK(skip_thing(&g, 2, 1, BWD, S_BEFORE_WS) == 0);
}

static void
test_before_ws_on_whitespace(void)
{
    init("  lead\n");
    /* The byte ahead of 0 is itself whitespace, so there is nothing to
     * scan over and p stays put. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_BEFORE_WS) == 0);
    CHECK(skip_thing(&g, 1, 1, FWD, S_BEFORE_WS) == 5);
}

/* ---- S_END_ALNUM and S_END_PUNCT: word vs punctuation runs --------------- */

static void
test_end_alnum(void)
{
    /* a0 .1 b2 ,3 c4 \n5 — alternating word and punctuation bytes. */
    init("a.b,c\n");
    /* The byte ahead of 0 is '.', not a word byte, so p stays. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_END_ALNUM) == 0);
    /* From the '.', the next byte 'b' is a word byte, so it steps onto it
     * and stops there because ',' follows. */
    CHECK(skip_thing(&g, 1, 1, FWD, S_END_ALNUM) == 2);
    CHECK(skip_thing(&g, 3, 1, FWD, S_END_ALNUM) == 4);

    /* A longer run is crossed in one call. */
    init("ab_12 x\n");
    CHECK(skip_thing(&g, 0, 1, FWD, S_END_ALNUM) == 4);
    /* '_' and digits count as word bytes, which is what makes the run
     * five bytes long rather than two. */
    CHECK(skip_thing(&g, 4, 1, BWD, S_END_ALNUM) == 0);
}

static void
test_end_punct(void)
{
    init("a.b,c\n");
    CHECK(skip_thing(&g, 0, 1, FWD, S_END_PUNCT) == 1);
    CHECK(skip_thing(&g, 2, 1, FWD, S_END_PUNCT) == 3);
    /* Ahead of 1 is 'b', not punctuation, so p stays. */
    CHECK(skip_thing(&g, 1, 1, FWD, S_END_PUNCT) == 1);

    /* A run of punctuation is crossed in one call. */
    init("a...b\n");
    CHECK(skip_thing(&g, 0, 1, FWD, S_END_PUNCT) == 3);
    CHECK(skip_thing(&g, 3, 1, BWD, S_END_PUNCT) == 1);
}

static void
test_end_punct_ignores_words(void)
{
    init("foo bar\n");
    /* No punctuation anywhere: every start position is a fixed point. */
    int p;

    for (p = 0; p < text_len(&g); p++) {
        CHECK(skip_thing(&g, p, 1, FWD, S_END_PUNCT) == p);
        CHECK(skip_thing(&g, p, 1, BWD, S_END_PUNCT) == p);
    }
}

/* ---- linecnt: how many line boundaries the scan may cross ---------------- */

static void
test_linecnt_stops_at_newline(void)
{
    /* a0 b1 \n2 \n3 c4 d5 \n6 e7 f8 \n9 */
    init("ab\n\ncd\nef\n");
    /* linecnt 1 means the first '\n' ends the scan. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_TO_WS) == 2);
    CHECK(skip_thing(&g, 2, 1, FWD, S_TO_WS) == 2);
    CHECK(skip_thing(&g, 3, 1, FWD, S_TO_WS) == 3);
}

static void
test_linecnt_crosses_newlines(void)
{
    init("ab\n\ncd\nef\n");
    /* Each extra line of allowance lets the scan pass one more '\n'. */
    CHECK(skip_thing(&g, 0, 2, FWD, S_TO_WS) == 3);
    CHECK(skip_thing(&g, 0, 3, FWD, S_TO_WS) == 6);
    CHECK(skip_thing(&g, 0, 4, FWD, S_TO_WS) == 9);
    /* And the same allowance works backwards. */
    CHECK(skip_thing(&g, 9, 2, BWD, S_TO_WS) == 6);
    CHECK(skip_thing(&g, 9, 3, BWD, S_TO_WS) == 3);
    CHECK(skip_thing(&g, 9, 4, BWD, S_TO_WS) == 2);
}

static void
test_linecnt_over_ws_crosses_blank_line(void)
{
    init("ab\n\ncd\nef\n");
    /* With one line of allowance the blank line is not crossed. */
    CHECK(skip_thing(&g, 2, 1, FWD, S_OVER_WS) == 2);
    /* With two, the scan steps onto the blank line's '\n'. */
    CHECK(skip_thing(&g, 2, 2, FWD, S_OVER_WS) == 3);
    /* With three, it reaches the next non-blank byte. */
    CHECK(skip_thing(&g, 2, 3, FWD, S_OVER_WS) == 4);
}

/* ---- buffer boundaries --------------------------------------------------- */

static void
test_stops_at_buffer_end(void)
{
    init("foo bar\n"); /* len 8 */
    int p;

    /* No scan may return the one-past-the-end offset, nor a negative. */
    for (p = 0; p < text_len(&g); p++) {
        int ty;

        for (ty = S_BEFORE_WS; ty <= S_END_ALNUM; ty++) {
            int f = skip_thing(&g, p, 4, FWD, ty);
            int b = skip_thing(&g, p, 4, BWD, ty);

            CHECK(f >= 0 && f <= text_len(&g) - 1);
            CHECK(b >= 0 && b <= text_len(&g) - 1);
        }
    }
}

static void
test_empty_buffer(void)
{
    int ty;

    init("");
    /* Nothing to scan: every type and direction returns 0. */
    for (ty = S_BEFORE_WS; ty <= S_END_ALNUM; ty++) {
        CHECK(skip_thing(&g, 0, 1, FWD, ty) == 0);
        CHECK(skip_thing(&g, 0, 1, BWD, ty) == 0);
    }
}

static void
test_single_character_buffer(void)
{
    init("x\n"); /* x0 \n1 */
    /* Forward from 0, S_TO_WS reaches the sentinel. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_TO_WS) == 1);
    /* S_BEFORE_WS cannot advance: the byte ahead is the '\n'. */
    CHECK(skip_thing(&g, 0, 1, FWD, S_BEFORE_WS) == 0);
    /* Backwards from the sentinel. */
    CHECK(skip_thing(&g, 1, 1, BWD, S_TO_WS) == 1);
    CHECK(skip_thing(&g, 1, 1, BWD, S_BEFORE_WS) == 0);
}

static void
test_unknown_type(void)
{
    init("foo bar\n");
    /* st_test leaves `test` at 0 for a type it does not recognise, so the
     * scan does not move.  That keeps a bad caller from running away. */
    CHECK(skip_thing(&g, 2, 1, FWD, 0) == 2);
    CHECK(skip_thing(&g, 2, 1, FWD, 99) == 2);
}

/* ---- UTF-8 --------------------------------------------------------------- */

static void
test_multibyte(void)
{
    /* "foéo bar": f0 o1 [c3 a9]2,3 o4 ' '5 b6 a7 r8 \n9.  The scan steps
     * by codepoint, so it must land on 4 and never on 3. */
    init("fo\xc3\xa9o bar\n");
    CHECK(text_len(&g) == 10);
    CHECK(skip_thing(&g, 0, 1, FWD, S_BEFORE_WS) == 4);
    CHECK(skip_thing(&g, 0, 1, FWD, S_END_ALNUM) == 4);
    CHECK(skip_thing(&g, 0, 1, FWD, S_TO_WS) == 5);
    /* Backwards from the last word, stopping on its first byte. */
    CHECK(skip_thing(&g, 8, 1, BWD, S_BEFORE_WS) == 6);
}

static void
test_multibyte_is_a_word_byte(void)
{
    /* A lead or continuation byte is >= UTF8_MULTIBYTE_MIN, which counts
     * as a word byte, so an accented word is one run and not three. */
    init("\xc3\xa9\xc3\xa9 x\n");
    CHECK(skip_thing(&g, 0, 1, FWD, S_END_ALNUM) == 2);
    CHECK(skip_thing(&g, 0, 1, FWD, S_TO_WS) == 4);
}

int
main(void)
{
    test_to_ws_forward();
    test_to_ws_backward();

    test_over_ws();
    test_over_ws_leading_indent();
    test_over_ws_tabs();

    test_before_ws_forward();
    test_before_ws_backward();
    test_before_ws_on_whitespace();

    test_end_alnum();
    test_end_punct();
    test_end_punct_ignores_words();

    test_linecnt_stops_at_newline();
    test_linecnt_crosses_newlines();
    test_linecnt_over_ws_crosses_blank_line();

    test_stops_at_buffer_end();
    test_empty_buffer();
    test_single_character_buffer();
    test_unknown_type();

    test_multibyte();
    test_multibyte_is_a_word_byte();

    SUMMARY();
}
