/*
 * test_wordmotion.c - unit tests for wordmotion.c
 *
 * The six word motions (w b e W B E) read g->dot and g->cmdcnt and write
 * back g->dot.  They call skip_thing() from scan.c, which is linked for
 * real here rather than stubbed, so these tests exercise the pair
 * together.  Nothing else is needed, so there are no stubs.
 *
 * cmdcnt is a count prefix and the loop is a do-while over --cmdcnt, so
 * cmdcnt 0 and cmdcnt 1 both mean one repetition.
 *
 * Lower case treats a punctuation run as a word of its own; upper case
 * (WORD) treats every non-whitespace run as one token.
 */
#include "testutil.h"
#include "wordmotion.h"
#include "pt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

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

/* Run one motion from offset p with no count, and return the new dot. */
typedef void (*motion)(struct editor *);

static int
at(const char *content, int p, motion m)
{
    init(content);
    g.dot    = p;
    g.cmdcnt = 0;
    m(&g);
    return g.dot;
}

/* The same with a count prefix. */
static int
at_n(const char *content, int p, int cnt, motion m)
{
    init(content);
    g.dot    = p;
    g.cmdcnt = cnt;
    m(&g);
    return g.dot;
}

/* ---- w: forward to the start of the next word ---------------------------- */

static void
test_w(void)
{
    /* f0 o1 o2 ' '3 b4 a5 r6 ' '7 b8 a9 z10 \n11 */
    const char *t = "foo bar baz\n";

    CHECK(at(t, 0, run_word_forward_cmd) == 4);
    CHECK(at(t, 1, run_word_forward_cmd) == 4);
    CHECK(at(t, 2, run_word_forward_cmd) == 4);
    /* From the space between words, w still lands on the next word. */
    CHECK(at(t, 3, run_word_forward_cmd) == 4);
    CHECK(at(t, 4, run_word_forward_cmd) == 8);
    /* From the last word there is nowhere further to go than the sentinel. */
    CHECK(at(t, 8, run_word_forward_cmd) == 11);
    CHECK(at(t, 11, run_word_forward_cmd) == 11);
}

static void
test_w_punctuation(void)
{
    /* a0 .1 b2 ' '3 ' '4 c5 d6 ,7 e8 \n9 */
    const char *t = "a.b  cd,e\n";

    /* Lower case w treats the punctuation run as a word of its own. */
    CHECK(at(t, 0, run_word_forward_cmd) == 1); /* 'a' -> '.' */
    CHECK(at(t, 1, run_word_forward_cmd) == 2); /* '.' -> 'b' */
    /* From 'b' the next word is past both spaces. */
    CHECK(at(t, 2, run_word_forward_cmd) == 5);
    CHECK(at(t, 5, run_word_forward_cmd) == 7); /* 'c' -> ',' */
    CHECK(at(t, 7, run_word_forward_cmd) == 8); /* ',' -> 'e' */
}

static void
test_w_across_lines(void)
{
    /* a0 b1 \n2 c3 d4 \n5 */
    const char *t = "ab\ncd\n";

    CHECK(at(t, 0, run_word_forward_cmd) == 3);
    CHECK(at(t, 1, run_word_forward_cmd) == 3);
    CHECK(at(t, 3, run_word_forward_cmd) == 5);
}

static void
test_w_leading_indent(void)
{
    /* ' '0 ' '1 x2 ' '3 y4 \n5 */
    const char *t = "  x y\n";

    CHECK(at(t, 0, run_word_forward_cmd) == 2);
    CHECK(at(t, 2, run_word_forward_cmd) == 4);
}

static void
test_w_count(void)
{
    /* a0 a1 ' '2 b3 b4 ' '5 c6 c7 ' '8 d9 d10 \n11 */
    const char *t = "aa bb cc dd\n";

    /* A count of 0 and a count of 1 both move one word. */
    CHECK(at_n(t, 0, 0, run_word_forward_cmd) == 3);
    CHECK(at_n(t, 0, 1, run_word_forward_cmd) == 3);
    CHECK(at_n(t, 0, 2, run_word_forward_cmd) == 6);
    CHECK(at_n(t, 0, 3, run_word_forward_cmd) == 9);
    /* A count past the end of the buffer stops at the sentinel. */
    CHECK(at_n(t, 0, 4, run_word_forward_cmd) == 11);
    CHECK(at_n(t, 0, 99, run_word_forward_cmd) == 11);
}

/* ---- b: backward to the start of a word ---------------------------------- */

static void
test_b(void)
{
    const char *t = "foo bar baz\n";

    /* From inside a word, b goes to that word's first byte. */
    CHECK(at(t, 6, run_word_backward_cmd) == 4);
    CHECK(at(t, 5, run_word_backward_cmd) == 4);
    /* From a word's first byte, b goes to the previous word. */
    CHECK(at(t, 4, run_word_backward_cmd) == 0);
    CHECK(at(t, 8, run_word_backward_cmd) == 4);
    /* The start of the buffer stops it. */
    CHECK(at(t, 0, run_word_backward_cmd) == 0);
    CHECK(at(t, 1, run_word_backward_cmd) == 0);
}

static void
test_b_punctuation(void)
{
    const char *t = "a.b  cd,e\n";

    CHECK(at(t, 9, run_word_backward_cmd) == 8); /* '\n' -> 'e' */
    CHECK(at(t, 8, run_word_backward_cmd) == 7); /* 'e' -> ',' */
    CHECK(at(t, 7, run_word_backward_cmd) == 5); /* ',' -> 'c' */
    CHECK(at(t, 5, run_word_backward_cmd) == 2); /* over both spaces to 'b' */
    CHECK(at(t, 2, run_word_backward_cmd) == 1); /* 'b' -> '.' */
}

static void
test_b_count(void)
{
    const char *t = "aa bb cc dd\n";

    CHECK(at_n(t, 9, 1, run_word_backward_cmd) == 6);
    CHECK(at_n(t, 9, 2, run_word_backward_cmd) == 3);
    CHECK(at_n(t, 9, 3, run_word_backward_cmd) == 0);
    CHECK(at_n(t, 9, 99, run_word_backward_cmd) == 0);
}

static void
test_b_at_line_start(void)
{
    /* a0 b1 \n2 c3 d4 \n5 */
    const char *t = "ab\ncd\n";

    /*
     * From the first byte of a line, b stops on the preceding '\n' rather
     * than continuing to the previous word.  Real vi would land on 0.
     * This is pre-existing behaviour, unchanged by the piece-tree port, so
     * the test pins it rather than asserting the vi answer.
     */
    CHECK(at(t, 3, run_word_backward_cmd) == 2);
    /* A second b then reaches the word on the line above. */
    CHECK(at_n(t, 3, 2, run_word_backward_cmd) == 0);
}

/* ---- e: forward to the end of a word ------------------------------------- */

static void
test_e(void)
{
    const char *t = "foo bar baz\n";

    /* From inside a word, e goes to that word's last byte. */
    CHECK(at(t, 0, run_word_end_cmd) == 2);
    CHECK(at(t, 1, run_word_end_cmd) == 2);
    /* From the last byte, e goes to the end of the next word. */
    CHECK(at(t, 2, run_word_end_cmd) == 6);
    CHECK(at(t, 6, run_word_end_cmd) == 10);
    /* From the space before a word, e reaches that word's end. */
    CHECK(at(t, 3, run_word_end_cmd) == 6);
}

static void
test_e_punctuation(void)
{
    const char *t = "a.b  cd,e\n";

    CHECK(at(t, 0, run_word_end_cmd) == 1); /* 'a' -> end of the '.' run */
    CHECK(at(t, 1, run_word_end_cmd) == 2); /* '.' -> 'b' */
    CHECK(at(t, 2, run_word_end_cmd) == 6); /* 'b' -> 'd', over the spaces */
    CHECK(at(t, 6, run_word_end_cmd) == 7); /* 'd' -> ',' */
}

static void
test_e_count(void)
{
    const char *t = "aa bb cc dd\n";

    CHECK(at_n(t, 0, 1, run_word_end_cmd) == 1);
    CHECK(at_n(t, 0, 2, run_word_end_cmd) == 4);
    CHECK(at_n(t, 0, 3, run_word_end_cmd) == 7);
    CHECK(at_n(t, 0, 4, run_word_end_cmd) == 10);
}

/* ---- W B E: punctuation counts as part of the WORD ----------------------- */

static void
test_blank_word_forward(void)
{
    /* a0 .1 b2 ' '3 ' '4 c5 d6 ,7 e8 \n9 */
    const char *t = "a.b  cd,e\n";

    /* W skips "a.b" whole, where w would have stopped on the '.'. */
    CHECK(at(t, 0, run_blank_word_forward_cmd) == 5);
    CHECK(at(t, 1, run_blank_word_forward_cmd) == 5);
    CHECK(at(t, 2, run_blank_word_forward_cmd) == 5);
    /* The last WORD has nothing after it but the sentinel. */
    CHECK(at(t, 5, run_blank_word_forward_cmd) == 9);
}

static void
test_blank_word_backward(void)
{
    const char *t = "a.b  cd,e\n";

    /* B from inside the second WORD goes to its first byte. */
    CHECK(at(t, 8, run_blank_word_backward_cmd) == 5);
    CHECK(at(t, 7, run_blank_word_backward_cmd) == 5);
    /* And from there back over both spaces to the first WORD's start. */
    CHECK(at(t, 5, run_blank_word_backward_cmd) == 0);
    CHECK(at(t, 0, run_blank_word_backward_cmd) == 0);
}

static void
test_blank_word_end(void)
{
    const char *t = "a.b  cd,e\n";

    /* E goes to the last byte of the WORD, so "a.b" ends at 2. */
    CHECK(at(t, 0, run_blank_word_end_cmd) == 2);
    CHECK(at(t, 1, run_blank_word_end_cmd) == 2);
    /* From that last byte, E reaches the end of the next WORD. */
    CHECK(at(t, 2, run_blank_word_end_cmd) == 8);
}

static void
test_blank_word_matches_word_without_punctuation(void)
{
    /* With no punctuation in the text, upper and lower case agree. */
    const char *t = "foo bar baz\n";
    int p;

    for (p = 0; p < 12; p++) {
        CHECK(at(t, p, run_word_forward_cmd) ==
              at(t, p, run_blank_word_forward_cmd));
        CHECK(at(t, p, run_word_backward_cmd) ==
              at(t, p, run_blank_word_backward_cmd));
        CHECK(at(t, p, run_word_end_cmd) == at(t, p, run_blank_word_end_cmd));
    }
}

static void
test_blank_word_count(void)
{
    const char *t = "a.b c.d e.f\n";

    CHECK(at_n(t, 0, 1, run_blank_word_forward_cmd) == 4);
    CHECK(at_n(t, 0, 2, run_blank_word_forward_cmd) == 8);
    CHECK(at_n(t, 0, 1, run_blank_word_end_cmd) == 2);
    CHECK(at_n(t, 0, 2, run_blank_word_end_cmd) == 6);
    CHECK(at_n(t, 10, 2, run_blank_word_backward_cmd) == 4);
}

/* ---- boundaries ---------------------------------------------------------- */

static void
test_empty_buffer(void)
{
    /* No text at all: every motion leaves dot at 0 rather than running
     * off either end. */
    CHECK(at("", 0, run_word_forward_cmd) == 0);
    CHECK(at("", 0, run_word_backward_cmd) == 0);
    CHECK(at("", 0, run_word_end_cmd) == 0);
    CHECK(at("", 0, run_blank_word_forward_cmd) == 0);
    CHECK(at("", 0, run_blank_word_backward_cmd) == 0);
    CHECK(at("", 0, run_blank_word_end_cmd) == 0);
}

static void
test_stays_in_bounds(void)
{
    /* Whatever the start position and count, dot must stay inside the
     * buffer.  A motion that walked past the sentinel would be an
     * out-of-bounds read for every later command. */
    static const char *texts[] = {"foo bar baz\n", "a.b  cd,e\n", "ab\ncd\n",
                                  "  x y\n",       "x\n",         "\n"};
    motion all[] = {run_word_forward_cmd,       run_word_backward_cmd,
                    run_word_end_cmd,           run_blank_word_forward_cmd,
                    run_blank_word_backward_cmd, run_blank_word_end_cmd};
    size_t ti;
    int mi;

    for (ti = 0; ti < sizeof(texts) / sizeof(texts[0]); ti++) {
        int len = (int)strlen(texts[ti]);
        int p;

        for (p = 0; p < len; p++)
            for (mi = 0; mi < 6; mi++) {
                int r = at_n(texts[ti], p, 3, all[mi]);

                CHECK(r >= 0 && r <= len - 1);
            }
    }
}

static void
test_single_word_buffer(void)
{
    /* x0 \n1 */
    const char *t = "x\n";

    CHECK(at(t, 0, run_word_forward_cmd) == 1);
    CHECK(at(t, 0, run_word_end_cmd) == 1);
    CHECK(at(t, 0, run_word_backward_cmd) == 0);
    CHECK(at(t, 1, run_word_backward_cmd) == 0);
}

/* ---- UTF-8 --------------------------------------------------------------- */

static void
test_multibyte(void)
{
    /* "foéo bar": f0 o1 [c3 a9]2,3 o4 ' '5 b6 a7 r8 \n9.  A multibyte
     * byte counts as a word byte, so "foéo" is one word, and the motions
     * must land on codepoint starts only, never on offset 3. */
    const char *t = "fo\xc3\xa9o bar\n";

    CHECK(at(t, 0, run_word_forward_cmd) == 6);
    CHECK(at(t, 0, run_word_end_cmd) == 4);
    CHECK(at(t, 6, run_word_backward_cmd) == 0);
    /* Stepping off the multibyte character goes to 4, not 3. */
    CHECK(at(t, 2, run_word_end_cmd) == 4);
}

int
main(void)
{
    test_w();
    test_w_punctuation();
    test_w_across_lines();
    test_w_leading_indent();
    test_w_count();

    test_b();
    test_b_punctuation();
    test_b_count();
    test_b_at_line_start();

    test_e();
    test_e_punctuation();
    test_e_count();

    test_blank_word_forward();
    test_blank_word_backward();
    test_blank_word_end();
    test_blank_word_matches_word_without_punctuation();
    test_blank_word_count();

    test_empty_buffer();
    test_stays_in_bounds();
    test_single_word_buffer();

    test_multibyte();

    SUMMARY();
}
