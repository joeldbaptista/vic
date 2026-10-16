/*
 * test_editcmd.c - unit tests for editcmd.c
 *
 * editcmd.c holds the keypresses that enter insert or replace mode (i I a
 * A o O R) and the join command (J).  buffer.c, motion.c, undo.c, line.c
 * and codepoint.c are all linked for real, so J is checked by its effect
 * on the text and by undoing it, and the mode-entry commands by where
 * they leave the cursor and the editor mode.
 *
 * Eight symbols are stubbed, all of them things the editor does to the
 * screen or the keyboard that these commands do not depend on:
 * get_one_char, indicate_error, refresh, reset_ydreg, showmatching,
 * status_line, status_line_bold, status_line_bold_errno.  reset_ydreg is
 * counted, because J must clear the yank register.
 *
 * cmd_mode: 0 is command mode, 1 insert, 2 replace.
 */
#include "testutil.h"
#include "editcmd.h"
#include "buffer.h"
#include "pt.h"
#include "text.h"
#include "undo.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static struct editor g;
static int ydreg_resets;

void
status_line(struct editor *x, const char *format, ...)
{
    (void)x;
    (void)format;
}

void
status_line_bold(struct editor *x, const char *format, ...)
{
    (void)x;
    (void)format;
}

void
status_line_bold_errno(struct editor *x, const char *fn)
{
    (void)x;
    (void)fn;
}

void
refresh(struct editor *x, int full_screen)
{
    (void)x;
    (void)full_screen;
}

int
get_one_char(struct editor *x)
{
    (void)x;
    return 0;
}

void
indicate_error(struct editor *x)
{
    (void)x;
}

void
reset_ydreg(struct editor *x)
{
    (void)x;
    ydreg_resets++;
}

void
showmatching(struct editor *x, int p)
{
    (void)x;
    (void)p;
}

/* ---- fixture ------------------------------------------------------------- */

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
    flush_undo_data(&g);
    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt                     = ptnew(copy, n);
    g.tabstop                = 8;
    g.rstart                 = -1;
    g.newindent              = -1;
    g.undo_queue_state       = UNDO_EMPTY;
    g.term_orig.c_cc[VERASE] = 127;
    ydreg_resets             = 0;
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

/* ---- entering insert and replace mode ------------------------------------ */

static void
test_start_insert(void)
{
    init("abc\n");
    g.dot = 1;
    edit_run_start_insert_cmd(&g);
    CHECK(g.cmd_mode == 1);
    /* -1 means "inherit the previous line's indent" when autoindent
     * fires on the first newline. */
    CHECK(g.newindent == -1);
    /* The cursor does not move. */
    CHECK(g.dot == 1);
}

static void
test_start_replace(void)
{
    init("abc\n");
    g.dot = 2;
    edit_run_start_replace_cmd(&g);
    CHECK(g.cmd_mode == 2);
    /* The entry position is recorded so backspace can rewind to it. */
    CHECK(g.rstart == 2);
    CHECK(g.dot == 2);
}

static void
test_insert_commits_the_undo_queue(void)
{
    init("abc\n");
    /* Queue a keystroke, then enter insert mode: the queue must be
     * flushed so the preceding command is one undo unit. */
    string_insert(&g, 1, "X", ALLOW_UNDO_QUEUED);
    CHECK(g.undo_q > 0);
    edit_run_start_insert_cmd(&g);
    CHECK(g.undo_q == 0);
}

static void
test_insert_before(void)
{
    init("abc\n");
    g.dot = 1;
    edit_run_insert_before_cmd(&g);
    /* i inserts at the cursor, so the cursor stays put. */
    CHECK(g.dot == 1);
    CHECK(g.cmd_mode == 1);
}

static void
test_insert_before_first_nonblank(void)
{
    /* ' '0 ' '1 a2 b3 \n4 */
    init("  ab\n");
    g.dot = 3;
    edit_run_insert_before_first_nonblank_cmd(&g);
    CHECK(g.dot == 2);
    CHECK(g.cmd_mode == 1);

    /* On a line with no indent, I is the same as 0i. */
    init("ab\n");
    g.dot = 1;
    edit_run_insert_before_first_nonblank_cmd(&g);
    CHECK(g.dot == 0);
}

static void
test_insert_before_first_nonblank_blank_line(void)
{
    /* ' '0 ' '1 \n2 — a line of only blanks: the cursor stops on the
     * newline rather than running onto the next line. */
    init("  \nx\n");
    g.dot = 0;
    edit_run_insert_before_first_nonblank_cmd(&g);
    CHECK(g.dot == 2);
}

/* ---- a and A ------------------------------------------------------------- */

static void
test_append_after(void)
{
    init("abc\n");
    g.dot = 1;
    edit_run_append_after_cmd(&g);
    /* a appends after the cursor, so the cursor steps one right. */
    CHECK(g.dot == 2);
    CHECK(g.cmd_mode == 1);
}

static void
test_append_after_on_newline(void)
{
    /* On an empty line there is nothing to append after, so the cursor
     * must not step onto the next line. */
    init("ab\n\ncd\n");
    g.dot = 3; /* the empty line's '\n' */
    edit_run_append_after_cmd(&g);
    CHECK(g.dot == 3);
}

static void
test_append_after_multibyte(void)
{
    /* a0 [c3 a9]1,2 b3 \n4 — the step is one codepoint, not one byte. */
    init("a\xc3\xa9""b\n");
    g.dot = 1;
    edit_run_append_after_cmd(&g);
    CHECK(g.dot == 3);
}

static void
test_append_eol(void)
{
    /* a0 b1 c2 \n3 d4 \n5 */
    init("abc\nd\n");
    g.dot = 0;
    edit_run_append_eol_cmd(&g);
    /* A goes to the line's newline, then a's step is suppressed there, so
     * the insert lands before the newline: at the end of the text. */
    CHECK(g.dot == 3);
    CHECK(g.cmd_mode == 1);
}

static void
test_append_eol_empty_line(void)
{
    init("ab\n\ncd\n");
    g.dot = 3;
    edit_run_append_eol_cmd(&g);
    CHECK(g.dot == 3);
}

/* ---- o and O ------------------------------------------------------------- */

static void
test_open_line_below(void)
{
    struct cmd_ctx ctx;

    /* a0 b1 \n2 c3 d4 \n5 */
    init("ab\ncd\n");
    g.dot = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'o';
    edit_run_open_line_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "ab\n\ncd\n") == 0);
    /* The cursor is on the new, empty line. */
    CHECK(g.dot == 3);
    CHECK(g.cmd_mode == 1);
}

static void
test_open_line_above(void)
{
    struct cmd_ctx ctx;

    init("ab\ncd\n");
    g.dot = 4; /* on the second line */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'O';
    edit_run_open_line_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "ab\n\ncd\n") == 0);
    CHECK(g.dot == 3);
    CHECK(g.cmd_mode == 1);
}

static void
test_open_line_above_first_line(void)
{
    struct cmd_ctx ctx;

    init("ab\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'O';
    edit_run_open_line_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "\nab\n") == 0);
    CHECK(g.dot == 0);
}

static void
test_open_line_captures_indent(void)
{
    struct cmd_ctx ctx;

    /*
     * ' '0..3 a4 b5 \n6.  O records the indent of the line it opens above
     * in g->newindent so that char_insert reproduces it.  The capture is
     * only live for the duration of that call: edit_run_start_insert_cmd
     * resets newindent to -1 afterwards.  So the effect is checked in the
     * text, not in the field.
     */
    init("    ab\n");
    g.setops |= VI_AUTOINDENT;
    g.dot = 5;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'O';
    edit_run_open_line_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "    \n    ab\n") == 0);
    CHECK(g.newindent == -1);

    /* Without autoindent the new line is empty. */
    init("    ab\n");
    g.dot = 5;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'O';
    edit_run_open_line_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "\n    ab\n") == 0);
}

static void
test_open_line_below_is_undoable(void)
{
    struct cmd_ctx ctx;

    init("ab\ncd\n");
    g.dot = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'o';
    edit_run_open_line_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "ab\n\ncd\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "ab\ncd\n") == 0);
}

static void
test_open_line_null_ctx_is_below(void)
{
    init("ab\ncd\n");
    g.dot = 0;
    /* With no context there is no 'O', so the default is o. */
    edit_run_open_line_cmd(&g, NULL);
    CHECK(strcmp(dump(), "ab\n\ncd\n") == 0);
}

/* ---- J ------------------------------------------------------------------- */

static void
test_join_lines(void)
{
    /* a0 b1 \n2 c3 d4 \n5 */
    init("ab\ncd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    /* The newline becomes a single space. */
    CHECK(strcmp(dump(), "ab cd\n") == 0);
    /* The cursor is left on the joint. */
    CHECK(g.dot == 3);
    /* J clears the yank register. */
    CHECK(ydreg_resets == 1);
}

static void
test_join_strips_leading_blanks(void)
{
    /* The joined line's indent is replaced by exactly one space. */
    init("ab\n    cd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "ab cd\n") == 0);

    init("ab\n\t\tcd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "ab cd\n") == 0);
}

static void
test_join_empty_next_line(void)
{
    /* a0 b1 \n2 \n3 c4 d5 \n6 — joining an empty line leaves the space. */
    init("ab\n\ncd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "ab \ncd\n") == 0);
}

static void
test_join_with_count(void)
{
    /*
     * At this layer the count is the number of joins performed, so a
     * count of 3 pulls up three lines.  vi's own "3J" joins three lines,
     * which is two joins; the adjustment, if any, is the dispatcher's.
     */
    init("a\nb\nc\nd\n");
    g.dot    = 0;
    g.cmdcnt = 2;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "a b c\nd\n") == 0);

    init("a\nb\nc\nd\n");
    g.dot    = 0;
    g.cmdcnt = 3;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "a b c d\n") == 0);
}

static void
test_join_on_last_line(void)
{
    /* Nothing below to join: the text is left alone. */
    init("ab\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "ab\n") == 0);
}

static void
test_join_count_past_the_end(void)
{
    /* A count larger than the remaining lines joins what it can and then
     * stops, rather than corrupting the tail. */
    init("a\nb\n");
    g.dot    = 0;
    g.cmdcnt = 9;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "a b\n") == 0);
}

static void
test_join_is_one_undo_unit(void)
{
    init("ab\n    cd\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "ab cd\n") == 0);

    /*
     * The join is a swap of the newline plus a chained delete per leading
     * blank, so a single undo must restore the line break and the indent
     * together.
     */
    undo_pop(&g);
    CHECK(strcmp(dump(), "ab\n    cd\n") == 0);
}

static void
test_join_from_mid_line(void)
{
    /* J works on the line the cursor is on, wherever in it the cursor
     * happens to be. */
    init("abc\ndef\n");
    g.dot    = 1;
    g.cmdcnt = 0;
    edit_run_join_lines_cmd(&g);
    CHECK(strcmp(dump(), "abc def\n") == 0);
}

int
main(void)
{
    test_start_insert();
    test_start_replace();
    test_insert_commits_the_undo_queue();
    test_insert_before();
    test_insert_before_first_nonblank();
    test_insert_before_first_nonblank_blank_line();

    test_append_after();
    test_append_after_on_newline();
    test_append_after_multibyte();
    test_append_eol();
    test_append_eol_empty_line();

    test_open_line_below();
    test_open_line_above();
    test_open_line_above_first_line();
    test_open_line_captures_indent();
    test_open_line_below_is_undoable();
    test_open_line_null_ctx_is_below();

    test_join_lines();
    test_join_strips_leading_blanks();
    test_join_empty_next_line();
    test_join_with_count();
    test_join_on_last_line();
    test_join_count_past_the_end();
    test_join_is_one_undo_unit();
    test_join_from_mid_line();

    SUMMARY();
}
