/*
 * test_operator.c - unit tests for operator.c
 *
 * operator.c holds the operators that act on a range (c d y Y x X s C D r
 * ~ and the ! filter) plus the register helpers that visual.c and ex.c
 * call directly (text_yank, what_reg, yank_delete, yank_status).
 *
 * buffer.c, motion.c, undo.c, range.c, textobj.c, editcmd.c and line.c
 * are linked for real, so an operator is checked by the text it leaves
 * behind, the register it filled, and whether one undo reverses it.
 *
 * Stubbed: do_cmd (a programmable move, as in test_range.c, since the
 * real one lives in vic.c), filter_prompt_and_run (records its range
 * instead of running a shell command), find_pair (copied from vic.c),
 * get_motion_char, get_one_char, indicate_error, refresh, reset_ydreg,
 * showmatching, status_line, status_line_bold, status_line_bold_errno.
 *
 * The shared ("+) register persists to $XDG_CACHE_HOME/vic/yank and the
 * active register to /tmp/vic-<epoch>.dat, so main() redirects
 * XDG_CACHE_HOME into a scratch directory before any test runs: a unit
 * test must not write to the user's real cache.
 */
#include "testutil.h"
#include "operator.h"
#include "buffer.h"
#include "pt.h"
#include "text.h"
#include "undo.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;
static int errors;
static int ydreg_resets;
static int status_calls;
static char status_last[256];

/* do_cmd stub: move dot to dc_target (when >= 0), report dc_error. */
static int dc_target = -1;
static int dc_error;

void
do_cmd(struct editor *x, int c, const struct cmd_ctx *ctx)
{
    (void)c;
    (void)ctx;
    if (dc_target >= 0)
        x->dot = dc_target;
    x->cmd_error = dc_error;
}

/* filter_prompt_and_run stub: record the range it was handed. */
static int filter_calls;
static int filter_p, filter_q;
static int filter_modifies;

void
filter_prompt_and_run(struct editor *x, int p, int q, const char *prompt)
{
    (void)prompt;
    filter_calls++;
    filter_p = p;
    filter_q = q;
    if (filter_modifies)
        x->modified_count++;
}

static int motion_key;
static int one_char;

int
get_motion_char(struct editor *x)
{
    (void)x;
    return motion_key;
}

int
get_one_char(struct editor *x)
{
    (void)x;
    return one_char;
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
    ydreg_resets++;
}

void
refresh(struct editor *x, int full_screen)
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
status_line(struct editor *x, const char *format, ...)
{
    va_list ap;

    (void)x;
    status_calls++;
    va_start(ap, format);
    vsnprintf(status_last, sizeof(status_last), format, ap);
    va_end(ap);
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
    g.undo_queue_state       = UNDO_EMPTY;
    g.term_orig.c_cc[VERASE] = 127;
    g.session_epoch          = 424242;
    g.ydreg                  = 0;
    errors         = 0;
    ydreg_resets   = 0;
    status_calls   = 0;
    status_last[0] = '\0';
    dc_target      = -1;
    dc_error       = 0;
    filter_calls   = 0;
    filter_modifies = 0;
    motion_key     = 0;
    one_char       = 0;
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

/* Run an operator with a doubled range key (dd, yy, cc …). */
static void
op_doubled(char op)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op = op;
    ctx.rg = op;
    operator_run_change_delete_yank_cmd(&g, &ctx);
}

/* Run an operator over a text object (diw, da( …). */
static void
op_object(char op, char ai, char obj)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = op;
    ctx.rg     = ai;
    ctx.anchor = obj;
    operator_run_change_delete_yank_cmd(&g, &ctx);
}

/* ---- text_yank and the registers ---------------------------------------- */

static void
test_text_yank(void)
{
    init("abcdef\n");
    /* The range is inclusive, so [1,3] is three bytes. */
    CHECK(text_yank(&g, 1, 3, 5, PARTIAL) == 1);
    CHECK(g.reg[5] != NULL);
    CHECK(strcmp(g.reg[5], "bcd") == 0);
    CHECK(g.regtype[5] == PARTIAL);
    /* The text is not touched by a yank. */
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_text_yank_reversed_range(void)
{
    init("abcdef\n");
    /* A reversed range yanks the same bytes and reports the lower end. */
    CHECK(text_yank(&g, 3, 1, 5, PARTIAL) == 1);
    CHECK(strcmp(g.reg[5], "bcd") == 0);
}

static void
test_text_yank_replaces_the_register(void)
{
    init("abcdef\n");
    text_yank(&g, 0, 0, 5, PARTIAL);
    CHECK(strcmp(g.reg[5], "a") == 0);
    /* The second yank frees the first; a leak here would show under
     * the sanitiser builds. */
    text_yank(&g, 1, 2, 5, PARTIAL);
    CHECK(strcmp(g.reg[5], "bc") == 0);
}

static void
test_what_reg(void)
{
    init("abc\n");
    /* a to z are the named registers. */
    g.ydreg = 0;
    CHECK(what_reg(&g) == 'a');
    g.ydreg = 25;
    CHECK(what_reg(&g) == 'z');
    /* The 'U' register holds the original line for the U command. */
    g.ydreg = ureg;
    CHECK(what_reg(&g) == 'U');
    /* The shared register displays as '+'. */
    g.ydreg = SHARED_REG;
    CHECK(what_reg(&g) == '+');
    /* Anything else is the unnamed register. */
    g.ydreg = 26;
    CHECK(what_reg(&g) == 'D');
}

static void
test_yank_status(void)
{
    init("abc\n");
    g.ydreg = 2;
    yank_status(&g, "Yank", "one\ntwo\n", 1);
    CHECK(status_calls == 1);
    /* Two newlines, eight characters, from register 'c'. */
    CHECK(strstr(status_last, "2 lines") != NULL);
    CHECK(strstr(status_last, "8 chars") != NULL);
    CHECK(strstr(status_last, "[c]") != NULL);

    /* The count multiplies both figures. */
    yank_status(&g, "Delete", "one\n", 3);
    CHECK(strstr(status_last, "3 lines") != NULL);
    CHECK(strstr(status_last, "12 chars") != NULL);
}

/* ---- yank_delete --------------------------------------------------------- */

static void
test_yank_delete_yank_only(void)
{
    init("abcdef\n");
    CHECK(yank_delete(&g, 1, 3, PARTIAL, 0, ALLOW_UNDO, 4) == 1);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(strcmp(g.reg[4], "bcd") == 0);
}

static void
test_yank_delete_deletes(void)
{
    init("abcdef\n");
    CHECK(yank_delete(&g, 1, 3, PARTIAL, 1, ALLOW_UNDO, 4) == 1);
    CHECK(strcmp(dump(), "aef\n") == 0);
    /* The register holds what was removed. */
    CHECK(strcmp(g.reg[4], "bcd") == 0);
    /* And one undo brings it back. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_yank_delete_normalises_the_range(void)
{
    init("abcdef\n");
    yank_delete(&g, 3, 1, PARTIAL, 1, ALLOW_UNDO, 4);
    CHECK(strcmp(dump(), "aef\n") == 0);
}

static void
test_yank_delete_skips_a_newline_start(void)
{
    /* a0 b1 \n2 c3 \n4 — a characterwise range starting on a newline is
     * the empty-line boundary case, and is refused so that dw on an empty
     * line does not swallow the line break. */
    init("ab\nc\n");
    CHECK(yank_delete(&g, 2, 3, PARTIAL, 1, ALLOW_UNDO, 4) == 2);
    CHECK(strcmp(dump(), "ab\nc\n") == 0);
    CHECK(g.reg[4] == NULL);

    /* A linewise range starting on a newline is not refused. */
    init("ab\nc\n");
    yank_delete(&g, 2, 3, WHOLE, 1, ALLOW_UNDO, 4);
    CHECK(strcmp(dump(), "ab\n") == 0);
}

static void
test_yank_delete_current_uses_ydreg(void)
{
    init("abcdef\n");
    g.ydreg = 7;
    yank_delete_current(&g, 1, 3, PARTIAL, TRUE, ALLOW_UNDO);
    CHECK(strcmp(dump(), "aef\n") == 0);
    CHECK(g.reg[7] != NULL && strcmp(g.reg[7], "bcd") == 0);
}

/* ---- dd, yy, cc ---------------------------------------------------------- */

static void
test_delete_line(void)
{
    /* a0 b1 \n2 c3 d4 \n5 e6 f7 \n8 */
    init("ab\ncd\nef\n");
    g.dot = 4;
    op_doubled('d');
    CHECK(strcmp(dump(), "ab\nef\n") == 0);
    /* The register holds the whole line, newline included. */
    CHECK(strcmp(g.reg[0], "cd\n") == 0);
    CHECK(g.regtype[0] == WHOLE);
    /* dd leaves the cursor on the first non-blank of the next line. */
    CHECK(g.dot == 3);
    CHECK(ydreg_resets == 1);
}

static void
test_delete_line_lands_on_first_nonblank(void)
{
    init("ab\n   cd\n");
    g.dot = 0;
    op_doubled('d');
    CHECK(strcmp(dump(), "   cd\n") == 0);
    CHECK(g.dot == 3);
}

static void
test_delete_line_is_undoable(void)
{
    init("ab\ncd\nef\n");
    g.dot = 4;
    op_doubled('d');
    CHECK(strcmp(dump(), "ab\nef\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "ab\ncd\nef\n") == 0);
}

static void
test_yank_line(void)
{
    init("ab\ncd\nef\n");
    g.dot = 4;
    op_doubled('y');
    /* yy leaves the text alone and the cursor where it was. */
    CHECK(strcmp(dump(), "ab\ncd\nef\n") == 0);
    CHECK(strcmp(g.reg[0], "cd\n") == 0);
    CHECK(g.dot == 4);
}

static void
test_change_line(void)
{
    init("ab\ncd\nef\n");
    g.dot = 4;
    op_doubled('c');
    /* cc empties the line and enters insert mode on it. */
    CHECK(strcmp(dump(), "ab\n\nef\n") == 0);
    CHECK(g.cmd_mode == 1);
    CHECK(strcmp(g.reg[0], "cd\n") == 0);
}

static void
test_change_line_is_one_undo_unit(void)
{
    init("ab\ncd\nef\n");
    g.dot = 4;
    op_doubled('c');
    CHECK(strcmp(dump(), "ab\n\nef\n") == 0);
    /* The delete and the replacement newline are chained. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "ab\ncd\nef\n") == 0);
}

static void
test_delete_last_line(void)
{
    init("ab\ncd\n");
    g.dot = 3;
    op_doubled('d');
    CHECK(strcmp(dump(), "ab\n") == 0);
}

static void
test_failed_range_does_nothing(void)
{
    struct cmd_ctx ctx;

    init("abc\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'Z'; /* no such motion */
    operator_run_change_delete_yank_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(g.reg[0] == NULL);
    CHECK(ydreg_resets == 1);
}

/* ---- operators over text objects ----------------------------------------- */

static void
test_delete_inner_word(void)
{
    init("foo bar\n");
    g.dot = 1;
    op_object('d', 'i', 'w');
    CHECK(strcmp(dump(), " bar\n") == 0);
    CHECK(strcmp(g.reg[0], "foo") == 0);
    CHECK(g.regtype[0] == PARTIAL);
}

static void
test_delete_around_word(void)
{
    init("foo bar\n");
    g.dot = 1;
    op_object('d', 'a', 'w');
    CHECK(strcmp(dump(), "bar\n") == 0);
    CHECK(strcmp(g.reg[0], "foo ") == 0);
}

static void
test_delete_inner_parens(void)
{
    init("x (ab cd) y\n");
    g.dot = 5;
    op_object('d', 'i', '(');
    CHECK(strcmp(dump(), "x () y\n") == 0);
    CHECK(strcmp(g.reg[0], "ab cd") == 0);
}

static void
test_change_inner_word_enters_insert(void)
{
    init("foo bar\n");
    g.dot = 1;
    op_object('c', 'i', 'w');
    CHECK(strcmp(dump(), " bar\n") == 0);
    CHECK(g.cmd_mode == 1);
}

static void
test_yank_inner_word_leaves_text(void)
{
    init("foo bar\n");
    g.dot = 1;
    op_object('y', 'i', 'w');
    CHECK(strcmp(dump(), "foo bar\n") == 0);
    CHECK(strcmp(g.reg[0], "foo") == 0);
}

/* ---- x, X and s ---------------------------------------------------------- */

static void
run_simple(char op, int cnt)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op   = op;
    g.cmdcnt = cnt;
    operator_run_delete_or_substitute_cmd(&g, &ctx);
}

static void
test_delete_char(void)
{
    init("abcdef\n");
    g.dot = 2;
    run_simple('x', 0);
    CHECK(strcmp(dump(), "abdef\n") == 0);
    CHECK(strcmp(g.reg[0], "c") == 0);
    CHECK(g.dot == 2);
}

static void
test_delete_char_with_count(void)
{
    init("abcdef\n");
    g.dot = 1;
    run_simple('x', 3);
    CHECK(strcmp(dump(), "aef\n") == 0);
    /* The register holds only the last deletion of the run, because each
     * repetition yanks over the previous one. */
    CHECK(strcmp(g.reg[0], "d") == 0);

    /* The whole run is one undo unit. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_delete_char_stops_at_newline(void)
{
    /* a0 b1 \n2 c3 \n4 — x cannot delete the line break. */
    init("ab\nc\n");
    g.dot = 2;
    run_simple('x', 0);
    CHECK(strcmp(dump(), "ab\nc\n") == 0);

    /* A count that reaches the newline deletes what it can. */
    init("ab\nc\n");
    g.dot = 0;
    run_simple('x', 5);
    CHECK(strcmp(dump(), "\nc\n") == 0);
}

static void
test_delete_char_multibyte(void)
{
    /* a0 [c3 a9]1,2 b3 \n4 — x removes a whole codepoint. */
    init("a\xc3\xa9""b\n");
    g.dot = 1;
    run_simple('x', 0);
    CHECK(strcmp(dump(), "ab\n") == 0);
    CHECK(strcmp(g.reg[0], "\xc3\xa9") == 0);
}

static void
test_delete_char_before(void)
{
    init("abcdef\n");
    g.dot = 3;
    run_simple('X', 0);
    CHECK(strcmp(dump(), "abdef\n") == 0);
    CHECK(strcmp(g.reg[0], "c") == 0);
}

static void
test_delete_char_before_at_line_start(void)
{
    /* X at the start of a line has nothing before it on that line. */
    init("ab\ncd\n");
    g.dot = 3;
    run_simple('X', 0);
    CHECK(strcmp(dump(), "ab\ncd\n") == 0);

    init("ab\n");
    g.dot = 0;
    run_simple('X', 0);
    CHECK(strcmp(dump(), "ab\n") == 0);
}

static void
test_substitute(void)
{
    init("abcdef\n");
    g.dot = 2;
    run_simple('s', 0);
    CHECK(strcmp(dump(), "abdef\n") == 0);
    /* s is x followed by insert mode. */
    CHECK(g.cmd_mode == 1);
}

/* ---- C and D ------------------------------------------------------------- */

static void
test_delete_to_eol(void)
{
    struct cmd_ctx ctx;

    /* a0 b1 c2 d3 \n4 e5 \n6 */
    init("abcd\ne\n");
    g.dot = 2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'D';
    operator_run_change_or_delete_eol_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "ab\ne\n") == 0);
    CHECK(strcmp(g.reg[0], "cd") == 0);
    CHECK(ydreg_resets == 1);
}

static void
test_change_to_eol(void)
{
    struct cmd_ctx ctx;

    init("abcd\ne\n");
    g.dot = 2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'C';
    operator_run_change_or_delete_eol_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "ab\ne\n") == 0);
    /* C enters insert mode rather than clearing the register. */
    CHECK(g.cmd_mode == 1);
    CHECK(ydreg_resets == 0);
}

static void
test_delete_to_eol_is_undoable(void)
{
    struct cmd_ctx ctx;

    init("abcd\ne\n");
    g.dot = 2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'D';
    operator_run_change_or_delete_eol_cmd(&g, &ctx);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcd\ne\n") == 0);
}

/* ---- r ------------------------------------------------------------------- */

static void
run_replace(char anchor, int cnt)
{
    struct cmd_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'r';
    ctx.anchor = anchor;
    g.cmdcnt   = cnt;
    operator_run_replace_char_cmd(&g, &ctx);
}

static void
test_replace_char(void)
{
    init("abcdef\n");
    g.dot = 2;
    run_replace('Z', 0);
    CHECK(strcmp(dump(), "abZdef\n") == 0);
    /* r leaves the cursor on the replaced character. */
    CHECK(g.dot == 2);
}

static void
test_replace_char_with_count(void)
{
    init("abcdef\n");
    g.dot = 1;
    run_replace('Z', 3);
    CHECK(strcmp(dump(), "aZZZef\n") == 0);
    CHECK(g.dot == 3);

    /* One undo reverses the whole run. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

static void
test_replace_char_past_end_of_line(void)
{
    /* a0 b1 \n2 — 5r would run past the line end, so it is refused
     * outright rather than partly applied. */
    init("ab\n");
    g.dot = 0;
    run_replace('Z', 5);
    CHECK(strcmp(dump(), "ab\n") == 0);
    CHECK(errors == 1);
    CHECK(ydreg_resets == 1);
}

static void
test_replace_char_escape_cancels(void)
{
    init("abcdef\n");
    g.dot = 2;
    run_replace(ASCII_ESC, 0);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(errors == 0);
    CHECK(ydreg_resets == 1);
}

static void
test_replace_char_reads_the_terminal(void)
{
    struct cmd_ctx ctx;

    init("abcdef\n");
    g.dot    = 2;
    one_char = 'Q';
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'r'; /* no anchor, so the character comes from the keyboard */
    operator_run_replace_char_cmd(&g, &ctx);
    CHECK(strcmp(dump(), "abQdef\n") == 0);
}

/* ---- ~ ------------------------------------------------------------------- */

static void
test_flip_case(void)
{
    init("aBc\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    operator_run_flip_case_cmd(&g);
    CHECK(strcmp(dump(), "ABc\n") == 0);
    /* The cursor advances past the character it toggled. */
    CHECK(g.dot == 1);
}

static void
test_flip_case_with_count(void)
{
    init("aBc\n");
    g.dot    = 0;
    g.cmdcnt = 3;
    operator_run_flip_case_cmd(&g);
    CHECK(strcmp(dump(), "AbC\n") == 0);

    /* The whole run is one undo unit. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "aBc\n") == 0);
}

static void
test_flip_case_skips_non_letters(void)
{
    init("a1b\n");
    g.dot    = 0;
    g.cmdcnt = 3;
    operator_run_flip_case_cmd(&g);
    /* Digits are left alone; only the letters flip. */
    CHECK(strcmp(dump(), "A1B\n") == 0);
}

static void
test_flip_case_skips_multibyte(void)
{
    /* Case flipping is ASCII-only, so an accented character is left
     * as it is rather than having a byte toggled inside it. */
    init("a\xc3\xa9""b\n");
    g.dot    = 0;
    g.cmdcnt = 3;
    operator_run_flip_case_cmd(&g);
    CHECK(strcmp(dump(), "A\xc3\xa9""B\n") == 0);
}

/* ---- the ! filter operator ----------------------------------------------- */

static void
test_filter_is_always_linewise(void)
{
    struct cmd_ctx ctx;

    /* a0 b1 \n2 c3 d4 \n5 — a characterwise motion still gives the filter
     * whole lines, matching vim. */
    init("ab\ncd\n");
    g.dot     = 1;
    dc_target = 4;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = '!';
    ctx.rg = 'j';
    operator_run_filter_cmd(&g, &ctx);
    CHECK(filter_calls == 1);
    CHECK(filter_p == 0);
    CHECK(filter_q == 5);
}

static void
test_filter_restores_dot_when_nothing_ran(void)
{
    struct cmd_ctx ctx;

    /* The user pressed ESC at the prompt, or the command failed, so the
     * buffer is unchanged and the cursor must go back where it was. */
    init("ab\ncd\n");
    g.dot           = 1;
    dc_target       = 4;
    filter_modifies = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = '!';
    ctx.rg = 'j';
    operator_run_filter_cmd(&g, &ctx);
    CHECK(g.dot == 1);
}

static void
test_filter_keeps_dot_when_the_buffer_changed(void)
{
    struct cmd_ctx ctx;

    init("ab\ncd\n");
    g.dot           = 1;
    dc_target       = 4;
    filter_modifies = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = '!';
    ctx.rg = 'j';
    operator_run_filter_cmd(&g, &ctx);
    CHECK(g.dot == 4);
}

static void
test_filter_failed_range(void)
{
    struct cmd_ctx ctx;

    init("ab\ncd\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = '!';
    ctx.rg = 'Z';
    operator_run_filter_cmd(&g, &ctx);
    CHECK(filter_calls == 0);
    CHECK(ydreg_resets == 1);
}

int
main(void)
{
    /*
     * The shared register writes under XDG_CACHE_HOME.  Point it at a
     * scratch directory so this test cannot touch the user's own yank
     * file, and keep the session temp file out of the way too.
     */
    static char tmpl[] = "/tmp/vic_test_operator_XXXXXX";

    if (!mkdtemp(tmpl))
        abort();
    setenv("XDG_CACHE_HOME", tmpl, 1);

    test_text_yank();
    test_text_yank_reversed_range();
    test_text_yank_replaces_the_register();
    test_what_reg();
    test_yank_status();

    test_yank_delete_yank_only();
    test_yank_delete_deletes();
    test_yank_delete_normalises_the_range();
    test_yank_delete_skips_a_newline_start();
    test_yank_delete_current_uses_ydreg();

    test_delete_line();
    test_delete_line_lands_on_first_nonblank();
    test_delete_line_is_undoable();
    test_yank_line();
    test_change_line();
    test_change_line_is_one_undo_unit();
    test_delete_last_line();
    test_failed_range_does_nothing();

    test_delete_inner_word();
    test_delete_around_word();
    test_delete_inner_parens();
    test_change_inner_word_enters_insert();
    test_yank_inner_word_leaves_text();

    test_delete_char();
    test_delete_char_with_count();
    test_delete_char_stops_at_newline();
    test_delete_char_multibyte();
    test_delete_char_before();
    test_delete_char_before_at_line_start();
    test_substitute();

    test_delete_to_eol();
    test_change_to_eol();
    test_delete_to_eol_is_undoable();

    test_replace_char();
    test_replace_char_with_count();
    test_replace_char_past_end_of_line();
    test_replace_char_escape_cancels();
    test_replace_char_reads_the_terminal();

    test_flip_case();
    test_flip_case_with_count();
    test_flip_case_skips_non_letters();
    test_flip_case_skips_multibyte();

    test_filter_is_always_linewise();
    test_filter_restores_dot_when_nothing_ran();
    test_filter_keeps_dot_when_the_buffer_changed();
    test_filter_failed_range();

    SUMMARY();
}
