/*
 * test_screen.c - unit tests for screen.c
 *
 * screen.c maintains the shadow screen buffer and diffs it against newly
 * formatted lines so that only changed rows reach the terminal.  The
 * tests therefore inspect the shadow buffer (g->screen) and the cached
 * refresh state, which is where everything observable ends up; the actual
 * terminal writes go to stdout, which main() redirects.
 *
 * The colorizer registry, visual.c and the whole editing core are linked
 * for real, so format_line renders with real syntax colouring and real
 * visual selections.  Fifteen symbols are stubbed, all of them terminal
 * or keyboard calls: clear_to_eol and place_cursor (counted, because they
 * mark an actual row repaint), query_screen_dimensions (returns the
 * fixture's size), sync_cursor (returns programmed row/column), plus
 * do_cmd, filter_prompt_and_run, find_pair, get_motion_char,
 * get_one_char, go_bottom_and_clear_to_eol, indicate_error, reset_ydreg,
 * showmatching, standout_start and standout_end.
 *
 * format_line itself is static, so it is exercised through refresh and
 * checked by reading the rendered row back out of the shadow buffer.
 */
#include "testutil.h"
#include "screen.h"
#include "buffer.h"
#include "pt.h"
#include "status.h"
#include "text.h"
#include "visual.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;
static int clear_calls;
static int place_calls;
static int last_place_row, last_place_col;
static int sync_row, sync_col; /* what sync_cursor reports */
static int sync_calls;
static int sync_last_dot;

void
clear_to_eol(void)
{
    clear_calls++;
}

void
place_cursor(struct editor *x, int row, int col)
{
    (void)x;
    place_calls++;
    last_place_row = row;
    last_place_col = col;
}

int
query_screen_dimensions(struct editor *x)
{
    (void)x;
    return 0; /* leave g->rows and g->columns as the fixture set them */
}

void
sync_cursor(struct editor *x, int d, int *row, int *col)
{
    (void)x;
    sync_calls++;
    sync_last_dot = d;
    *row          = sync_row;
    *col          = sync_col;
}

void
go_bottom_and_clear_to_eol(struct editor *x)
{
    (void)x;
}

void
standout_start(void)
{
}

void
standout_end(void)
{
}

void
do_cmd(struct editor *x, int c, const struct cmd_ctx *ctx)
{
    (void)x;
    (void)c;
    (void)ctx;
}

void
filter_prompt_and_run(struct editor *x, int p, int q, const char *prompt)
{
    (void)x;
    (void)p;
    (void)q;
    (void)prompt;
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
}

void
showmatching(struct editor *x, int p)
{
    (void)x;
    (void)p;
}

int
find_pair(struct editor *x, int p, const char c)
{
    (void)x;
    (void)p;
    (void)c;
    return -1;
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
    ptfree(g.pt);
    free(g.screen);
    free(g.highlight_pattern);
    memset(&g, 0, sizeof(g));
    g.pt                           = ptnew(copy, n);
    g.tabstop                      = 8;
    g.rows                         = 6;
    g.columns                      = 40;
    g.visual_anchor                = -1;
    g.last_modified_count          = -1;
    g.line_count_cache_stamp       = INT_MIN;
    g.refresh_last_modified_count  = INT_MIN;
    g.refresh_last_screenbegin     = -1;
    g.undo_queue_state             = UNDO_EMPTY;
    g.get_rowcol_error             = 0;
    for (i = 0; i < (int)(sizeof(g.mark) / sizeof(g.mark[0])); i++)
        g.mark[i] = -1;
    new_screen(&g, (int)g.rows, (int)g.columns);
    clear_calls = place_calls = sync_calls = 0;
    sync_row = sync_col = 0;
    sync_last_dot       = -1;
}

/* The shadow-buffer row li, as a string. */
static const char *
row(int li)
{
    return &g.screen[li * g.screen_line_size];
}

/*
 * True when row li holds exactly `text`, ignoring the trailing spaces
 * that format_line pads every row out to the terminal width with.
 */
static int
row_is(int li, const char *text)
{
    const char *r = row(li);
    size_t n = strlen(text);

    if (strncmp(r, text, n) != 0)
        return 0;
    for (r += n; *r; r++)
        if (*r != ' ')
            return 0;
    return 1;
}

/* The number of rows refresh draws into: the last one is the status
 * line, which show_status_line owns. */
static int
text_rows(void)
{
    return (int)g.rows - 1;
}

/* ---- new_screen and screen_erase ---------------------------------------- */

static void
test_new_screen_sizes(void)
{
    init("abc\n");
    new_screen(&g, 10, 20);
    /* Four bytes per column for expanded UTF-8, plus slack. */
    CHECK(g.screen_line_size == 20 * 4 + SCREEN_LINE_SLACK);
    CHECK(g.screensize == 10 * g.screen_line_size);
    CHECK(g.screen != NULL);
}

static void
test_new_screen_starts_blank(void)
{
    init("abc\n");
    new_screen(&g, 4, 20);
    g.rows = 4;
    /* Every row compares as empty, so the first refresh repaints all of
     * them. */
    CHECK(row(0)[0] == '\0');
    CHECK(row(3)[0] == '\0');
}

static void
test_new_screen_reallocates(void)
{
    init("abc\n");
    new_screen(&g, 4, 20);
    new_screen(&g, 8, 60);
    /* A resize replaces the buffer rather than growing it in place. */
    CHECK(g.screen_line_size == 60 * 4 + SCREEN_LINE_SLACK);
    CHECK(g.screensize == 8 * g.screen_line_size);
}

static void
test_screen_erase(void)
{
    init("one\ntwo\nthree\n");
    refresh(&g, TRUE);
    /* Something was drawn. */
    CHECK(row(0)[0] != '\0');

    screen_erase(&g);
    /* Every row is blanked, which is what forces the next refresh to
     * repaint in full. */
    CHECK(row(0)[0] == '\0');
    CHECK(row((int)g.rows - 1)[0] == '\0');
}

/* ---- screen_line_number_width ------------------------------------------- */

static void
test_line_number_width_off(void)
{
    init("one\ntwo\n");
    /* Neither number nor relativenumber: no gutter at all. */
    CHECK(screen_line_number_width(&g) == 0);
}

static void
test_line_number_width_digits(void)
{
    init("a\nb\nc\n");
    g.setops |= VI_NUMBER;
    /* Three lines: one digit plus the separating space. */
    CHECK(screen_line_number_width(&g) == 2);
}

static void
test_line_number_width_grows(void)
{
    char buf[4096];
    int i;

    /* 12 lines needs two digits, so the gutter is three columns. */
    buf[0] = '\0';
    for (i = 0; i < 12; i++)
        strcat(buf, "x\n");
    init(buf);
    g.setops |= VI_NUMBER;
    CHECK(screen_line_number_width(&g) == 3);

    /* 120 lines needs three. */
    buf[0] = '\0';
    for (i = 0; i < 120; i++)
        strcat(buf, "x\n");
    init(buf);
    g.setops |= VI_NUMBER;
    CHECK(screen_line_number_width(&g) == 4);
}

static void
test_line_number_width_relativenumber(void)
{
    init("a\nb\n");
    /* relativenumber alone also reserves the gutter. */
    g.setops |= VI_RELATIVENUMBER;
    CHECK(screen_line_number_width(&g) == 2);
}

/* ---- screen_text_columns_on_screen -------------------------------------- */

static void
test_text_columns(void)
{
    init("a\nb\n");
    g.columns = 40;
    /* No gutter: the whole width is text. */
    CHECK(screen_text_columns_on_screen(&g) == 40);

    g.setops |= VI_NUMBER;
    /* The gutter comes off the text width. */
    CHECK(screen_text_columns_on_screen(&g) == 38);
}

static void
test_text_columns_never_zero(void)
{
    init("a\nb\n");
    g.setops |= VI_NUMBER;
    g.columns = 1;
    /* The gutter is wider than the terminal, but there must always be a
     * column to put text in. */
    CHECK(screen_text_columns_on_screen(&g) == 1);

    g.columns = 0;
    CHECK(screen_text_columns_on_screen(&g) == 1);
}

/* ---- refresh: what lands in the shadow buffer --------------------------- */

static void
test_refresh_draws_the_lines(void)
{
    init("one\ntwo\nthree\n");
    refresh(&g, TRUE);
    CHECK(row_is(0, "one"));
    CHECK(row_is(1, "two"));
    CHECK(row_is(2, "three"));
}

static void
test_refresh_marks_lines_past_the_end(void)
{
    init("one\n");
    refresh(&g, TRUE);
    CHECK(row_is(0, "one"));
    /* Rows with no buffer line behind them show the empty-line marker. */
    CHECK(row(1)[0] == '~');
}

static void
test_refresh_expands_tabs(void)
{
    init("\tx\n");
    refresh(&g, TRUE);
    /* The tab is rendered as spaces to the next tab stop, so the row is
     * eight columns wide before the x. */
    CHECK(strncmp(row(0), "        x", 9) == 0);
}

static void
test_refresh_renders_control_characters(void)
{
    init("a\001b\n");
    refresh(&g, TRUE);
    /* A control byte is shown as a caret pair rather than sent to the
     * terminal. */
    CHECK(strstr(row(0), "^A") != NULL);
}

static void
test_refresh_starts_at_screenbegin(void)
{
    /* o0..o3 "one\n" t4.. "two\n" t8.. "three\n" */
    init("one\ntwo\nthree\n");
    g.screenbegin = 4;
    refresh(&g, TRUE);
    /* The viewport starts at the second line. */
    CHECK(row_is(0, "two"));
    CHECK(row_is(1, "three"));
}

static void
test_refresh_line_numbers(void)
{
    init("one\ntwo\n");
    g.setops |= VI_NUMBER;
    refresh(&g, TRUE);
    /* The gutter carries the absolute line number. */
    CHECK(strstr(row(0), "1") != NULL);
    CHECK(strstr(row(0), "one") != NULL);
    CHECK(strstr(row(1), "2") != NULL);
}

static void
test_refresh_relative_line_numbers(void)
{
    init("one\ntwo\nthree\n");
    g.setops |= VI_RELATIVENUMBER;
    g.dot = 4; /* the second line is the current one */
    refresh(&g, TRUE);
    /* The current line shows 0 in a relative gutter. */
    CHECK(strstr(row(1), "0") != NULL);
}

/* ---- refresh: the cursor ------------------------------------------------ */

static void
test_refresh_syncs_the_cursor(void)
{
    init("one\ntwo\n");
    g.dot    = 5;
    sync_row = 1;
    sync_col = 1;
    refresh(&g, TRUE);
    /* The row and column come from sync_cursor, asked about dot. */
    CHECK(sync_calls == 1);
    CHECK(sync_last_dot == 5);
    CHECK(g.crow == 1 && g.ccol == 1);
    /* The hardware cursor is placed last, at that position. */
    CHECK(last_place_row == 1);
    CHECK(last_place_col == 1);
}

static void
test_refresh_offsets_the_cursor_by_the_gutter(void)
{
    init("one\ntwo\n");
    g.setops |= VI_NUMBER;
    g.dot    = 1;
    sync_row = 0;
    sync_col = 1;
    refresh(&g, TRUE);
    /* The gutter is two columns wide, so the cursor sits two further
     * right than its text column. */
    CHECK(last_place_col == 1 + 2);
}

static void
test_refresh_tracks_the_column(void)
{
    init("one\ntwo\n");
    g.dot        = 2;
    sync_row     = 0;
    sync_col     = 2;
    g.keep_index = FALSE;
    refresh(&g, TRUE);
    /* With no sticky column in force, the remembered column follows the
     * cursor. */
    CHECK(g.cindex == 2);
}

static void
test_refresh_keeps_a_sticky_column(void)
{
    init("one\ntwo\n");
    g.dot        = 2;
    sync_row     = 0;
    sync_col     = 2;
    g.cindex     = 99;
    g.keep_index = TRUE;
    refresh(&g, TRUE);
    /* keep_index is j/k and $ saying "leave my column alone". */
    CHECK(g.cindex == 99);
}

/* ---- refresh: the diff -------------------------------------------------- */

static void
test_refresh_only_redraws_changed_rows(void)
{
    int first;

    init("one\ntwo\nthree\n");
    refresh(&g, TRUE);
    first = clear_calls;
    CHECK(first > 0);

    /* Nothing changed and the fast path has not been armed yet, so the
     * row comparison alone must suppress every repaint. */
    clear_calls = 0;
    refresh(&g, FALSE);
    CHECK(clear_calls == 0);
}

static void
test_refresh_redraws_an_edited_row(void)
{
    init("one\ntwo\nthree\n");
    refresh(&g, TRUE);

    /* Change one line, and mark the buffer modified as an edit would. */
    text_set_bytes(&g, 4, "TWO", 3);
    g.modified_count++;
    clear_calls = 0;
    refresh(&g, FALSE);
    CHECK(clear_calls == 1);
    CHECK(row_is(1, "TWO"));
    /* The untouched rows still hold their old content. */
    CHECK(row_is(0, "one"));
}

static void
test_refresh_full_screen_redraws_everything(void)
{
    init("one\ntwo\nthree\n");
    refresh(&g, TRUE);
    clear_calls = 0;
    refresh(&g, TRUE);
    /* TRUE forces every text row out again, which is what Ctrl-L needs.
     * The bottom row belongs to the status line and is not drawn here. */
    CHECK(clear_calls == text_rows());
}

static void
test_refresh_fast_path(void)
{
    init("one\ntwo\n");
    g.dot = 0;
    refresh(&g, TRUE);
    /* The second refresh with nothing changed takes the early return:
     * the cursor is placed and no row is touched. */
    clear_calls = 0;
    place_calls = 0;
    refresh(&g, FALSE);
    CHECK(clear_calls == 0);
    CHECK(place_calls == 1);
}

static void
test_refresh_records_its_cache_state(void)
{
    init("one\ntwo\n");
    g.screenbegin    = 4;
    g.modified_count = 7;
    refresh(&g, TRUE);
    /* The stamps are what the fast path compares against next time. */
    CHECK(g.refresh_last_screenbegin == 4);
    CHECK(g.refresh_last_modified_count == 7);
}

static void
test_refresh_redraws_when_scrolled(void)
{
    init("one\ntwo\nthree\nfour\n");
    refresh(&g, TRUE);
    clear_calls = 0;
    /* A scroll changes screenbegin, so the fast path must not fire. */
    g.screenbegin = 4;
    refresh(&g, FALSE);
    CHECK(clear_calls > 0);
    CHECK(row_is(0, "two"));
}

static void
test_refresh_redraws_when_the_highlight_changes(void)
{
    init("one\ntwo\n");
    refresh(&g, TRUE);
    clear_calls = 0;
    /* A new highlight pattern changes what every row looks like. */
    g.highlight_pattern = xstrdup("two");
    refresh(&g, FALSE);
    CHECK(clear_calls > 0);
}

static void
test_refresh_in_visual_mode_always_redraws(void)
{
    init("one\ntwo\n");
    g.dot = 0;
    refresh(&g, TRUE);
    /* A visual selection moves with the cursor, so the fast path is
     * disabled while it is active. */
    visual_enter(&g, 1);
    g.dot       = 2;
    clear_calls = 0;
    place_calls = 0;
    refresh(&g, FALSE);
    CHECK(place_calls > 1);
}

/* ---- refresh: horizontal scrolling -------------------------------------- */

static void
test_refresh_horizontal_offset(void)
{
    init("abcdefghij\n");
    g.columns = 40;
    g.offset  = 3;
    refresh(&g, TRUE);
    /* With a horizontal offset the row starts three characters in. */
    CHECK(strncmp(row(0), "defghij", 7) == 0);
}

static void
test_refresh_redraws_when_the_offset_changes(void)
{
    init("abcdefghij\n");
    refresh(&g, TRUE);
    clear_calls = 0;
    g.offset    = 3;
    refresh(&g, FALSE);
    CHECK(clear_calls > 0);
    CHECK(g.scr_old_offset == 3);
}

/* ---- redraw ------------------------------------------------------------- */

static void
test_redraw(void)
{
    init("one\ntwo\n");
    refresh(&g, TRUE);
    g.last_status_cksum = 12345;
    clear_calls         = 0;
    redraw(&g, TRUE);
    /* redraw clears the terminal and blanks the shadow buffer, so every
     * text row is painted again. */
    CHECK(clear_calls == text_rows());
    CHECK(row_is(0, "one"));
    /*
     * The status checksum is cleared before the refresh so the status
     * line is rewritten; show_status_line then stamps the new one, so
     * what matters is that the old value did not survive.
     */
    CHECK(g.last_status_cksum != 12345);
}

static void
test_redraw_without_full_screen(void)
{
    init("one\ntwo\n");
    refresh(&g, TRUE);
    clear_calls = 0;
    /*
     * redraw(g, FALSE) blanks the shadow buffer but the refresh it calls
     * can still take its early-return path, because that path compares
     * the cache stamps and never looks at the shadow buffer.  Nothing is
     * then painted, even though the terminal has just been cleared.
     *
     * Pinned as current behaviour.  It is not reachable today: the one
     * caller that passes FALSE (the edit loop in vic.c) runs with the
     * stamps invalidated by init_text_buffer, so the full repaint does
     * happen there.  Every other caller passes TRUE.
     */
    redraw(&g, FALSE);
    CHECK(clear_calls == 0);
    CHECK(row(0)[0] == '\0');
}

/* ---- degenerate buffers ------------------------------------------------- */

static void
test_refresh_empty_buffer(void)
{
    init("");
    refresh(&g, TRUE);
    /* Nothing to show but the empty-line markers. */
    CHECK(ptcheck(g.pt) == 1);
    CHECK(row(1)[0] == '~');
}

static void
test_refresh_single_newline(void)
{
    init("\n");
    refresh(&g, TRUE);
    /* One empty line: the first row is blank and the rest are markers. */
    CHECK(row_is(0, ""));
    CHECK(row(1)[0] == '~');
    CHECK(ptcheck(g.pt) == 1);
}

static void
test_refresh_long_line(void)
{
    char buf[600];
    int i;

    /* A line far wider than the terminal must be clipped to the row
     * buffer rather than overrunning it. */
    for (i = 0; i < (int)sizeof(buf) - 2; i++)
        buf[i] = 'x';
    buf[sizeof(buf) - 2] = '\n';
    buf[sizeof(buf) - 1] = '\0';
    init(buf);
    g.columns = 40;
    refresh(&g, TRUE);
    CHECK((int)strlen(row(0)) < g.screen_line_size);
}

static void
test_refresh_multibyte(void)
{
    init("caf\xc3\xa9\n");
    refresh(&g, TRUE);
    /* The accented character is passed through as its two bytes, not
     * expanded or mangled. */
    CHECK(strstr(row(0), "caf\xc3\xa9") != NULL);
}

static void
test_refresh_narrow_terminal(void)
{
    init("abcdefghij\n");
    g.columns = 4;
    new_screen(&g, (int)g.rows, (int)g.columns);
    refresh(&g, TRUE);
    CHECK((int)strlen(row(0)) < g.screen_line_size);
    CHECK(ptcheck(g.pt) == 1);
}

int
main(void)
{
    /* screen.c writes the rows and escapes to stdout. */
    if (!freopen("/dev/null", "w", stdout))
        abort();

    test_new_screen_sizes();
    test_new_screen_starts_blank();
    test_new_screen_reallocates();
    test_screen_erase();

    test_line_number_width_off();
    test_line_number_width_digits();
    test_line_number_width_grows();
    test_line_number_width_relativenumber();

    test_text_columns();
    test_text_columns_never_zero();

    test_refresh_draws_the_lines();
    test_refresh_marks_lines_past_the_end();
    test_refresh_expands_tabs();
    test_refresh_renders_control_characters();
    test_refresh_starts_at_screenbegin();
    test_refresh_line_numbers();
    test_refresh_relative_line_numbers();

    test_refresh_syncs_the_cursor();
    test_refresh_offsets_the_cursor_by_the_gutter();
    test_refresh_tracks_the_column();
    test_refresh_keeps_a_sticky_column();

    test_refresh_only_redraws_changed_rows();
    test_refresh_redraws_an_edited_row();
    test_refresh_full_screen_redraws_everything();
    test_refresh_fast_path();
    test_refresh_records_its_cache_state();
    test_refresh_redraws_when_scrolled();
    test_refresh_redraws_when_the_highlight_changes();
    test_refresh_in_visual_mode_always_redraws();

    test_refresh_horizontal_offset();
    test_refresh_redraws_when_the_offset_changes();

    test_redraw();
    test_redraw_without_full_screen();

    test_refresh_empty_buffer();
    test_refresh_single_newline();
    test_refresh_long_line();
    test_refresh_multibyte();
    test_refresh_narrow_terminal();

    SUMMARY();
}
