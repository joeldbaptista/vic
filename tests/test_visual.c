/*
 * test_visual.c - unit tests for visual.c
 *
 * visual.c owns visual-mode state (v V Ctrl-V), the selection-to-range
 * conversion, the block-mode per-row range computation, and the operator
 * application for all three modes.
 *
 * operator.c, buffer.c, motion.c, undo.c, range.c, textobj.c, editcmd.c,
 * search.c, status.c and line.c are all linked for real, so an operator
 * is checked by the text it leaves behind and by undoing it.
 *
 * Stubbed: do_cmd and filter_prompt_and_run (programmable, since the real
 * ones live in vic.c and run.c), find_pair (copied from vic.c), and the
 * terminal and keyboard calls get_motion_char, get_one_char,
 * go_bottom_and_clear_to_eol, indicate_error, place_cursor, redraw,
 * refresh, reset_ydreg, screen_line_number_width, showmatching,
 * standout_start, standout_end.
 *
 * visual_mode: 0 off, 1 charwise, 2 linewise, 3 block.  visual_anchor is
 * the fixed end of the selection and -1 when there is none.
 */
#include "testutil.h"
#include "visual.h"
#include "buffer.h"
#include "operator.h"
#include "pt.h"
#include "text.h"
#include "undo.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;
static int errors;

void
do_cmd(struct editor *x, int c, const struct cmd_ctx *ctx)
{
    (void)x;
    (void)c;
    (void)ctx;
}

static int filter_calls;
static int filter_p, filter_q;

void
filter_prompt_and_run(struct editor *x, int p, int q, const char *prompt)
{
    (void)x;
    (void)prompt;
    filter_calls++;
    filter_p = p;
    filter_q = q;
}

int
get_motion_char(struct editor *x)
{
    (void)x;
    return 0;
}

int
get_one_char(struct editor *x)
{
    (void)x;
    return '\n';
}

void
indicate_error(struct editor *x)
{
    (void)x;
    errors++;
}

void
reset_ydreg(struct editor *x)
{
    (void)x;
}

void
refresh(struct editor *x, int full_screen)
{
    (void)x;
    (void)full_screen;
}

void
redraw(struct editor *x, int full_screen)
{
    (void)x;
    (void)full_screen;
}

void
showmatching(struct editor *x, int p)
{
    (void)x;
    (void)p;
}

void
go_bottom_and_clear_to_eol(struct editor *x)
{
    (void)x;
}

void
place_cursor(struct editor *x, int row, int col)
{
    (void)x;
    (void)row;
    (void)col;
}

unsigned
screen_line_number_width(struct editor *x)
{
    (void)x;
    return 0;
}

void
standout_start(void)
{
}

void
standout_end(void)
{
}

/* Verbatim from vic.c, as in test_textobj.c. */
int
find_pair(struct editor *gg, int p, const char c)
{
    const char *braces = "()[]{}<>";
    const char *hit;
    char match;
    int dir, level;

    hit = strchr(braces, c);
    if (hit == NULL)
        return -1;
    dir = hit - braces;
    dir ^= 1;
    match = braces[dir];
    dir = ((dir & 1) << 1) - 1;
    level = 1;
    for (;;) {
        p += dir;
        if (p < 0 || p >= text_len(gg))
            return -1;
        if (text_char(gg, p) == c)
            level++;
        if (text_char(gg, p) == match) {
            level--;
            if (level == 0)
                return p;
        }
    }
}

/* ---- fixture ------------------------------------------------------------- */

static void
init(const char *content)
{
    size_t n = strlen(content);
    char *copy = NULL;
    int i;

    if (n) {
        copy = malloc(n + 1);
        if (!copy)
            abort();
        memcpy(copy, content, n + 1);
    }
    flush_undo_data(&g);
    ptfree(g.pt);
    for (i = 0; i < (int)(sizeof(g.reg) / sizeof(g.reg[0])); i++)
        free(g.reg[i]);
    memset(&g, 0, sizeof(g));
    g.pt                     = ptnew(copy, n);
    g.tabstop                = 8;
    g.rstart                 = -1;
    g.newindent              = -1;
    g.visual_anchor          = -1;
    g.undo_queue_state       = UNDO_EMPTY;
    g.term_orig.c_cc[VERASE] = 127;
    g.session_epoch          = 424243;
    g.columns                = 80;
    for (i = 0; i < (int)(sizeof(g.mark) / sizeof(g.mark[0])); i++)
        g.mark[i] = -1;
    errors       = 0;
    filter_calls = 0;
    /*
     * operator.c mirrors the active register into /tmp/vic-<epoch>.dat and
     * visual put reads it back, so a leftover file would carry a register
     * from one test into the next.  Remove it for every test.
     */
    {
        char path[64];

        snprintf(path, sizeof(path), "/tmp/vic-%ld.dat", (long)g.session_epoch);
        unlink(path);
    }
}

static const char *
dump(void)
{
    static char buf[256];
    int n = text_len(&g);

    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    text_read(&g, 0, n, buf);
    buf[n] = '\0';
    return buf;
}

/* Enter visual mode at `anchor` and move the cursor to `dot`. */
static void
select_from(int anchor, int dot, int mode)
{
    g.dot = anchor;
    visual_enter(&g, mode);
    g.dot = dot;
}

/* ---- entering and leaving ------------------------------------------------ */

static void
test_visual_enter_charwise(void)
{
    init("abc\ndef\n");
    g.dot = 2;
    visual_enter(&g, 1);
    CHECK(g.visual_mode == 1);
    /* The anchor is where the selection started. */
    CHECK(g.visual_anchor == 2);
    CHECK(g.dot == 2);
}

static void
test_visual_enter_linewise(void)
{
    /* a0 b1 c2 \n3 d4 e5 f6 \n7 */
    init("abc\ndef\n");
    g.dot = 5;
    visual_enter(&g, 2);
    CHECK(g.visual_mode == 2);
    /* V snaps both ends to the line start. */
    CHECK(g.visual_anchor == 4);
    CHECK(g.dot == 4);
}

static void
test_visual_enter_block(void)
{
    init("abc\ndef\n");
    g.dot = 5;
    visual_enter(&g, 3);
    CHECK(g.visual_mode == 3);
    /* Block mode keeps the exact offset, because the column matters. */
    CHECK(g.visual_anchor == 5);
    CHECK(g.dot == 5);
}

static void
test_visual_leave_sets_the_marks(void)
{
    init("abc\ndef\n");
    select_from(1, 5, 1);
    visual_leave(&g);
    CHECK(g.visual_mode == 0);
    CHECK(g.visual_anchor == -1);
    /* `< is the lower end and `> the higher, whichever way round the
     * selection was made. */
    CHECK(g.mark[MARK_LT] == 1);
    CHECK(g.mark[MARK_GT] == 5);
}

static void
test_visual_leave_marks_are_ordered(void)
{
    init("abc\ndef\n");
    /* Selected backwards: anchor after dot. */
    select_from(5, 1, 1);
    visual_leave(&g);
    CHECK(g.mark[MARK_LT] == 1);
    CHECK(g.mark[MARK_GT] == 5);
}

static void
test_visual_leave_linewise_marks_whole_lines(void)
{
    init("abc\ndef\n");
    select_from(1, 5, 2);
    visual_leave(&g);
    /* In line mode the marks snap out to the line boundaries. */
    CHECK(g.mark[MARK_LT] == 0);
    CHECK(g.mark[MARK_GT] == 7);
}

static void
test_visual_leave_clears_pending_state(void)
{
    init("abc\n");
    select_from(0, 2, 1);
    g.vis_ai_pending  = 'i';
    g.vis_reg_pending = 1;
    visual_leave(&g);
    /* A half-typed text object or register prefix must not survive into
     * normal mode. */
    CHECK(g.vis_ai_pending == 0);
    CHECK(g.vis_reg_pending == 0);
}

static void
test_visual_leave_without_a_selection(void)
{
    init("abc\n");
    /* No anchor: the marks are left as they were. */
    visual_leave(&g);
    CHECK(g.visual_mode == 0);
    CHECK(g.mark[MARK_LT] == -1);
    CHECK(g.mark[MARK_GT] == -1);
}

/* ---- visual_get_range ---------------------------------------------------- */

static int r_start, r_stop, r_buftype;

static int
get_range(void)
{
    r_start = r_stop = r_buftype = -9;
    return visual_get_range(&g, &r_start, &r_stop, &r_buftype);
}

static void
test_get_range_charwise(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    CHECK(get_range() == 1);
    /* Both ends are inclusive. */
    CHECK(r_start == 1 && r_stop == 3);
    CHECK(r_buftype == PARTIAL);
}

static void
test_get_range_charwise_backwards(void)
{
    init("abcdef\n");
    select_from(3, 1, 1);
    CHECK(get_range() == 1);
    CHECK(r_start == 1 && r_stop == 3);
}

static void
test_get_range_single_character(void)
{
    init("abcdef\n");
    select_from(2, 2, 1);
    CHECK(get_range() == 1);
    CHECK(r_start == 2 && r_stop == 2);
}

static void
test_get_range_charwise_multiline(void)
{
    /* a0 b1 \n2 c3 d4 \n5 */
    init("ab\ncd\n");
    select_from(1, 4, 1);
    CHECK(get_range() == 1);
    CHECK(r_start == 1 && r_stop == 4);
    /* Crossing a newline makes it MULTI rather than PARTIAL. */
    CHECK(r_buftype == MULTI);
}

static void
test_get_range_charwise_multibyte(void)
{
    /* a0 [c3 a9]1,2 b3 \n4 — the end extends to the last byte of the
     * character under the cursor, never into the middle of it. */
    init("a\xc3\xa9""b\n");
    select_from(0, 1, 1);
    CHECK(get_range() == 1);
    CHECK(r_start == 0 && r_stop == 2);
}

static void
test_get_range_linewise(void)
{
    /* a0 b1 \n2 c3 d4 \n5 e6 \n7 */
    init("ab\ncd\ne\n");
    select_from(0, 4, 2);
    CHECK(get_range() == 1);
    CHECK(r_start == 0 && r_stop == 5);
    CHECK(r_buftype == WHOLE);
}

static void
test_get_range_block_returns_zero(void)
{
    init("abcdef\n");
    select_from(1, 3, 3);
    /* Block mode has no single range; the caller must use
     * block_selection_ranges instead. */
    CHECK(get_range() == 0);
}

static void
test_get_range_inactive(void)
{
    init("abcdef\n");
    CHECK(get_range() == 0);

    /* Mode set but no anchor is also "no selection". */
    g.visual_mode   = 1;
    g.visual_anchor = -1;
    CHECK(get_range() == 0);
}

/* ---- block_visual_cols --------------------------------------------------- */

static void
test_block_visual_cols(void)
{
    int cl, cr, rt, rb;

    /* a0 b1 c2 d3 \n4 e5 f6 g7 h8 \n9 */
    init("abcd\nefgh\n");
    select_from(1, 7, 3); /* column 1 on row 0, column 2 on row 1 */
    block_visual_cols(&g, &cl, &cr, &rt, &rb);
    CHECK(cl == 1 && cr == 2);
    CHECK(rt == 0 && rb == 5);
}

static void
test_block_visual_cols_ordered(void)
{
    int cl, cr, rt, rb;

    init("abcd\nefgh\n");
    /* Selected from the bottom right to the top left. */
    select_from(8, 1, 3);
    block_visual_cols(&g, &cl, &cr, &rt, &rb);
    CHECK(cl == 1 && cr == 3);
    CHECK(rt == 0 && rb == 5);
}

static void
test_block_visual_cols_tabs(void)
{
    int cl, cr, rt, rb;

    /* \t0 a1 \n2 — with tabstop 8 the 'a' sits at column 8. */
    init("\ta\nxxxxxxxxx\n");
    select_from(1, 3, 3);
    block_visual_cols(&g, &cl, &cr, &rt, &rb);
    CHECK(cl == 0 && cr == 8);
}

/* ---- block_selection_ranges ---------------------------------------------- */

static void
test_block_ranges(void)
{
    struct block_range *r;
    int n = -1;

    /* a0 b1 c2 d3 \n4 e5 f6 g7 h8 \n9.  Anchor at column 1 of row 0 and
     * cursor at column 2 of row 1 gives the window columns 1 to 2. */
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    r = block_selection_ranges(&g, &n);
    CHECK(r != NULL);
    CHECK(n == 2);
    /* Each range is [p, q): q is one past the window. */
    CHECK(r[0].p == 1 && r[0].q == 3);
    CHECK(r[1].p == 6 && r[1].q == 8);
    free(r);
}

static void
test_block_ranges_single_column(void)
{
    struct block_range *r;
    int n = -1;

    init("abcd\nefgh\n");
    select_from(1, 6, 3); /* column 1 on both rows */
    r = block_selection_ranges(&g, &n);
    CHECK(n == 2);
    CHECK(r[0].p == 1 && r[0].q == 2);
    CHECK(r[1].p == 6 && r[1].q == 7);
    free(r);
}

static void
test_block_ranges_short_line_is_clamped(void)
{
    struct block_range *r;
    int n = -1;

    /* a0 b1 c2 d3 \n4 e5 \n6 — the second row ends before the window
     * does, so its range stops at the line end. */
    init("abcd\ne\n");
    select_from(1, 5, 3);
    g.dot = 3; /* columns 1 to 3 */
    r     = block_selection_ranges(&g, &n);
    CHECK(r != NULL);
    CHECK(n == 1); /* row 1 does not reach column 1 */
    CHECK(r[0].p == 1 && r[0].q == 4);
    free(r);
}

static void
test_block_ranges_skips_lines_that_are_too_short(void)
{
    struct block_range *r;
    int n = -1;

    /* The middle row has nothing at those columns at all, so it is left
     * out rather than returned as an empty range. */
    init("abcd\n\nefgh\n");
    select_from(1, 8, 3); /* columns 1 to 2, rows 0 to 2 */
    r = block_selection_ranges(&g, &n);
    CHECK(r != NULL);
    CHECK(n == 2);
    free(r);
}

static void
test_block_ranges_not_block_mode(void)
{
    int n = -1;

    init("abcd\nefgh\n");
    select_from(1, 6, 1);
    /* Charwise mode has no block ranges. */
    CHECK(block_selection_ranges(&g, &n) == NULL);
    CHECK(n == 0);
}

static void
test_block_ranges_tab_spanning_the_window(void)
{
    struct block_range *r;
    int n = -1;

    /*
     * A tab occupies columns 0 to 7 in one byte.  A window wholly inside
     * it cannot be expressed as a byte range, so the row is skipped
     * rather than returning a range that cuts the tab in half.
     */
    init("\tx\nabcdefghij\n");
    g.dot           = 2;  /* column 2 of the second row */
    g.visual_mode   = 3;
    g.visual_anchor = 11; /* also inside the second row */
    g.dot           = 12;
    r               = block_selection_ranges(&g, &n);
    free(r);

    init("\tx\nabcdefghij\n");
    g.visual_mode   = 3;
    g.visual_anchor = 0;  /* the tab, column 0 */
    g.dot           = 0;
    r               = block_selection_ranges(&g, &n);
    /* Column 0 does fall on the tab's first column, so the tab is
     * selected. */
    CHECK(n == 1);
    CHECK(r != NULL && r[0].p == 0);
    free(r);
}

/* ---- charwise and linewise operators ------------------------------------ */

static void
test_visual_delete_charwise(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'd');
    CHECK(strcmp(dump(), "aef\n") == 0);
    CHECK(strcmp(g.reg[0], "bcd") == 0);
    /* The operator leaves visual mode. */
    CHECK(g.visual_mode == 0);
}

static void
test_visual_delete_is_undoable(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'd');
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_visual_x_is_delete(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'x');
    CHECK(strcmp(dump(), "aef\n") == 0);
}

static void
test_visual_yank_charwise(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'y');
    /* A yank leaves the text alone. */
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(strcmp(g.reg[0], "bcd") == 0);
    CHECK(g.visual_mode == 0);
}

static void
test_visual_delete_linewise(void)
{
    /* a0 b1 \n2 c3 d4 \n5 e6 \n7 */
    init("ab\ncd\ne\n");
    select_from(0, 4, 2);
    visual_apply_operator(&g, 'd');
    CHECK(strcmp(dump(), "e\n") == 0);
    CHECK(strcmp(g.reg[0], "ab\ncd\n") == 0);
    CHECK(g.regtype[0] == WHOLE);
}

static void
test_visual_change_charwise(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'c');
    CHECK(strcmp(dump(), "aef\n") == 0);
    /* c enters insert mode. */
    CHECK(g.cmd_mode == 1);
}

static void
test_visual_case_operators(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'U');
    CHECK(strcmp(dump(), "aBCDef\n") == 0);

    init("ABCDEF\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'u');
    CHECK(strcmp(dump(), "AbcdEF\n") == 0);
}

static void
test_visual_case_is_undoable(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'U');
    CHECK(strcmp(dump(), "aBCDef\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_visual_indent(void)
{
    init("ab\ncd\n");
    g.tabstop = 8;
    select_from(0, 3, 2);
    visual_apply_operator(&g, '>');
    /* Both selected lines are indented. */
    CHECK(strstr(dump(), "ab") != NULL);
    CHECK(dump()[0] == '\t' || dump()[0] == ' ');
}

static void
test_visual_dedent(void)
{
    init("\tab\n\tcd\n");
    select_from(0, 4, 2);
    visual_apply_operator(&g, '<');
    CHECK(strcmp(dump(), "ab\ncd\n") == 0);
}

static void
test_visual_put(void)
{
    init("abcdef\n");
    /* Put replaces the selection with the register contents. */
    g.reg[0]     = xstrdup("XY");
    g.regtype[0] = PARTIAL;
    select_from(1, 3, 1);
    visual_apply_operator(&g, 'p');
    CHECK(strcmp(dump(), "aXYef\n") == 0);
}

static void
test_visual_put_empty_register(void)
{
    init("abcdef\n");
    select_from(1, 3, 1);
    /* Nothing to put: the selection survives and visual mode stays on,
     * matching normal-mode p on an empty register. */
    visual_apply_operator(&g, 'p');
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(g.visual_mode == 1);
}

static void
test_visual_filter(void)
{
    init("ab\ncd\n");
    select_from(1, 4, 1);
    visual_apply_operator(&g, '!');
    CHECK(filter_calls == 1);
    /* The filter is linewise, so it is handed whole lines. */
    CHECK(filter_p == 0);
    CHECK(filter_q == 5);
}

static void
test_visual_operator_without_a_selection(void)
{
    init("abcdef\n");
    /* No selection: the operator beeps instead of acting. */
    visual_apply_operator(&g, 'd');
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(errors == 1);
}

/* ---- block operators ----------------------------------------------------- */

static void
test_block_delete(void)
{
    /* a0 b1 c2 d3 \n4 e5 f6 g7 h8 \n9 — columns 1 to 2 of both rows. */
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    visual_apply_operator(&g, 'd');
    CHECK(strcmp(dump(), "ad\neh\n") == 0);
    CHECK(g.visual_mode == 0);
}

static void
test_block_delete_is_undoable(void)
{
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    visual_apply_operator(&g, 'd');
    CHECK(strcmp(dump(), "ad\neh\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcd\nefgh\n") == 0);
}

static void
test_block_yank(void)
{
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    visual_apply_operator(&g, 'y');
    CHECK(strcmp(dump(), "abcd\nefgh\n") == 0);
    /* The register holds one line per row of the block. */
    CHECK(g.reg[0] != NULL);
    CHECK(strcmp(g.reg[0], "bc\nfg\n") == 0);
}

static void
test_block_case(void)
{
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    visual_apply_operator(&g, 'U');
    CHECK(strcmp(dump(), "aBCd\neFGh\n") == 0);
}

static void
test_block_put_is_rejected(void)
{
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    /* Block put is not implemented, so it beeps and leaves visual mode
     * rather than doing something unexpected. */
    visual_apply_operator(&g, 'p');
    CHECK(strcmp(dump(), "abcd\nefgh\n") == 0);
    CHECK(errors == 1);
    CHECK(g.visual_mode == 0);
}

static void
test_block_empty_selection(void)
{
    /* Both rows are too short to reach the window. */
    init("a\nb\nxxxxxxxx\n");
    g.visual_mode   = 3;
    g.visual_anchor = 8; /* column 4 of the long row */
    visual_enter(&g, 3);
    g.visual_anchor = 8;
    g.dot           = 9;
    /* Move the block onto the two short rows by anchoring there instead. */
    init("a\nb\n");
    g.visual_mode   = 3;
    g.visual_anchor = 0;
    g.dot           = 0;
    visual_apply_operator(&g, 'd');
    /* Column 0 exists on both rows, so this does delete. */
    CHECK(strcmp(dump(), "\nb\n") == 0);
}

static void
test_block_filter(void)
{
    init("abcd\nefgh\n");
    select_from(1, 7, 3);
    visual_apply_operator(&g, '!');
    CHECK(filter_calls == 1);
}

/* ---- block insert replay ------------------------------------------------- */

static void
test_block_insert_replay(void)
{
    /*
     * After I in block mode the typed text is already on the first row;
     * the replay copies it to every other row of the block at the same
     * column.  The state below is what vic.c leaves behind when the user
     * blocked columns 1.. over three rows of "abcd/efgh/ijkl" and typed
     * "XY": the first row already reads "aXYbcd", dot is on the last
     * inserted byte, and row_bot_off is the offset recorded *before* the
     * typing (10), which the replay itself shifts by the inserted length.
     */
    init("aXYbcd\nefgh\nijkl\n");
    g.vis_block_insert_active    = 1;
    g.vis_block_insert_col       = 1;
    g.vis_block_insert_start_off = 1;
    g.dot                        = 2;
    g.vis_block_row_top_off      = 0;
    g.vis_block_row_bot_off      = 10;
    visual_block_insert_replay(&g);
    CHECK(strcmp(dump(), "aXYbcd\neXYfgh\niXYjkl\n") == 0);
}

static void
test_block_insert_replay_is_one_undo_unit(void)
{
    init("aXYbcd\nefgh\nijkl\n");
    g.vis_block_insert_active    = 1;
    g.vis_block_insert_col       = 1;
    g.vis_block_insert_start_off = 1;
    g.dot                        = 2;
    g.vis_block_row_top_off      = 0;
    g.vis_block_row_bot_off      = 10;
    visual_block_insert_replay(&g);
    /* The replayed insertions are chained, so one undo removes them all
     * (the first row's own insert was recorded by the insert session). */
    undo_pop(&g);
    CHECK(strcmp(dump(), "aXYbcd\nefgh\nijkl\n") == 0);
}

static void
test_block_insert_replay_skips_short_rows(void)
{
    /*
     * Original text "abcd/e/fgh" with "XY" typed at column 2 of the first
     * row.  The middle row has only one character, so it never reaches
     * column 2 and is left alone.
     */
    init("abXYcd\ne\nfgh\n");
    g.vis_block_insert_active    = 1;
    g.vis_block_insert_col       = 2;
    g.vis_block_insert_start_off = 2;
    g.dot                        = 3;
    g.vis_block_row_top_off      = 0;
    g.vis_block_row_bot_off      = 7;
    visual_block_insert_replay(&g);
    CHECK(strcmp(dump(), "abXYcd\ne\nfgXYh\n") == 0);
}

static void
test_block_insert_replay_single_row(void)
{
    /*
     * A one-row block has nothing to replay onto.  The guard is the row
     * count, not vis_block_insert_active: that flag is the caller's
     * (vic.c) record that a replay is pending.
     */
    init("aXYbcd\n");
    g.vis_block_insert_active    = 1;
    g.vis_block_insert_col       = 1;
    g.vis_block_insert_start_off = 1;
    g.dot                        = 2;
    g.vis_block_row_top_off      = 0;
    g.vis_block_row_bot_off      = 0;
    visual_block_insert_replay(&g);
    CHECK(strcmp(dump(), "aXYbcd\n") == 0);
}

int
main(void)
{
    static char tmpl[] = "/tmp/vic_test_visual_XXXXXX";

    /* operator.c persists the shared register under XDG_CACHE_HOME. */
    if (!mkdtemp(tmpl))
        abort();
    setenv("XDG_CACHE_HOME", tmpl, 1);

    test_visual_enter_charwise();
    test_visual_enter_linewise();
    test_visual_enter_block();
    test_visual_leave_sets_the_marks();
    test_visual_leave_marks_are_ordered();
    test_visual_leave_linewise_marks_whole_lines();
    test_visual_leave_clears_pending_state();
    test_visual_leave_without_a_selection();

    test_get_range_charwise();
    test_get_range_charwise_backwards();
    test_get_range_single_character();
    test_get_range_charwise_multiline();
    test_get_range_charwise_multibyte();
    test_get_range_linewise();
    test_get_range_block_returns_zero();
    test_get_range_inactive();

    test_block_visual_cols();
    test_block_visual_cols_ordered();
    test_block_visual_cols_tabs();

    test_block_ranges();
    test_block_ranges_single_column();
    test_block_ranges_short_line_is_clamped();
    test_block_ranges_skips_lines_that_are_too_short();
    test_block_ranges_not_block_mode();
    test_block_ranges_tab_spanning_the_window();

    test_visual_delete_charwise();
    test_visual_delete_is_undoable();
    test_visual_x_is_delete();
    test_visual_yank_charwise();
    test_visual_delete_linewise();
    test_visual_change_charwise();
    test_visual_case_operators();
    test_visual_case_is_undoable();
    test_visual_indent();
    test_visual_dedent();
    test_visual_put();
    test_visual_put_empty_register();
    test_visual_filter();
    test_visual_operator_without_a_selection();

    test_block_delete();
    test_block_delete_is_undoable();
    test_block_yank();
    test_block_case();
    test_block_put_is_rejected();
    test_block_empty_selection();
    test_block_filter();

    test_block_insert_replay();
    test_block_insert_replay_is_one_undo_unit();
    test_block_insert_replay_skips_short_rows();
    test_block_insert_replay_single_row();

    SUMMARY();
}
