/*
 * test_vic.c - unit tests for vic.c
 *
 * vic.c is the normal-mode dispatcher, the key table, the put and shift
 * commands, bracket matching, and the cursor/viewport synchronisation.
 * do_cmd() is the single entry point the edit loop uses, so most of these
 * tests press a key and check the buffer, the registers and the cursor.
 *
 * Everything else in the editor is linked for real, including the command
 * parser, so these are integration tests of one keystroke at a time.
 * vic.c defines main(), so it is compiled separately with -Dmain=vic_main
 * (see the Makefile) and this file supplies the test's own main.
 *
 * Only the terminal and keyboard layer is stubbed: clear_to_eol,
 * cookmode, get_one_char, go_bottom_and_clear_to_eol, mysleep,
 * place_cursor, query_screen_dimensions, rawmode, safe_read_key,
 * standout_start, standout_end and the eight term_cursor_shape_*
 * functions.
 *
 * Not covered here, and why:
 *   edit_file and the edit loop  — they read keys from the terminal until
 *                                  :q, so they belong to the PTY harness.
 *   the signal handlers          — they need real signals delivered.
 *   : commands through run_colon_cmd — the prompt reads from the keyboard;
 *                                  ex.c is tested directly instead.
 */
#include "testutil.h"
#include "vic.h"
#include "buffer.h"
#include "operator.h"
#include "pt.h"
#include "screen.h"
#include "text.h"
#include "undo.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;

/* ---- stubs: the terminal and keyboard layer ----------------------------- */

static int next_key = '\n';

void
clear_to_eol(void)
{
}

void
cookmode(struct editor *x)
{
    (void)x;
}

void
rawmode(struct editor *x)
{
    (void)x;
}

int
get_one_char(struct editor *x)
{
    (void)x;
    return next_key;
}

uint64_t
safe_read_key(int fd, char *buffer, int timeout_ms)
{
    (void)fd;
    (void)buffer;
    (void)timeout_ms;
    return (uint64_t)next_key;
}

void
go_bottom_and_clear_to_eol(struct editor *x)
{
    (void)x;
}

void
mysleep(int hundredths)
{
    (void)hundredths;
}

void
place_cursor(struct editor *x, int row, int col)
{
    (void)x;
    (void)row;
    (void)col;
}

int
query_screen_dimensions(struct editor *x)
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

int
term_cursor_shape_get_configured(void)
{
    return 2;
}

int
term_cursor_shape_get_ex(void)
{
    return 2;
}

int
term_cursor_shape_get_insert(void)
{
    return 5;
}

void
term_cursor_shape_init_and_set(void)
{
}

void
term_cursor_shape_restore_startup(void)
{
}

void
term_cursor_shape_set(int shape)
{
    (void)shape;
}

int
term_cursor_shape_set_configured(int shape)
{
    return (shape >= 0 && shape <= 6) ? 0 : -1;
}

int
term_cursor_shape_set_ex(int shape)
{
    return (shape >= 0 && shape <= 6) ? 0 : -1;
}

int
term_cursor_shape_set_insert(int shape)
{
    return (shape >= 0 && shape <= 6) ? 0 : -1;
}

void
term_cursor_shape_update_for_mode(struct editor *x)
{
    (void)x;
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
    free(g.screen);
    free(g.last_search_pattern);
    for (i = 0; i < (int)(sizeof(g.reg) / sizeof(g.reg[0])); i++)
        free(g.reg[i]);
    memset(&g, 0, sizeof(g));
    g.pt                          = ptnew(copy, n);
    g.tabstop                     = 8;
    g.rows                        = 6;
    g.columns                     = 40;
    g.rstart                      = -1;
    g.newindent                   = -1;
    g.visual_anchor               = -1;
    g.last_modified_count         = -1;
    g.line_count_cache_stamp      = INT_MIN;
    g.refresh_last_modified_count = INT_MIN;
    g.refresh_last_screenbegin    = -1;
    g.undo_queue_state            = UNDO_EMPTY;
    g.undo_queue_spos             = -1;
    g.session_epoch               = 424245;
    g.last_search_pattern         = xzalloc(2);
    g.term_orig.c_cc[VERASE]      = 127;
    g.editing                     = 1;
    /*
     * 26 is the unnamed register, which is where reset_ydreg puts ydreg
     * back after every command.  vic.c's real reset_ydreg is linked here,
     * so the fixture has to start in that same steady state or a yank
     * would be written to one register and read back from another.
     */
    g.ydreg = 26;
    for (i = 0; i < (int)(sizeof(g.mark) / sizeof(g.mark[0])); i++)
        g.mark[i] = -1;
    new_screen(&g, (int)g.rows, (int)g.columns);
    next_key = '\n';
    {
        char path[64];

        snprintf(path, sizeof(path), "/tmp/vic-%ld.dat", (long)g.session_epoch);
        unlink(path);
    }
}

static const char *
dump(void)
{
    static char buf[512];
    int n = text_len(&g);

    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    text_read(&g, 0, n, buf);
    buf[n] = '\0';
    return buf;
}

/* Press one key in the current mode. */
static void
key(int c)
{
    do_cmd(&g, c, NULL);
}

/* Press one key with a count prefix, the way the parser would. */
static void
key_n(int c, int count)
{
    g.cmdcnt = count;
    do_cmd(&g, c, NULL);
}

/* Press a key carrying an operator context (dw, rX, 'a …). */
static void
key_ctx(int c, char op, char rg, char anchor)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = op;
    ctx.rg     = rg;
    ctx.anchor = anchor;
    ctx.count  = 1;
    ctx.rcount = 1;
    do_cmd(&g, c, &ctx);
}

/* ---- find_pair ----------------------------------------------------------- */

static void
test_find_pair(void)
{
    /* a0 (1 b2 (3 c4 )5 d6 )7 \n8 */
    init("a(b(c)d)\n");
    /* Forward from an opener to its match, counting nesting. */
    CHECK(find_pair(&g, 1, '(') == 7);
    CHECK(find_pair(&g, 3, '(') == 5);
    /* Backward from a closer. */
    CHECK(find_pair(&g, 7, ')') == 1);
    CHECK(find_pair(&g, 5, ')') == 3);
}

static void
test_find_pair_every_kind(void)
{
    init("[a]\n");
    CHECK(find_pair(&g, 0, '[') == 2);
    init("{a}\n");
    CHECK(find_pair(&g, 0, '{') == 2);
    init("<a>\n");
    CHECK(find_pair(&g, 0, '<') == 2);
}

static void
test_find_pair_unmatched(void)
{
    init("a(b\n");
    CHECK(find_pair(&g, 1, '(') == -1);
    init("a)b\n");
    CHECK(find_pair(&g, 1, ')') == -1);
}

static void
test_find_pair_not_a_bracket(void)
{
    init("abc\n");
    /* A character that is not a bracket has no match. */
    CHECK(find_pair(&g, 1, 'b') == -1);
}

/* ---- sync_cursor --------------------------------------------------------- */

static void
test_sync_cursor_row_and_column(void)
{
    int row = -1, col = -1;

    /* o0..o3 "one\n" t4..t7 "two\n" t8.. "three\n" */
    init("one\ntwo\nthree\n");
    g.screenbegin = 0;
    sync_cursor(&g, 5, &row, &col);
    /* Second line, second column. */
    CHECK(row == 1);
    CHECK(col == 1);

    sync_cursor(&g, 0, &row, &col);
    CHECK(row == 0 && col == 0);
}

static void
test_sync_cursor_scrolls_down(void)
{
    int row = -1, col = -1;
    char buf[128];
    int i;

    /* More lines than the window holds. */
    buf[0] = '\0';
    for (i = 0; i < 20; i++)
        strcat(buf, "x\n");
    init(buf);
    g.screenbegin = 0;
    /* The last line is far below the window, so the view must follow. */
    sync_cursor(&g, text_len(&g) - 2, &row, &col);
    CHECK(g.screenbegin > 0);
    CHECK(row >= 0 && row < (int)g.rows - 1);
}

static void
test_sync_cursor_scrolls_up(void)
{
    int row = -1, col = -1;
    char buf[128];
    int i;

    buf[0] = '\0';
    for (i = 0; i < 20; i++)
        strcat(buf, "x\n");
    init(buf);
    g.screenbegin = 30;
    sync_cursor(&g, 0, &row, &col);
    /* The view follows the cursor back to the top. */
    CHECK(g.screenbegin == 0);
    CHECK(row == 0);
}

static void
test_sync_cursor_tabs(void)
{
    int row = -1, col = -1;

    /* \t0 a1 \n2 — with tabstop 8 the 'a' is at column 8. */
    init("\ta\n");
    g.screenbegin = 0;
    sync_cursor(&g, 1, &row, &col);
    CHECK(col == 8);
}

static void
test_sync_cursor_horizontal_scroll(void)
{
    int row = -1, col = -1;
    char buf[200];
    int i;

    for (i = 0; i < 150; i++)
        buf[i] = 'x';
    buf[150] = '\n';
    buf[151] = '\0';
    init(buf);
    g.columns = 40;
    /* A column past the right edge shifts the horizontal window. */
    sync_cursor(&g, 100, &row, &col);
    CHECK(g.offset > 0);
    CHECK(col >= 0 && col < 40);
}

/* ---- motions through do_cmd --------------------------------------------- */

static void
test_motion_keys(void)
{
    /* a0 b1 c2 \n3 d4 e5 f6 \n7 */
    init("abc\ndef\n");
    g.dot = 1;
    key('h');
    CHECK(g.dot == 0);
    key('l');
    CHECK(g.dot == 1);
    key('j');
    CHECK(g.dot == 5);
    key('k');
    CHECK(g.dot == 1);
    key('0');
    CHECK(g.dot == 0);
    key('$');
    /* dot is nudged off the newline in normal mode. */
    CHECK(g.dot == 2);
}

static void
test_word_motion_keys(void)
{
    init("foo bar baz\n");
    g.dot = 0;
    key('w');
    CHECK(g.dot == 4);
    key('e');
    CHECK(g.dot == 6);
    key('b');
    CHECK(g.dot == 4);
}

static void
test_goto_line_key(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 0;
    key('G');
    /* G with no count goes to the last line. */
    CHECK(g.dot == 8);
    key_n('G', 1);
    CHECK(g.dot == 0);
}

static void
test_count_prefix(void)
{
    init("abcdef\n");
    g.dot = 0;
    /* Digits accumulate into cmdcnt. */
    key('3');
    CHECK(g.cmdcnt == 3);
    key('l');
    CHECK(g.dot == 3);
    /* And the count is cleared by the command that consumed it. */
    CHECK(g.cmdcnt == 0);
}

static void
test_multi_digit_count(void)
{
    char buf[64];
    int i;

    buf[0] = '\0';
    for (i = 0; i < 20; i++)
        strcat(buf, "x\n");
    init(buf);
    g.dot = 0;
    key('1');
    key('2');
    CHECK(g.cmdcnt == 12);
    key('G');
    /* Line 12 starts at offset 22. */
    CHECK(g.dot == 22);
}

static void
test_match_pair_key(void)
{
    init("a(bc)d\n");
    g.dot = 1;
    key('%');
    CHECK(g.dot == 4);
    key('%');
    CHECK(g.dot == 1);
}

/* ---- editing keys ------------------------------------------------------- */

static void
test_delete_char_key(void)
{
    init("abcdef\n");
    g.dot = 2;
    key('x');
    CHECK(strcmp(dump(), "abdef\n") == 0);
    CHECK(strcmp(g.reg[g.ydreg], "c") == 0);
}

static void
test_delete_char_count(void)
{
    init("abcdef\n");
    g.dot = 1;
    key_n('x', 3);
    CHECK(strcmp(dump(), "aef\n") == 0);
}

static void
test_delete_line_key(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    /* dd arrives as one key with the range key already parsed. */
    key_ctx('d', 'd', 'd', 0);
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
}

static void
test_delete_word_key(void)
{
    init("foo bar\n");
    g.dot = 0;
    key_ctx('d', 'd', 'w', 0);
    CHECK(strcmp(dump(), "bar\n") == 0);
}

static void
test_delete_text_object(void)
{
    init("x (ab) y\n");
    g.dot = 4;
    key_ctx('d', 'd', 'i', '(');
    CHECK(strcmp(dump(), "x () y\n") == 0);
}

static void
test_join_key(void)
{
    init("ab\ncd\n");
    g.dot = 0;
    key('J');
    CHECK(strcmp(dump(), "ab cd\n") == 0);
}

static void
test_flip_case_key(void)
{
    init("abc\n");
    g.dot = 0;
    key('~');
    CHECK(strcmp(dump(), "Abc\n") == 0);
    CHECK(g.dot == 1);
}

static void
test_replace_char_key(void)
{
    init("abc\n");
    g.dot = 1;
    key_ctx('r', 'r', 0, 'Z');
    CHECK(strcmp(dump(), "aZc\n") == 0);
}

static void
test_delete_to_eol_key(void)
{
    init("abcd\nef\n");
    g.dot = 2;
    key('D');
    CHECK(strcmp(dump(), "ab\nef\n") == 0);
}

static void
test_open_line_keys(void)
{
    init("ab\ncd\n");
    g.dot = 0;
    key_ctx('o', 'o', 0, 0);
    CHECK(strcmp(dump(), "ab\n\ncd\n") == 0);
    CHECK(g.cmd_mode == 1);

    init("ab\ncd\n");
    g.dot = 4;
    key_ctx('O', 'O', 0, 0);
    CHECK(strcmp(dump(), "ab\n\ncd\n") == 0);
}

/* ---- insert mode -------------------------------------------------------- */

static void
test_insert_mode_typing(void)
{
    init("ac\n");
    g.dot = 1;
    key('i');
    CHECK(g.cmd_mode == 1);
    key('b');
    CHECK(strcmp(dump(), "abc\n") == 0);
    key(ASCII_ESC);
    /* ESC leaves insert mode and steps the cursor back onto the last
     * character typed. */
    CHECK(g.cmd_mode == 0);
    CHECK(g.dot == 1);
}

static void
test_append_mode_typing(void)
{
    init("ac\n");
    g.dot = 0;
    key('a');
    CHECK(g.cmd_mode == 1);
    key('b');
    CHECK(strcmp(dump(), "abc\n") == 0);
    key(ASCII_ESC);
    CHECK(g.cmd_mode == 0);
}

static void
test_insert_typing_is_one_undo_unit(void)
{
    init("ac\n");
    g.dot = 1;
    key('i');
    key('x');
    key('y');
    key('z');
    CHECK(strcmp(dump(), "axyzc\n") == 0);
    key(ASCII_ESC);
    /* The whole insert session undoes in one step. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "ac\n") == 0);
}

static void
test_insert_newline(void)
{
    init("ab\n");
    g.dot = 1;
    key('i');
    key('\n');
    CHECK(strcmp(dump(), "a\nb\n") == 0);
    key(ASCII_ESC);
}

static void
test_insert_backspace(void)
{
    init("ac\n");
    g.dot = 1;
    key('i');
    key('x');
    CHECK(strcmp(dump(), "axc\n") == 0);
    key(127);
    CHECK(strcmp(dump(), "ac\n") == 0);
    key(ASCII_ESC);
}

static void
test_replace_mode(void)
{
    init("abcd\n");
    g.dot = 1;
    key('R');
    CHECK(g.cmd_mode == 2);
    key('X');
    key('Y');
    /* Replace overwrites rather than inserting. */
    CHECK(strcmp(dump(), "aXYd\n") == 0);
    key(ASCII_ESC);
    CHECK(g.cmd_mode == 0);
}

static void
test_replace_mode_backspace_restores(void)
{
    init("abcd\n");
    g.dot = 1;
    key('R');
    key('X');
    CHECK(strcmp(dump(), "aXcd\n") == 0);
    key(127);
    /* Backspace in replace mode puts the overwritten byte back. */
    CHECK(strcmp(dump(), "abcd\n") == 0);
    key(ASCII_ESC);
}

/* ---- put ---------------------------------------------------------------- */

static void
test_put_after_charwise(void)
{
    init("abc\n");
    g.reg[g.ydreg]     = xstrdup("XY");
    g.regtype[g.ydreg] = PARTIAL;
    g.dot              = 0;
    key('p');
    /* p pastes after the cursor. */
    CHECK(strcmp(dump(), "aXYbc\n") == 0);
}

static void
test_put_before_charwise(void)
{
    init("abc\n");
    g.reg[g.ydreg]     = xstrdup("XY");
    g.regtype[g.ydreg] = PARTIAL;
    g.dot              = 1;
    key('P');
    /* P pastes at the cursor. */
    CHECK(strcmp(dump(), "aXYbc\n") == 0);
}

static void
test_put_after_linewise(void)
{
    init("one\ntwo\n");
    g.reg[g.ydreg]     = xstrdup("new\n");
    g.regtype[g.ydreg] = WHOLE;
    g.dot              = 0;
    key('p');
    /* A linewise register goes on its own line below. */
    CHECK(strcmp(dump(), "one\nnew\ntwo\n") == 0);
}

static void
test_put_before_linewise(void)
{
    init("one\ntwo\n");
    g.reg[g.ydreg]     = xstrdup("new\n");
    g.regtype[g.ydreg] = WHOLE;
    g.dot              = 4;
    key('P');
    CHECK(strcmp(dump(), "one\nnew\ntwo\n") == 0);
}

static void
test_put_linewise_at_the_end(void)
{
    init("one\ntwo\n");
    g.reg[g.ydreg]     = xstrdup("new\n");
    g.regtype[g.ydreg] = WHOLE;
    g.dot              = 4; /* the last line */
    key('p');
    CHECK(strcmp(dump(), "one\ntwo\nnew\n") == 0);
}

static void
test_put_with_count(void)
{
    init("abc\n");
    g.reg[g.ydreg]     = xstrdup("X");
    g.regtype[g.ydreg] = PARTIAL;
    g.dot              = 0;
    key_n('p', 3);
    CHECK(strcmp(dump(), "aXXXbc\n") == 0);
}

static void
test_put_empty_register(void)
{
    init("abc\n");
    g.dot = 0;
    key('p');
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(strstr(g.status_buffer, "Nothing in register") != NULL);
}

static void
test_put_is_undoable(void)
{
    init("abc\n");
    g.reg[g.ydreg]     = xstrdup("XY");
    g.regtype[g.ydreg] = PARTIAL;
    g.dot              = 0;
    key('p');
    CHECK(strcmp(dump(), "aXYbc\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_delete_then_put_round_trip(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    key_ctx('d', 'd', 'd', 0);
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
    g.dot = 0;
    key('p');
    /* The deleted line comes back where it was. */
    CHECK(strcmp(dump(), "one\ntwo\nthree\n") == 0);
}

/* ---- shift -------------------------------------------------------------- */

static void
test_shift_right(void)
{
    init("ab\ncd\n");
    g.dot = 0;
    key_ctx('>', '>', '>', 0);
    /* The line gains one indent level. */
    CHECK(dump()[0] == '\t' || dump()[0] == ' ');
    CHECK(strstr(dump(), "ab") != NULL);
}

static void
test_shift_left(void)
{
    init("\tab\n\tcd\n");
    g.dot = 0;
    key_ctx('<', '<', '<', 0);
    CHECK(strcmp(dump(), "ab\n\tcd\n") == 0);
}

static void
test_shift_is_undoable(void)
{
    init("\tab\n");
    g.dot = 0;
    key_ctx('<', '<', '<', 0);
    CHECK(strcmp(dump(), "ab\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "\tab\n") == 0);
}

/* ---- undo and redo keys ------------------------------------------------- */

static void
test_undo_key(void)
{
    init("abc\n");
    g.dot = 0;
    key('x');
    CHECK(strcmp(dump(), "bc\n") == 0);
    key('u');
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_redo_key(void)
{
    init("abc\n");
    g.dot = 0;
    key('x');
    key('u');
    CHECK(strcmp(dump(), "abc\n") == 0);
    key(ASCII_CTRL_R);
    /* Ctrl-R replays what u undid. */
    CHECK(strcmp(dump(), "bc\n") == 0);
}

static void
test_repeat_key(void)
{
    struct cmd_ctx ctx;

    /*
     * '.' replays g->last_cmd_ctx, which the edit loop records as it
     * reads each command from the parser.  Calling do_cmd directly does
     * not record it, so '.' alone does nothing here.
     */
    init("abcdef\n");
    g.dot = 0;
    key('x');
    CHECK(strcmp(dump(), "bcdef\n") == 0);
    key('.');
    CHECK(strcmp(dump(), "bcdef\n") == 0);

    /* With the context recorded, as the edit loop would, it repeats. */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op              = 'x';
    ctx.count           = 1;
    ctx.rcount          = 1;
    g.last_cmd_ctx      = ctx;
    g.has_last_cmd_ctx  = 1;
    key('.');
    CHECK(strcmp(dump(), "cdef\n") == 0);
}

static void
test_repeat_key_with_an_explicit_count(void)
{
    struct cmd_ctx ctx;

    init("abcdef\n");
    g.dot = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op             = 'x';
    ctx.count          = 1;
    ctx.rcount         = 1;
    g.last_cmd_ctx     = ctx;
    g.has_last_cmd_ctx = 1;
    /* A count typed before '.' overrides the one the command had. */
    key_n('.', 3);
    CHECK(strcmp(dump(), "def\n") == 0);
}

/* ---- marks -------------------------------------------------------------- */

static void
test_set_and_jump_mark(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    key_ctx('m', 'm', 0, 'a');
    CHECK(g.mark[0] == 4);

    g.dot = 0;
    key_ctx('\'', '\'', 0, 'a');
    CHECK(g.dot == 4);
}

static void
test_jump_unset_mark(void)
{
    init("one\ntwo\n");
    g.dot = 0;
    key_ctx('\'', '\'', 0, 'z');
    /* An unset mark leaves the cursor alone. */
    CHECK(g.dot == 0);
}

/* ---- visual mode through do_cmd ----------------------------------------- */

static void
test_visual_mode_keys(void)
{
    init("abcdef\n");
    g.dot = 1;
    key('v');
    CHECK(g.visual_mode == 1);
    CHECK(g.visual_anchor == 1);
    /* Extend the selection, then delete it. */
    key('l');
    key('l');
    CHECK(g.dot == 3);
    key('d');
    CHECK(strcmp(dump(), "aef\n") == 0);
    CHECK(g.visual_mode == 0);
}

static void
test_visual_line_mode(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    key('V');
    CHECK(g.visual_mode == 2);
    key('d');
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
}

static void
test_visual_escape_cancels(void)
{
    init("abcdef\n");
    g.dot = 1;
    key('v');
    key('l');
    key(ASCII_ESC);
    CHECK(g.visual_mode == 0);
    /* Nothing was changed. */
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_visual_yank(void)
{
    init("abcdef\n");
    g.dot = 1;
    key('v');
    key('l');
    key('y');
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(strcmp(g.reg[g.ydreg], "bc") == 0);
}

static void
test_visual_block_mode(void)
{
    init("abcd\nefgh\n");
    g.dot = 1;
    key(ASCII_CTRL_V);
    CHECK(g.visual_mode == 3);
    key('j');
    key('d');
    /* Column 1 of both rows goes. */
    CHECK(strcmp(dump(), "acd\negh\n") == 0);
}

/* ---- scrolling keys ----------------------------------------------------- */

static void
test_scroll_keys(void)
{
    char buf[128];
    int i;

    buf[0] = '\0';
    for (i = 0; i < 30; i++)
        strcat(buf, "x\n");
    init(buf);
    g.screenbegin = 0;
    g.dot         = 0;

    key(ASCII_CTRL_E);
    CHECK(g.screenbegin > 0);

    key(ASCII_CTRL_Y);
    CHECK(g.screenbegin == 0);

    key(ASCII_CTRL_F);
    CHECK(g.screenbegin > 0);
    {
        int after_page = g.screenbegin;

        key(ASCII_CTRL_B);
        CHECK(g.screenbegin < after_page);
    }
}

static void
test_scroll_half_page_keys(void)
{
    char buf[128];
    int i;

    buf[0] = '\0';
    for (i = 0; i < 30; i++)
        strcat(buf, "x\n");
    init(buf);
    g.screenbegin = 10;
    g.dot         = 10;
    key(ASCII_CTRL_D);
    CHECK(g.screenbegin > 10);
    {
        int after = g.screenbegin;

        key(ASCII_CTRL_U);
        CHECK(g.screenbegin < after);
    }
}

/* ---- the do_cmd epilogue ------------------------------------------------ */

static void
test_buffer_never_becomes_empty(void)
{
    init("a\n");
    g.dot = 0;
    /* Deleting the only line would leave no buffer at all, so the
     * sentinel newline is put back. */
    key_ctx('d', 'd', 'd', 0);
    CHECK(text_len(&g) == 1);
    CHECK(strcmp(dump(), "\n") == 0);
    CHECK(g.dot == 0);
}

static void
test_dot_is_clamped(void)
{
    init("abc\ndef\n");
    g.dot = 6;
    key_ctx('d', 'd', 'd', 0);
    /* Whatever the command did, dot must stay inside the buffer. */
    CHECK(g.dot >= 0);
    CHECK(g.dot <= text_len(&g));
}

static void
test_dot_snaps_to_a_codepoint_start(void)
{
    /*
     * a0 [c3 a9]1,2 b3 \n4.  The epilogue runs dot through bound_dot,
     * which snaps it to the first byte of its character.  A cursor left
     * inside a multibyte character would make every later read of the
     * character under the cursor wrong, so this is checked with a key
     * that does not move: offset 2 must come back as 1.
     */
    init("a\xc3\xa9" "b\n");
    g.dot = 2;
    key(0x01); /* no binding, so nothing moves dot itself */
    CHECK(g.dot == 1);

    /* And from the other side of the character. */
    init("a\xc3\xa9" "b\n");
    g.dot = 2;
    key('m'); /* set-mark with no name: also leaves dot alone */
    CHECK(g.dot == 1);
}

static void
test_dot_leaves_the_newline(void)
{
    init("abc\ndef\n");
    g.dot = 0;
    key('$');
    /* In normal mode the cursor sits on the last character, not on the
     * line break. */
    CHECK(text_char(&g, g.dot) != '\n');
    CHECK(g.dot == 2);
}

static void
test_dot_stays_on_an_empty_line(void)
{
    /* a0 b1 \n2 \n3 c4 \n5 — the empty line has nothing but its '\n', so
     * the cursor has to rest on it. */
    init("ab\n\nc\n");
    g.dot = 0;
    key('j');
    CHECK(g.dot == 3);
    CHECK(text_char(&g, g.dot) == '\n');
}

static void
test_count_is_reset_by_a_command(void)
{
    init("abcdef\n");
    g.dot = 0;
    key('3');
    CHECK(g.cmdcnt == 3);
    key('x');
    CHECK(g.cmdcnt == 0);
    CHECK(strcmp(dump(), "def\n") == 0);
}

static void
test_jump_records_the_context(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 0;
    /*
     * G is a far motion, so do_cmd pushes the context ring afterwards.
     * check_context runs after the motion, so what lands in
     * mark[MARK_CONTEXT] is the destination and not the origin; the
     * origin ends up one slot deeper on the next jump, which is the slot
     * '' actually reads.
     */
    key('G');
    CHECK(g.mark[MARK_CONTEXT] == 8);
    CHECK(g.mark[MARK_PREV_CONTEXT] == -1);

    /* A second far motion pushes the first destination down, so '' can
     * now return to it. */
    key_n('G', 1);
    CHECK(g.mark[MARK_CONTEXT] == 0);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 8);
}

static void
test_a_near_motion_does_not_record_a_context(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 0;
    /* 'l' is not in the far-motion set, so the ring is untouched. */
    key('l');
    CHECK(g.mark[MARK_CONTEXT] == -1);
}

static void
test_unknown_key_is_harmless(void)
{
    init("abc\n");
    g.dot = 1;
    /* A key with no binding changes nothing. */
    key(0x01);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(g.dot == 1);
}

/* ---- arrow and editing keys always act in normal mode ------------------ */

static void
test_arrow_keys_work_in_insert_mode(void)
{
    init("abc\ndef\n");
    g.dot = 0;
    key('i');
    CHECK(g.cmd_mode == 1);
    key(KEYCODE_DOWN);
    /* The arrow moved the cursor instead of inserting a character. */
    CHECK(strcmp(dump(), "abc\ndef\n") == 0);
    CHECK(g.dot >= 4);
    CHECK(g.cmd_mode == 1);
}

static void
test_delete_key(void)
{
    init("abc\n");
    g.dot = 1;
    key(KEYCODE_DELETE);
    CHECK(strcmp(dump(), "ac\n") == 0);
}

static void
test_home_and_end_keys(void)
{
    init("abc\ndef\n");
    g.dot = 5;
    key(KEYCODE_HOME);
    CHECK(g.dot == 4);
    key(KEYCODE_END);
    CHECK(g.dot == 6);
}

/* ---- every key leaves a consistent editor ------------------------------ */

static void
test_every_key_keeps_the_buffer_valid(void)
{
    static const char *texts[] = {"abc\ndef\n", "a\n", "\n", "ab\n\ncd\n"};
    size_t ti;
    int c;

    /*
     * Press every printable key, plus the control keys the table binds,
     * from a fresh buffer each time.  Nothing may corrupt the piece tree,
     * leave dot out of bounds, or empty the buffer.
     */
    for (ti = 0; ti < sizeof(texts) / sizeof(texts[0]); ti++)
        for (c = 1; c < 128; c++) {
            if (c == ':' || c == '!' || c == 'Z')
                continue; /* these prompt or exit */
            init(texts[ti]);
            g.dot = 0;
            do_cmd(&g, c, NULL);
            CHECK(ptcheck(g.pt) == 1);
            CHECK(text_len(&g) > 0);
            CHECK(g.dot >= 0);
            CHECK(g.dot <= text_len(&g));
        }
}

int
main(void)
{
    static char tmpl[] = "/tmp/vic_test_vic_cache_XXXXXX";

    /* operator.c persists the shared register under XDG_CACHE_HOME, and
     * the screen and status code write to stdout. */
    if (!mkdtemp(tmpl))
        abort();
    setenv("XDG_CACHE_HOME", tmpl, 1);
    if (!freopen("/dev/null", "w", stdout))
        abort();

    test_find_pair();
    test_find_pair_every_kind();
    test_find_pair_unmatched();
    test_find_pair_not_a_bracket();

    test_sync_cursor_row_and_column();
    test_sync_cursor_scrolls_down();
    test_sync_cursor_scrolls_up();
    test_sync_cursor_tabs();
    test_sync_cursor_horizontal_scroll();

    test_motion_keys();
    test_word_motion_keys();
    test_goto_line_key();
    test_count_prefix();
    test_multi_digit_count();
    test_match_pair_key();

    test_delete_char_key();
    test_delete_char_count();
    test_delete_line_key();
    test_delete_word_key();
    test_delete_text_object();
    test_join_key();
    test_flip_case_key();
    test_replace_char_key();
    test_delete_to_eol_key();
    test_open_line_keys();

    test_insert_mode_typing();
    test_append_mode_typing();
    test_insert_typing_is_one_undo_unit();
    test_insert_newline();
    test_insert_backspace();
    test_replace_mode();
    test_replace_mode_backspace_restores();

    test_put_after_charwise();
    test_put_before_charwise();
    test_put_after_linewise();
    test_put_before_linewise();
    test_put_linewise_at_the_end();
    test_put_with_count();
    test_put_empty_register();
    test_put_is_undoable();
    test_delete_then_put_round_trip();

    test_shift_right();
    test_shift_left();
    test_shift_is_undoable();

    test_undo_key();
    test_redo_key();
    test_repeat_key();
    test_repeat_key_with_an_explicit_count();

    test_set_and_jump_mark();
    test_jump_unset_mark();

    test_visual_mode_keys();
    test_visual_line_mode();
    test_visual_escape_cancels();
    test_visual_yank();
    test_visual_block_mode();

    test_scroll_keys();
    test_scroll_half_page_keys();

    test_buffer_never_becomes_empty();
    test_dot_is_clamped();
    test_dot_snaps_to_a_codepoint_start();
    test_dot_leaves_the_newline();
    test_dot_stays_on_an_empty_line();
    test_count_is_reset_by_a_command();
    test_jump_records_the_context();
    test_a_near_motion_does_not_record_a_context();
    test_unknown_key_is_harmless();

    test_arrow_keys_work_in_insert_mode();
    test_delete_key();
    test_home_and_end_keys();

    test_every_key_keeps_the_buffer_valid();

    SUMMARY();
}
