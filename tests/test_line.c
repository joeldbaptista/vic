/*
 * test_line.c - unit tests for line.c
 *
 * All functions under test only read g->text, g->end, g->tabstop,
 * and the cache fields touched by total_line_count.  A zero-initialised
 * struct editor with those fields set is sufficient.
 *
 * Buffer contract: every test buffer ends with '\n' (the sentinel).
 * g->end points one past the last byte (the byte after the '\n').
 */
#include "testutil.h"
#include "line.h"
#include <stdlib.h>
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

/* ---- begin_line / end_line ----------------------------------------------- */

static void
test_begin_end_line(void)
{
    init("abc\ndef\n");
    /* begin_line: first line */
    CHECK(begin_line(&g, buf + 0) == buf + 0);
    CHECK(begin_line(&g, buf + 1) == buf + 0); /* inside 'abc' */
    CHECK(begin_line(&g, buf + 3) == buf + 0); /* the '\n' itself */
    /* begin_line: second line */
    CHECK(begin_line(&g, buf + 4) == buf + 4);
    CHECK(begin_line(&g, buf + 6) == buf + 4); /* inside 'def' */

    /* end_line: first line ends at its '\n' */
    CHECK(end_line(&g, buf + 0) == buf + 3);
    CHECK(end_line(&g, buf + 2) == buf + 3);
    /* end_line: second line ends at the sentinel '\n' (end-1) */
    CHECK(end_line(&g, buf + 4) == buf + 7);
    CHECK(end_line(&g, buf + 7) == buf + 7); /* already at sentinel */
}

static void
test_begin_end_line_single(void)
{
    /* single-line buffer */
    init("hello\n");
    CHECK(begin_line(&g, buf + 3) == buf + 0);
    CHECK(end_line(&g, buf + 0) == buf + 5);
}

/* ---- dollar_line --------------------------------------------------------- */

static void
test_dollar_line(void)
{
    init("abc\ndef\n");
    /* non-empty line: one before the newline */
    CHECK(dollar_line(&g, buf + 0) == buf + 2); /* 'c' */
    CHECK(dollar_line(&g, buf + 4) == buf + 6); /* 'f' */

    /* empty line: content is just '\n', dollar_line stays on the '\n' */
    init("abc\n\ndef\n");
    CHECK(dollar_line(&g, buf + 4) == buf + 4); /* empty line '\n' */
    CHECK(dollar_line(&g, buf + 0) == buf + 2); /* 'c' */
    CHECK(dollar_line(&g, buf + 5) == buf + 7); /* 'f' */
}

/* ---- next_line / prev_line ----------------------------------------------- */

static void
test_next_prev_line(void)
{
    init("abc\ndef\nghi\n");
    /*
     * abc = buf[0..2], \n at 3
     * def = buf[4..6], \n at 7
     * ghi = buf[8..10], \n at 11 (sentinel, end-1)
     */
    CHECK(next_line(&g, buf +  0) == buf + 4);
    CHECK(next_line(&g, buf +  4) == buf + 8);
    /* last line: returns the sentinel '\n', not past it */
    CHECK(next_line(&g, buf +  8) == buf + 11);
    CHECK(next_line(&g, buf + 11) == buf + 11);

    CHECK(prev_line(&g, buf +  8) == buf + 4);
    CHECK(prev_line(&g, buf +  4) == buf + 0);
    /* first line: clamped to g->text */
    CHECK(prev_line(&g, buf +  0) == buf + 0);
    CHECK(prev_line(&g, buf +  2) == buf + 0); /* mid first-line → first-line */
}

/* ---- count_lines --------------------------------------------------------- */

static void
test_count_lines(void)
{
    init("abc\ndef\nghi\n");
    CHECK(count_lines(&g, buf + 0, buf +  2) == 1);  /* within first line */
    CHECK(count_lines(&g, buf + 0, buf +  7) == 2);  /* 'abc\n' + 'def\n' */
    CHECK(count_lines(&g, buf + 0, buf + 11) == 3);  /* all three */
    CHECK(count_lines(&g, buf + 4, buf +  7) == 1);  /* just 'def\n' */
    /* reversed start/stop: same result */
    CHECK(count_lines(&g, buf + 7, buf +  0) == 2);
}

/* ---- find_line ----------------------------------------------------------- */

static void
test_find_line(void)
{
    init("abc\ndef\nghi\n");
    CHECK(find_line(&g, 1) == buf + 0);
    CHECK(find_line(&g, 2) == buf + 4);
    CHECK(find_line(&g, 3) == buf + 8);
}

/* ---- total_line_count ---------------------------------------------------- */

static void
test_total_line_count(void)
{
    init("abc\ndef\nghi\n");
    /* force recompute by having modified_count != stamp (both 0 after init) */
    g.modified_count = 1;
    CHECK(total_line_count(&g) == 3);

    init("line\n");
    g.modified_count = 1;
    CHECK(total_line_count(&g) == 1);
}

/* ---- next_tabstop / prev_tabstop ----------------------------------------- */

static void
test_tabstop(void)
{
    /* next_tabstop returns (next_stop - 1); next_column adds the final +1. */
    init("x\n");
    g.tabstop = 8;
    CHECK(next_tabstop(&g,  0) == 7);  /* → col 8 */
    CHECK(next_tabstop(&g,  7) == 7);  /* → col 8 */
    CHECK(next_tabstop(&g,  8) == 15); /* → col 16 */
    CHECK(next_tabstop(&g, 15) == 15); /* → col 16 */
    CHECK(next_tabstop(&g, 16) == 23); /* → col 24 */

    g.tabstop = 4;
    CHECK(next_tabstop(&g,  0) == 3);  /* → col 4 */
    CHECK(next_tabstop(&g,  3) == 3);  /* → col 4 */
    CHECK(next_tabstop(&g,  4) == 7);  /* → col 8 */

    /* prev_tabstop: largest tab-stop strictly less than col */
    g.tabstop = 8;
    CHECK(prev_tabstop(&g,  8) == 0);
    CHECK(prev_tabstop(&g,  9) == 8);
    CHECK(prev_tabstop(&g, 15) == 8);
    CHECK(prev_tabstop(&g, 16) == 8);
    CHECK(prev_tabstop(&g, 17) == 16);
}

/* ---- at_eof -------------------------------------------------------------- */

static void
test_at_eof(void)
{
    init("abc\n");
    /* sentinel is buf+3 (end-1).  at_eof is true for sentinel and
     * for the byte just before it when that byte[1]=='\\n'. */
    CHECK(!at_eof(&g, buf + 0));  /* 'a' */
    CHECK(!at_eof(&g, buf + 1));  /* 'b' */
    CHECK( at_eof(&g, buf + 2));  /* 'c', because buf+2[1]=='\n' */
    CHECK( at_eof(&g, buf + 3));  /* the sentinel '\n' */

    /* multi-line: only the last visible char and sentinel are EOF */
    init("abc\ndef\n");
    CHECK(!at_eof(&g, buf + 2));  /* 'c' on first line */
    CHECK(!at_eof(&g, buf + 3));  /* '\n' of first line */
    CHECK(!at_eof(&g, buf + 5));  /* 'e', not near end */
    CHECK( at_eof(&g, buf + 7));  /* sentinel '\n' */
    CHECK( at_eof(&g, buf + 6));  /* 'f': end-2 with end-2[1]=='\n' */
}

/* ---- indent_len ---------------------------------------------------------- */

static void
test_indent_len(void)
{
    init("abc\n");
    CHECK(indent_len(&g, buf) == 0);

    init("   abc\n");
    CHECK(indent_len(&g, buf) == 3);

    init("\tabc\n");
    CHECK(indent_len(&g, buf) == 1);

    init("  \t  abc\n");
    CHECK(indent_len(&g, buf) == 5); /* spaces+tab+spaces */

    /* empty line: no indent */
    init("\n");
    CHECK(indent_len(&g, buf) == 0);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_begin_end_line();
    test_begin_end_line_single();
    test_dollar_line();
    test_next_prev_line();
    test_count_lines();
    test_find_line();
    test_total_line_count();
    test_tabstop();
    test_at_eof();
    test_indent_len();
    SUMMARY();
}
