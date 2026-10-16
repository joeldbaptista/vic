/*
 * test_status.c - unit tests for status.c
 *
 * status.c builds the bottom status line.  Everything it produces lands
 * in g->status_buffer before it is written out, so the tests inspect that
 * buffer rather than the terminal.
 *
 * line.c and codepoint.c are linked for real, because the edit status
 * counts lines and columns.  Seven symbols are stubbed, all of them
 * terminal output: go_bottom_and_clear_to_eol (counted, since it marks an
 * actual redraw), place_cursor, redraw, screen_line_number_width,
 * standout_start, standout_end, and get_one_char (which returns '\n' so
 * that hit_return cannot block).
 *
 * status.c also writes to stdout directly, so main() redirects stdout to
 * /dev/null.  testutil.h reports on stderr, which is left alone.
 */
#include "testutil.h"
#include "status.h"
#include "pt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

/* status.c's own escape wrappers, repeated here. */
#define BOLD "\033[7m"
#define NORM "\033[m"

static struct editor g;
static int clears;   /* go_bottom_and_clear_to_eol calls */
static int redraws;
static int cursor_placements;

void
go_bottom_and_clear_to_eol(struct editor *x)
{
    (void)x;
    clears++;
}

void
place_cursor(struct editor *x, int row, int col)
{
    (void)x;
    (void)row;
    (void)col;
    cursor_placements++;
}

void
redraw(struct editor *x, int full_screen)
{
    (void)x;
    (void)full_screen;
    redraws++;
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

int
get_one_char(struct editor *x)
{
    (void)x;
    return '\n'; /* so hit_return returns at once */
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
    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt                    = ptnew(copy, n);
    g.tabstop               = 8;
    g.columns               = 80;
    g.rows                  = 24;
    g.last_modified_count   = -1;
    g.line_count_cache_stamp = INT_MIN;
    clears                  = 0;
    redraws                 = 0;
    cursor_placements       = 0;
}

/* ---- status_line --------------------------------------------------------- */

static void
test_status_line(void)
{
    init("abc\n");
    status_line(&g, "hello %s %d", "world", 42);
    CHECK(strcmp(g.status_buffer, "hello world 42") == 0);
    /* 1 marks "a message is queued with no escape padding". */
    CHECK(g.have_status_msg == 1);
}

static void
test_status_line_truncates(void)
{
    char big[400];

    init("abc\n");
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    status_line(&g, "%s", big);
    /* The buffer is a fixed size, so a long message is cut to fit and
     * stays NUL-terminated. */
    CHECK(strlen(g.status_buffer) == STATUS_BUFFER_LEN - 1);
}

static void
test_status_line_bold(void)
{
    init("abc\n");
    status_line_bold(&g, "oops %d", 7);
    /* The text is wrapped in the reverse-video escapes. */
    CHECK(strcmp(g.status_buffer, BOLD "oops 7" NORM) == 0);
    /*
     * have_status_msg carries the escape byte count so that
     * show_status_line can work out the message's printed width: 1 plus
     * the two escape sequences.
     */
    CHECK(g.have_status_msg == 1 + (int)strlen(BOLD) + (int)strlen(NORM));
}

static void
test_status_line_bold_errno(void)
{
    init("abc\n");
    errno = ENOENT;
    status_line_bold_errno(&g, "missing.txt");
    CHECK(strstr(g.status_buffer, "missing.txt") != NULL);
    /* The strerror text is appended so the user sees why. */
    CHECK(strstr(g.status_buffer, strerror(ENOENT)) != NULL);
    CHECK(strstr(g.status_buffer, BOLD) != NULL);
}

static void
test_not_implemented(void)
{
    init("abc\n");
    not_implemented(&g, "ZZ");
    CHECK(strstr(g.status_buffer, "'ZZ' is not implemented") != NULL);
}

static void
test_not_implemented_sanitises(void)
{
    init("abc\n");
    /* The command name comes from the keyboard, so a control character
     * must be rendered rather than sent to the terminal. */
    not_implemented(&g, "\033[31m");
    CHECK(strstr(g.status_buffer, "^[[31m") != NULL);
    CHECK(strstr(g.status_buffer, "\033[31m") == NULL);
}

/* ---- print_literal ------------------------------------------------------- */

static void
test_print_literal_plain(void)
{
    char buf[PRINT_LITERAL_LEN + 16];

    print_literal(buf, "hello");
    CHECK(strcmp(buf, "hello") == 0);
}

static void
test_print_literal_empty(void)
{
    char buf[PRINT_LITERAL_LEN + 16];

    /* An empty string is shown as "(NULL)" rather than as nothing. */
    print_literal(buf, "");
    CHECK(strcmp(buf, "(NULL)") == 0);
}

static void
test_print_literal_control_chars(void)
{
    char buf[PRINT_LITERAL_LEN + 16];

    /* A control byte becomes a caret plus the letter it maps to, which is
     * what stops an escape sequence from reaching the terminal. */
    print_literal(buf, "\001");
    CHECK(strcmp(buf, "^A") == 0);

    print_literal(buf, "\033");
    CHECK(strcmp(buf, "^[") == 0);

    print_literal(buf, "a\tb");
    CHECK(strcmp(buf, "a^Ib") == 0);

    print_literal(buf, "a\nb");
    CHECK(strcmp(buf, "a^Jb") == 0);

    /* DEL is shown as ^? */
    print_literal(buf, "\177");
    CHECK(strcmp(buf, "^?") == 0);
}

static void
test_print_literal_high_bytes(void)
{
    char buf[PRINT_LITERAL_LEN + 16];

    /* Any byte with the high bit set is not printable in this encoding,
     * so it becomes a question mark.  UTF-8 text is therefore mangled,
     * which is deliberate: the status line is a byte display, not a
     * codepoint display. */
    print_literal(buf, "\xc3\xa9");
    CHECK(strcmp(buf, "??") == 0);

    print_literal(buf, "a\xff" "b");
    CHECK(strcmp(buf, "a?b") == 0);
}

static void
test_print_literal_truncates(void)
{
    char buf[PRINT_LITERAL_LEN + 16];
    char big[300];

    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    print_literal(buf, big);
    /* The output stops short of the buffer end, with room for the
     * two-byte expansion of a control character. */
    CHECK(strlen(buf) <= PRINT_LITERAL_LEN - 9);
    CHECK(strlen(buf) > 0);
}

static void
test_print_literal_truncates_expanded(void)
{
    char buf[PRINT_LITERAL_LEN + 16];
    char big[300];
    size_t i;

    /* Every byte expands to two, so the limit is reached twice as fast
     * and must still not overrun. */
    for (i = 0; i < sizeof(big) - 1; i++)
        big[i] = '\001';
    big[sizeof(big) - 1] = '\0';
    print_literal(buf, big);
    CHECK(strlen(buf) <= PRINT_LITERAL_LEN);
}

/* ---- show_status_line: the edit status ----------------------------------- */

static void
test_edit_status_format(void)
{
    init("one\ntwo\nthree\n");
    g.dot             = 4;
    g.current_filename = (char *)"file.c";
    show_status_line(&g);
    /* "%M %F%R%m %c/%t,%C %p%%": mode, name, line/total, column, percent. */
    CHECK(strstr(g.status_buffer, "-") != NULL);
    CHECK(strstr(g.status_buffer, "file.c") != NULL);
    CHECK(strstr(g.status_buffer, "2/3") != NULL);
    CHECK(strstr(g.status_buffer, ",1") != NULL);
    CHECK(strstr(g.status_buffer, "66%") != NULL);
    /* No [Modified] and no [Readonly] on a clean, writable buffer. */
    CHECK(strstr(g.status_buffer, "[Modified]") == NULL);
    CHECK(strstr(g.status_buffer, "[Readonly]") == NULL);
}

static void
test_edit_status_no_file(void)
{
    init("abc\n");
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "No file") != NULL);
}

static void
test_edit_status_modes(void)
{
    init("abc\n");
    g.cmd_mode = 1;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "INSERT -") != NULL);

    init("abc\n");
    g.cmd_mode = 2;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "REPLACE -") != NULL);

    /* Visual mode wins over the insert/replace mode. */
    init("abc\n");
    g.visual_mode = 1;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "VISUAL -") != NULL);

    init("abc\n");
    g.visual_mode = 2;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "VISUAL LINE -") != NULL);

    init("abc\n");
    g.visual_mode = 3;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "VISUAL BLOCK -") != NULL);
}

static void
test_edit_status_flags(void)
{
    init("abc\n");
    g.modified_count = 3;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "[Modified]") != NULL);

    init("abc\n");
    g.readonly_mode = 1;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "[Readonly]") != NULL);
}

static void
test_edit_status_column(void)
{
    init("abcdef\n");
    g.dot = 3;
    show_status_line(&g);
    /* The column is 1-based. */
    CHECK(strstr(g.status_buffer, ",4") != NULL);
}

static void
test_edit_status_tab_column(void)
{
    /* \t0 a1 — with tabstop 8, 'a' is at column 8, shown as 9. */
    init("\tab\n");
    g.dot = 1;
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, ",9") != NULL);
}

static void
test_edit_status_empty_buffer(void)
{
    init("");
    /* No lines at all: the percentage is reported as 100 rather than
     * dividing by zero. */
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "100%") != NULL);
    CHECK(strstr(g.status_buffer, "0/0") != NULL);
}

static void
test_edit_status_truncated_to_the_terminal(void)
{
    init("abc\n");
    g.columns          = 20;
    g.current_filename = (char *)"a-rather-long-file-name-that-will-not-fit.c";
    show_status_line(&g);
    /* The status line never exceeds the terminal width. */
    CHECK(strlen(g.status_buffer) <= 20);
}

static void
test_edit_status_very_narrow_terminal(void)
{
    init("abc\n");
    g.columns          = 3;
    g.current_filename = (char *)"file.c";
    show_status_line(&g);
    CHECK(strlen(g.status_buffer) <= 3);
}

/* ---- show_status_line: redraw suppression -------------------------------- */

static void
test_redraw_is_skipped_when_nothing_changed(void)
{
    init("one\ntwo\n");
    g.current_filename = (char *)"f.c";
    show_status_line(&g);
    CHECK(clears == 1);

    /* The same status produces the same checksum, so the line is not
     * written again. */
    show_status_line(&g);
    CHECK(clears == 1);
}

static void
test_redraw_happens_when_the_position_changes(void)
{
    init("one\ntwo\n");
    g.current_filename = (char *)"f.c";
    show_status_line(&g);
    CHECK(clears == 1);

    g.dot = 4;
    show_status_line(&g);
    CHECK(clears == 2);
}

static void
test_checksum_is_position_weighted(void)
{
    char buf[700];
    int i;

    /*
     * A plain byte sum aliases.  In a 200-line buffer, line 100 column 2
     * renders as "- No file 100/200,2 50%" and line 101 column 1 as
     * "- No file 101/200,1 50%": the line number gains exactly what the
     * column loses, and the percentage is the same, so the two strings
     * have identical byte sums.  With a plain sum the second status is
     * taken for the first and the stale numbers stay on screen.  The sum
     * is position-weighted precisely to separate this pair.
     */
    buf[0] = '\0';
    for (i = 0; i < 200; i++)
        strcat(buf, "xx\n");

    init(buf);
    g.dot = 99 * 3 + 1; /* line 100, column 1 (shown as 2) */
    show_status_line(&g);
    CHECK(clears == 1);
    CHECK(strstr(g.status_buffer, "100/200,2") != NULL);

    g.dot = 100 * 3; /* line 101, column 0 (shown as 1) */
    show_status_line(&g);
    CHECK(strstr(g.status_buffer, "101/200,1") != NULL);
    /* The redraw must happen: the two statuses differ. */
    CHECK(clears == 2);
}

static void
test_queued_message_always_redraws(void)
{
    init("abc\n");
    status_line(&g, "a message");
    show_status_line(&g);
    CHECK(clears == 1);
    /* The flag is consumed, so the next call falls back to the edit
     * status. */
    CHECK(g.have_status_msg == 0);

    status_line(&g, "a message");
    show_status_line(&g);
    CHECK(clears == 2);
}

static void
test_long_message_prompts_hit_return(void)
{
    char big[120];

    init("abc\n");
    g.columns = 20;
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    status_line(&g, "%s", big);
    show_status_line(&g);
    /* A message wider than the terminal needs acknowledging, and
     * hit_return redraws the screen afterwards. */
    CHECK(redraws == 1);
}

static void
test_short_message_does_not_prompt(void)
{
    init("abc\n");
    g.columns = 80;
    status_line(&g, "short");
    show_status_line(&g);
    CHECK(redraws == 0);
}

static void
test_hit_return(void)
{
    init("abc\n");
    /* get_one_char returns '\n', so this returns immediately and
     * redraws. */
    hit_return(&g);
    CHECK(redraws == 1);
}

int
main(void)
{
    /* status.c writes the line to stdout; testutil reports on stderr. */
    if (!freopen("/dev/null", "w", stdout))
        abort();

    test_status_line();
    test_status_line_truncates();
    test_status_line_bold();
    test_status_line_bold_errno();
    test_not_implemented();
    test_not_implemented_sanitises();

    test_print_literal_plain();
    test_print_literal_empty();
    test_print_literal_control_chars();
    test_print_literal_high_bytes();
    test_print_literal_truncates();
    test_print_literal_truncates_expanded();

    test_edit_status_format();
    test_edit_status_no_file();
    test_edit_status_modes();
    test_edit_status_flags();
    test_edit_status_column();
    test_edit_status_tab_column();
    test_edit_status_empty_buffer();
    test_edit_status_truncated_to_the_terminal();
    test_edit_status_very_narrow_terminal();

    test_redraw_is_skipped_when_nothing_changed();
    test_redraw_happens_when_the_position_changes();
    test_checksum_is_position_weighted();
    test_queued_message_always_redraws();
    test_long_message_prompts_hit_return();
    test_short_message_does_not_prompt();
    test_hit_return();

    SUMMARY();
}
