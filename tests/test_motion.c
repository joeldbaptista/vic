/*
 * test_motion.c - unit tests for motion.c
 *
 * motion.c holds the cursor motions: the primitives (dot_left, dot_right,
 * dot_begin, dot_end, move_to_col, dot_next, dot_prev, dot_skip_over_ws,
 * bound_dot) and the command entry points behind h l ^ $ | G j k + - H L
 * M { } f F t T ; , z and the scroll keys.
 *
 * line.c, codepoint.c and the piece tree are linked for real.  Three
 * symbols are stubbed:
 *
 *   undo_queue_commit — counted; the motions must commit pending undo.
 *   indicate_error    — counted; several motions beep rather than move.
 *   get_one_char      — returns a scripted key for the interactive paths.
 *
 * A motion must never leave dot outside [0, text_len - 1], which is
 * checked directly at the end of this file.
 *
 * Screen-relative motions (H L M z and dot_scroll) read g->rows and
 * g->screenbegin, which the fixture sets; no terminal is involved.
 */
#include "testutil.h"
#include "motion.h"
#include "line.h"
#include "pt.h"
#include "text.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static struct editor g;
static int errors;  /* indicate_error() calls */
static int commits; /* undo_queue_commit() calls */
static int next_key;

void
indicate_error(struct editor *x)
{
    (void)x;
    errors++;
}

void
undo_queue_commit(struct editor *x)
{
    (void)x;
    commits++;
}

int
get_one_char(struct editor *x)
{
    (void)x;
    return next_key;
}

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
    g.rows    = 5;
    g.columns = 80;
    errors    = 0;
    commits   = 0;
    next_key  = 0;
}

/* ---- dot_left / dot_right ------------------------------------------------ */

static void
test_dot_left_right(void)
{
    /* a0 b1 c2 \n3 d4 e5 \n6 */
    init("abc\nde\n");
    g.dot = 2;
    dot_left(&g);
    CHECK(g.dot == 1);
    /* Every motion commits the pending undo run first. */
    CHECK(commits == 1);
    dot_left(&g);
    CHECK(g.dot == 0);
    /* The start of the line stops it; it does not wrap to the line above. */
    dot_left(&g);
    CHECK(g.dot == 0);

    g.dot = 0;
    dot_right(&g);
    CHECK(g.dot == 1);
    dot_right(&g);
    CHECK(g.dot == 2);
    /* The guard tests the byte at dot, not the one ahead, so from the
     * last character of a line the cursor steps onto the trailing '\n'
     * and stops there.  It never crosses to the next line. */
    dot_right(&g);
    CHECK(g.dot == 3);
    dot_right(&g);
    CHECK(g.dot == 3);
}

static void
test_dot_left_right_line_boundaries(void)
{
    init("abc\nde\n");
    /* On the first byte of a line, left does not cross back. */
    g.dot = 4;
    dot_left(&g);
    CHECK(g.dot == 4);
    /* On the last byte of a line, right reaches that line's '\n' and
     * goes no further. */
    g.dot = 5;
    dot_right(&g);
    CHECK(g.dot == 6);
    dot_right(&g);
    CHECK(g.dot == 6);
}

static void
test_dot_left_right_multibyte(void)
{
    /* a0 [c3 a9]1,2 b3 \n4 — the motions step whole codepoints. */
    init("a\xc3\xa9""b\n");
    g.dot = 3;
    dot_left(&g);
    CHECK(g.dot == 1);
    dot_left(&g);
    CHECK(g.dot == 0);
    dot_right(&g);
    CHECK(g.dot == 1);
    dot_right(&g);
    CHECK(g.dot == 3);
}

/* ---- dot_begin / dot_end ------------------------------------------------- */

static void
test_dot_begin_end(void)
{
    /* a0 b1 c2 \n3 d4 e5 \n6 */
    init("abc\nde\n");
    g.dot = 2;
    dot_begin(&g);
    CHECK(g.dot == 0);
    g.dot = 5;
    dot_begin(&g);
    CHECK(g.dot == 4);

    g.dot = 0;
    dot_end(&g);
    /* dot_end lands on the newline itself. */
    CHECK(g.dot == 3);
    g.dot = 4;
    dot_end(&g);
    CHECK(g.dot == 6);
}

/* ---- dot_next / dot_prev ------------------------------------------------- */

static void
test_dot_next_prev(void)
{
    init("abc\nde\nf\n");
    g.dot = 1;
    dot_next(&g);
    CHECK(begin_line(&g, g.dot) == 4);
    dot_prev(&g);
    CHECK(begin_line(&g, g.dot) == 0);
}

/* ---- dot_skip_over_ws ---------------------------------------------------- */

static void
test_dot_skip_over_ws(void)
{
    /* ' '0 ' '1 \t2 x3 \n4 */
    init("  \tx\n");
    g.dot = 0;
    dot_skip_over_ws(&g);
    CHECK(g.dot == 3);

    /* Already on a non-blank: no movement. */
    g.dot = 3;
    dot_skip_over_ws(&g);
    CHECK(g.dot == 3);
}

static void
test_dot_skip_over_ws_stops_at_newline(void)
{
    /* A blank line must not be skipped out of. */
    init("  \n x\n");
    g.dot = 0;
    dot_skip_over_ws(&g);
    CHECK(g.dot == 2); /* the '\n' at 2 */
}

/* ---- move_to_col --------------------------------------------------------- */

static void
test_move_to_col(void)
{
    /* a0 b1 c2 \n3 */
    init("abc\n");
    CHECK(move_to_col(&g, 0, 0) == 0);
    CHECK(move_to_col(&g, 0, 1) == 1);
    CHECK(move_to_col(&g, 0, 2) == 2);
    /* Past the end of the line it stops on the newline. */
    CHECK(move_to_col(&g, 0, 3) == 3);
    CHECK(move_to_col(&g, 0, 99) == 3);
    /* Any position on the line gives the same answer: the search starts
     * from the line's first byte. */
    CHECK(move_to_col(&g, 2, 1) == 1);
}

static void
test_move_to_col_tabs(void)
{
    /* \t0 a1 b2 \n3 with tabstop 8, so 'a' sits at column 8. */
    init("\tab\n");
    /* Columns 0 to 7 are all inside the tab, so they all resolve to it. */
    CHECK(move_to_col(&g, 0, 0) == 0);
    CHECK(move_to_col(&g, 0, 7) == 0);
    CHECK(move_to_col(&g, 0, 8) == 1);
    CHECK(move_to_col(&g, 0, 9) == 2);
}

static void
test_move_to_col_second_line(void)
{
    /* a0 b1 c2 \n3 x4 \n5 l6 ... */
    init("abc\nx\nlonger line\n");
    CHECK(move_to_col(&g, 6, 0) == 6);
    CHECK(move_to_col(&g, 6, 10) == 16);
    /* Column past the line's last character resolves to its newline. */
    CHECK(move_to_col(&g, 6, 11) == 17);
    CHECK(move_to_col(&g, 6, INT_MAX) == 17);
}

/* ---- h and l with a count ------------------------------------------------ */

static void
test_left_right_cmd_count(void)
{
    init("abcdef\n");
    g.dot    = 5;
    g.cmdcnt = 3;
    motion_run_left_cmd(&g);
    CHECK(g.dot == 2);

    g.dot    = 0;
    g.cmdcnt = 3;
    motion_run_right_cmd(&g);
    CHECK(g.dot == 3);

    /* A count of 0 still moves once, because the loop is a do-while. */
    g.dot    = 3;
    g.cmdcnt = 0;
    motion_run_left_cmd(&g);
    CHECK(g.dot == 2);

    /* A count past the line end stops on the trailing '\n' rather than
     * crossing into the next line. */
    g.dot    = 0;
    g.cmdcnt = 99;
    motion_run_right_cmd(&g);
    CHECK(g.dot == 6);
}

/* ---- ^ and $ ------------------------------------------------------------- */

static void
test_first_nonblank(void)
{
    /* ' '0 ' '1 a2 b3 \n4 */
    init("  ab\n");
    g.dot = 3;
    motion_run_first_nonblank_cmd(&g);
    CHECK(g.dot == 2);
}

static void
test_line_end(void)
{
    init("abc\nde\nf\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    motion_run_line_end_cmd(&g);
    CHECK(g.dot == 3);
    /* $ sets the sticky column to "end of line" for a following j or k. */
    CHECK(g.cindex == -1);
    CHECK(g.keep_index == TRUE);

    /* With a count, $ goes to the end of the n-th line below. */
    g.dot    = 0;
    g.cmdcnt = 2;
    motion_run_line_end_cmd(&g);
    CHECK(g.dot == 6);
}

/* ---- | and G ------------------------------------------------------------- */

static void
test_goto_column(void)
{
    init("abcdef\n");
    /* The count is a 1-based column. */
    g.dot    = 0;
    g.cmdcnt = 3;
    motion_run_goto_column_cmd(&g);
    CHECK(g.dot == 2);

    g.dot    = 5;
    g.cmdcnt = 1;
    motion_run_goto_column_cmd(&g);
    CHECK(g.dot == 0);
}

static void
test_goto_line(void)
{
    /* a0 b1 \n2 ' '3 c4 d5 \n6 e7 \n8 */
    init("ab\n cd\ne\n");
    /* With no count, G goes to the last line. */
    g.dot    = 0;
    g.cmdcnt = 0;
    motion_run_goto_line_cmd(&g);
    CHECK(begin_line(&g, g.dot) == 7);

    /* With a count, G goes to that 1-based line, first non-blank. */
    g.dot    = 0;
    g.cmdcnt = 2;
    motion_run_goto_line_cmd(&g);
    CHECK(g.dot == 4); /* past the leading space */

    g.dot    = 8;
    g.cmdcnt = 1;
    motion_run_goto_line_cmd(&g);
    CHECK(g.dot == 0);
}

/* ---- j and k ------------------------------------------------------------- */

static void
test_next_line_keep_col(void)
{
    /* a0 b1 c2 \n3 x4 \n5 l6 ... \n17 */
    init("abc\nx\nlonger line\n");
    g.dot    = 2; /* column 2 */
    g.cmdcnt = 0;
    motion_run_next_line_keep_col_cmd(&g);
    /* The short line has no column 2, so the cursor clamps to its end. */
    CHECK(g.dot == 5);
    CHECK(g.cindex == 2);
    CHECK(g.keep_index == TRUE);
}

static void
test_next_line_sticky_column(void)
{
    init("abc\nx\nlonger line\n");
    g.dot    = 2;
    g.cmdcnt = 0;
    motion_run_next_line_keep_col_cmd(&g);
    /* The caller copies keep_index into prev_keep_index between commands;
     * that is what makes the intended column survive the short line. */
    g.prev_keep_index = g.keep_index;
    g.cmdcnt          = 0;
    motion_run_next_line_keep_col_cmd(&g);
    CHECK(g.dot == 8); /* column 2 of the long line, not its start */
    CHECK(g.cindex == 2);
}

static void
test_next_line_end_of_line_sticky(void)
{
    init("abc\nx\nlonger line\n");
    /* cindex -1 is the "stay at end of line" signal that $ leaves. */
    g.dot    = 2;
    g.cindex = -1;
    g.cmdcnt = 0;
    motion_run_next_line_keep_col_cmd(&g);
    CHECK(g.dot == 5);
    CHECK(g.cindex == -1);
}

static void
test_next_line_at_last_line(void)
{
    init("abc\nx\nlonger line\n");
    /* On the last line, j beeps and leaves dot alone, so a pending
     * operator is not applied. */
    g.dot    = 17;
    g.cmdcnt = 0;
    motion_run_next_line_keep_col_cmd(&g);
    CHECK(g.dot == 17);
    CHECK(errors == 1);
}

static void
test_next_line_count_clamps(void)
{
    init("abc\nx\nlonger line\n");
    g.dot    = 0;
    g.cmdcnt = 2;
    motion_run_next_line_keep_col_cmd(&g);
    CHECK(g.dot == 6);

    /* A count larger than the remaining lines clamps without beeping, so
     * that d9j still deletes to the bottom. */
    init("abc\nx\nlonger line\n");
    g.dot    = 0;
    g.cmdcnt = 9;
    motion_run_next_line_keep_col_cmd(&g);
    CHECK(g.dot == 6);
    CHECK(errors == 0);
}

static void
test_prev_line_keep_col(void)
{
    init("abc\nx\nlonger line\n");
    g.dot    = 6;
    g.cmdcnt = 0;
    motion_run_prev_line_keep_col_cmd(&g);
    CHECK(g.dot == 4);

    /* On the first line, k beeps and stays. */
    init("abc\nx\nlonger line\n");
    g.dot    = 1;
    g.cmdcnt = 0;
    motion_run_prev_line_keep_col_cmd(&g);
    CHECK(g.dot == 1);
    CHECK(errors == 1);
}

/* ---- + and - ------------------------------------------------------------- */

static void
test_next_prev_line_skip_ws(void)
{
    /* a0 b1 \n2 ' '3 ' '4 c5 \n6 d7 \n8 */
    init("ab\n  c\nd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    motion_run_next_line_skip_ws_cmd(&g);
    CHECK(g.dot == 5); /* first non-blank of the next line */

    g.dot    = 7;
    g.cmdcnt = 0;
    motion_run_prev_line_skip_ws_cmd(&g);
    CHECK(g.dot == 5);

    /* Off the end in either direction beeps. */
    g.dot    = 7;
    g.cmdcnt = 0;
    errors   = 0;
    motion_run_next_line_skip_ws_cmd(&g);
    CHECK(errors == 1);
    g.dot    = 0;
    g.cmdcnt = 0;
    errors   = 0;
    motion_run_prev_line_skip_ws_cmd(&g);
    CHECK(errors == 1);
}

/* ---- empty lines: { and } ------------------------------------------------ */

static void
test_next_prev_empty_line(void)
{
    /* a0 b1 \n2 \n3 \n4 c5 d6 \n7 \n8 e9 f10 \n11 — empty lines at 3, 4
     * and 8. */
    init("ab\n\n\ncd\n\nef\n");
    CHECK(next_empty_line(&g, 0) == 3);
    CHECK(next_empty_line(&g, 3) == 4);
    CHECK(next_empty_line(&g, 4) == 8);
    /* Nothing after the last one. */
    CHECK(next_empty_line(&g, 9) == 11);

    CHECK(prev_empty_line(&g, 5) == 4);
    CHECK(prev_empty_line(&g, 4) == 3);
    /* Nothing before the first one. */
    CHECK(prev_empty_line(&g, 0) == -1);
    CHECK(prev_empty_line(&g, 2) == -1);
}

static void
test_empty_line_commands(void)
{
    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    motion_run_next_empty_line_cmd(&g);
    CHECK(g.dot == 3);

    /* A count jumps that many empty lines. */
    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 0;
    g.cmdcnt = 2;
    motion_run_next_empty_line_cmd(&g);
    CHECK(g.dot == 4);

    /* No empty line before the first: beep and stay. */
    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 1;
    g.cmdcnt = 0;
    motion_run_prev_empty_line_cmd(&g);
    CHECK(g.dot == 1);
    CHECK(errors == 1);

    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 9;
    g.cmdcnt = 0;
    motion_run_prev_empty_line_cmd(&g);
    CHECK(g.dot == 8);
    CHECK(errors == 0);
}

/* ---- paragraphs ---------------------------------------------------------- */

static void
test_paragraph_forward(void)
{
    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    /* Returns 0 on success; a run of blank lines counts as one boundary. */
    CHECK(motion_run_paragraph_cmd(&g, '}') == 0);
    CHECK(g.dot == 3);

    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 5;
    g.cmdcnt = 0;
    CHECK(motion_run_paragraph_cmd(&g, '}') == 0);
    CHECK(g.dot == 8);
}

static void
test_paragraph_backward(void)
{
    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 9;
    g.cmdcnt = 0;
    CHECK(motion_run_paragraph_cmd(&g, '{') == 0);
    CHECK(g.dot == 8);

    init("ab\n\n\ncd\n\nef\n");
    g.dot    = 5;
    g.cmdcnt = 0;
    CHECK(motion_run_paragraph_cmd(&g, '{') == 0);
    CHECK(g.dot == 4);
}

static void
test_paragraph_none(void)
{
    /* A buffer with no blank line has no paragraph boundary, so the
     * command reports failure for the caller to turn into a beep. */
    init("ab\ncd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    CHECK(motion_run_paragraph_cmd(&g, '}') == 1);

    init("ab\ncd\n");
    g.dot    = 4;
    g.cmdcnt = 0;
    CHECK(motion_run_paragraph_cmd(&g, '{') == 1);
}

/* ---- f F t T and their repeats ------------------------------------------- */

static void
test_find_char(void)
{
    /* a0 b1 c2 a3 b4 c5 \n6 */
    init("abcabc\n");
    g.dot              = 0;
    g.last_search_char = 'c';
    g.cmdcnt           = 0;
    dot_to_char(&g, 'f');
    CHECK(g.dot == 2);

    /* A count finds the n-th occurrence. */
    init("abcabc\n");
    g.dot              = 0;
    g.last_search_char = 'c';
    g.cmdcnt           = 2;
    dot_to_char(&g, 'f');
    CHECK(g.dot == 5);
}

static void
test_till_char(void)
{
    init("abcabc\n");
    /* t stops one before the target. */
    g.dot              = 0;
    g.last_search_char = 'c';
    g.cmdcnt           = 0;
    dot_to_char(&g, 't');
    CHECK(g.dot == 1);
}

static void
test_find_char_backward(void)
{
    init("abcabc\n");
    g.dot              = 5;
    g.last_search_char = 'a';
    g.cmdcnt           = 0;
    dot_to_char(&g, 'F');
    CHECK(g.dot == 3);

    /* T stops one after the target. */
    init("abcabc\n");
    g.dot              = 5;
    g.last_search_char = 'a';
    g.cmdcnt           = 0;
    dot_to_char(&g, 'T');
    CHECK(g.dot == 4);
}

static void
test_find_char_absent(void)
{
    init("abcabc\n");
    g.dot              = 0;
    g.last_search_char = 'z';
    g.cmdcnt           = 0;
    dot_to_char(&g, 'f');
    /* Not on this line: beep and stay. */
    CHECK(g.dot == 0);
    CHECK(errors == 1);
}

static void
test_find_char_does_not_cross_lines(void)
{
    /* a0 b1 \n2 c3 \n4 — the 'c' is on the next line. */
    init("ab\nc\n");
    g.dot              = 0;
    g.last_search_char = 'c';
    g.cmdcnt           = 0;
    dot_to_char(&g, 'f');
    CHECK(g.dot == 0);
    CHECK(errors == 1);
}

static void
test_find_char_unset(void)
{
    init("abcabc\n");
    /* Nothing searched for yet: silently do nothing, with no beep. */
    g.dot              = 0;
    g.last_search_char = 0;
    g.cmdcnt           = 0;
    dot_to_char(&g, 'f');
    CHECK(g.dot == 0);
    CHECK(errors == 0);
}

static void
test_find_char_cmd_from_ctx(void)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.anchor = 'c';
    ctx.op     = 'f';

    init("abcabc\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    motion_run_find_char_cmd(&g, &ctx);
    CHECK(g.dot == 2);
    /* The target is remembered for ; and , */
    CHECK(g.last_search_char == 'c');
    CHECK(g.last_search_cmd == 'f');
}

static void
test_find_char_cmd_from_terminal(void)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'f'; /* no anchor, so the character comes from the keyboard */

    init("abcabc\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    next_key = 'b';
    motion_run_find_char_cmd(&g, &ctx);
    CHECK(g.last_search_char == 'b');
    CHECK(g.dot == 1);
}

static void
test_repeat_search(void)
{
    init("abcabc\n");
    g.dot              = 0;
    g.last_search_char = 'c';
    g.last_search_cmd  = 'f';
    g.cmdcnt           = 0;
    motion_run_repeat_search_same_cmd(&g);
    CHECK(g.dot == 2);
    g.cmdcnt = 0;
    motion_run_repeat_search_same_cmd(&g);
    CHECK(g.dot == 5);

    /* The reverse repeat flips the case bit, turning f into F. */
    g.cmdcnt = 0;
    motion_run_repeat_search_reverse_cmd(&g);
    CHECK(g.dot == 2);
}

/* ---- screen-relative motions --------------------------------------------- */

static void
test_screen_top_bottom_middle(void)
{
    /* rows is 5, so the text window is four lines starting at
     * screenbegin.  Lines start at 0, 3, 4, 5, 8, 9. */
    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 9;
    g.cmdcnt      = 0;
    motion_run_screen_top_cmd(&g);
    CHECK(g.dot == 0);

    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 0;
    g.cmdcnt      = 0;
    motion_run_screen_bottom_cmd(&g);
    CHECK(g.dot == 5);

    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 0;
    g.cmdcnt      = 0;
    motion_run_screen_middle_cmd(&g);
    CHECK(g.dot == 4);
}

static void
test_screen_top_bottom_count(void)
{
    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 9;
    g.cmdcnt      = 3;
    motion_run_screen_top_cmd(&g);
    CHECK(g.dot == 4); /* the third line of the window */

    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 0;
    g.cmdcnt      = 2;
    motion_run_screen_bottom_cmd(&g);
    CHECK(g.dot == 4); /* the second line up from the window's bottom */
}

static void
test_dot_scroll(void)
{
    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 0;
    dot_scroll(&g, 2, 1);
    CHECK(g.screenbegin == 4);
    /* dot is pulled into the window it can no longer precede. */
    CHECK(g.dot == 4);
    CHECK(commits == 1);

    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 5;
    g.dot         = 9;
    dot_scroll(&g, 1, -1);
    CHECK(g.screenbegin == 4);
    CHECK(g.dot == 9);
}

static void
test_scroll_to_screenpos(void)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op2 = 't'; /* z t: current line to the top */

    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 9;
    motion_run_scroll_to_screenpos_cmd(&g, &ctx);
    CHECK(g.screenbegin == 9);

    /* z . centres the line instead. */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op2 = '.';
    init("ab\n\n\ncd\n\nef\n");
    g.screenbegin = 0;
    g.dot         = 9;
    motion_run_scroll_to_screenpos_cmd(&g, &ctx);
    CHECK(g.screenbegin < 9);
}

/* ---- bound_dot ----------------------------------------------------------- */

static void
test_bound_dot(void)
{
    init("ab\n\n\ncd\n\nef\n"); /* len 12 */
    /* In range: unchanged and silent. */
    CHECK(bound_dot(&g, 0) == 0);
    CHECK(bound_dot(&g, 11) == 11);
    CHECK(errors == 0);

    /* Out of range: clamped, with a beep each time. */
    CHECK(bound_dot(&g, 12) == 11);
    CHECK(bound_dot(&g, 99) == 11);
    CHECK(bound_dot(&g, -1) == 0);
    CHECK(errors == 3);
}

static void
test_bound_dot_snaps_to_codepoint(void)
{
    /* a0 [c3 a9]1,2 b3 \n4 — an offset inside the character snaps back to
     * its first byte. */
    init("a\xc3\xa9""b\n");
    CHECK(bound_dot(&g, 2) == 1);
    CHECK(bound_dot(&g, 1) == 1);
    CHECK(errors == 0);
}

static void
test_bound_dot_empty_buffer(void)
{
    init("");
    /*
     * On an empty buffer the upper clamp is skipped, because it is guarded
     * by text_len > 0, so an out-of-range p comes back unchanged and
     * without a beep.  Pinned as current behaviour; it is a latent
     * out-of-bounds for any caller that then reads at p.
     */
    CHECK(bound_dot(&g, 5) == 5);
    CHECK(errors == 0);
    CHECK(bound_dot(&g, -1) == 0);
    CHECK(errors == 1);
}

/* ---- every motion keeps dot inside the buffer ---------------------------- */

static void
test_motions_stay_in_bounds(void)
{
    static const char *texts[] = {"abc\nde\nf\n", "ab\n\n\ncd\n\nef\n",
                                  "\tx  y\n",     "x\n", "\n"};
    size_t ti;

    for (ti = 0; ti < sizeof(texts) / sizeof(texts[0]); ti++) {
        int len = (int)strlen(texts[ti]);
        int p;

        for (p = 0; p < len; p++) {
            int cnt;

            for (cnt = 0; cnt <= 3; cnt++) {
                struct {
                    void (*f)(struct editor *);
                } moves[] = {
                    {dot_left},
                    {dot_right},
                    {dot_begin},
                    {dot_end},
                    {dot_next},
                    {dot_prev},
                    {dot_skip_over_ws},
                    {motion_run_left_cmd},
                    {motion_run_right_cmd},
                    {motion_run_first_nonblank_cmd},
                    {motion_run_goto_column_cmd},
                    {motion_run_goto_line_cmd},
                    {motion_run_line_end_cmd},
                    {motion_run_next_line_keep_col_cmd},
                    {motion_run_prev_line_keep_col_cmd},
                    {motion_run_next_line_skip_ws_cmd},
                    {motion_run_prev_line_skip_ws_cmd},
                    {motion_run_next_empty_line_cmd},
                    {motion_run_prev_empty_line_cmd},
                    {motion_run_screen_top_cmd},
                    {motion_run_screen_bottom_cmd},
                    {motion_run_screen_middle_cmd},
                };
                size_t mi;

                for (mi = 0; mi < sizeof(moves) / sizeof(moves[0]); mi++) {
                    init(texts[ti]);
                    g.dot         = p;
                    g.cmdcnt      = cnt;
                    g.screenbegin = 0;
                    moves[mi].f(&g);
                    CHECK(g.dot >= 0);
                    CHECK(g.dot <= text_len(&g) - 1);
                }
            }
        }
    }
}

int
main(void)
{
    test_dot_left_right();
    test_dot_left_right_line_boundaries();
    test_dot_left_right_multibyte();
    test_dot_begin_end();
    test_dot_next_prev();
    test_dot_skip_over_ws();
    test_dot_skip_over_ws_stops_at_newline();

    test_move_to_col();
    test_move_to_col_tabs();
    test_move_to_col_second_line();

    test_left_right_cmd_count();
    test_first_nonblank();
    test_line_end();
    test_goto_column();
    test_goto_line();

    test_next_line_keep_col();
    test_next_line_sticky_column();
    test_next_line_end_of_line_sticky();
    test_next_line_at_last_line();
    test_next_line_count_clamps();
    test_prev_line_keep_col();
    test_next_prev_line_skip_ws();

    test_next_prev_empty_line();
    test_empty_line_commands();

    test_paragraph_forward();
    test_paragraph_backward();
    test_paragraph_none();

    test_find_char();
    test_till_char();
    test_find_char_backward();
    test_find_char_absent();
    test_find_char_does_not_cross_lines();
    test_find_char_unset();
    test_find_char_cmd_from_ctx();
    test_find_char_cmd_from_terminal();
    test_repeat_search();

    test_screen_top_bottom_middle();
    test_screen_top_bottom_count();
    test_dot_scroll();
    test_scroll_to_screenpos();

    test_bound_dot();
    test_bound_dot_snaps_to_codepoint();
    test_bound_dot_empty_buffer();

    test_motions_stay_in_bounds();

    SUMMARY();
}
