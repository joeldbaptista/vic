/*
 * test_ex.c - unit tests for ex.c
 *
 * ex.c is the colon-command layer: address and range parsing, then one
 * handler per command.  colon() is the only entry point the editor uses,
 * so the tests drive that and check the buffer, the registers, the marks
 * and the status message.
 *
 * Nearly the whole editor is linked for real — buffer.c, undo.c,
 * operator.c, motion.c, search.c with the vendored regex engine,
 * excore.c, run.c, screen.c, status.c, session.c and the colorizer
 * registry — because an ex command is only correct if it leaves the right
 * text behind.  Twenty-six symbols are stubbed, every one of them either
 * terminal output, keyboard input, or the normal-mode dispatcher in
 * vic.c, which cannot be linked.
 *
 * Not covered here, and why:
 *   :!cmd, :r!cmd, range filters, :sh — they fork and exec a shell.
 *   :e, :n, :p, :rew                   — they call edit_file, which
 *                                        re-enters the file loop.
 *   The interactive :s confirmation     — it reads keys from the terminal.
 * Those paths belong to the PTY harness in tools/check-pty.
 *
 * optind is global state that :q and :n read, so the fixture resets it.
 */
#include "testutil.h"
#include "ex.h"
#include "buffer.h"
#include "pt.h"
#include "screen.h"
#include "text.h"
#include "undo.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;

/* ---- stubs: terminal, keyboard, and the normal-mode dispatcher --------- */

static char status_last[512];
static int status_calls;

void
clear_to_eol(void)
{
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
sync_cursor(struct editor *x, int d, int *rowp, int *colp)
{
    (void)x;
    (void)d;
    *rowp = 0;
    *colp = 0;
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
cookmode(struct editor *x)
{
    (void)x;
}

void
rawmode(struct editor *x)
{
    (void)x;
}

void
do_cmd(struct editor *x, int c, const struct cmd_ctx *ctx)
{
    (void)x;
    (void)c;
    (void)ctx;
}

static int edit_file_calls;

void
edit_file(struct editor *x, char *fn)
{
    (void)x;
    (void)fn;
    edit_file_calls++;
}

int
find_pair(struct editor *x, int p, const char c)
{
    (void)x;
    (void)p;
    (void)c;
    return -1;
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
}

void
reset_ydreg(struct editor *x)
{
    (void)x;
}

void
show_help(void)
{
}

void
showmatching(struct editor *x, int p)
{
    (void)x;
    (void)p;
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
    g.session_epoch               = 424244;
    g.last_search_pattern         = xzalloc(2);
    g.term_orig.c_cc[VERASE]      = 127;
    g.editing                     = 1;
    for (i = 0; i < (int)(sizeof(g.mark) / sizeof(g.mark[0])); i++)
        g.mark[i] = -1;
    new_screen(&g, (int)g.rows, (int)g.columns);
    status_calls    = 0;
    status_last[0]  = '\0';
    edit_file_calls = 0;
    optind          = 0;
    /* The yank mirror file would otherwise carry a register between
     * tests. */
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

/* Run one ex command on a writable copy, as the editor does. */
static void
ex(const char *cmd)
{
    char buf[256];

    snprintf(buf, sizeof(buf), "%s", cmd);
    colon(&g, buf);
}

/* status.c is linked for real, so the message is read out of the
 * editor's own buffer; status_line_bold leaves escape bytes around it. */
static int
said(const char *needle)
{
    return strstr(g.status_buffer, needle) != NULL;
}

/* ---- the trivial cases --------------------------------------------------- */

static void
test_empty_command(void)
{
    init("one\ntwo\n");
    g.dot = 4;
    ex("");
    /* Nothing typed: nothing happens, and dot stays. */
    CHECK(strcmp(dump(), "one\ntwo\n") == 0);
    CHECK(g.dot == 4);
}

static void
test_leading_colons_and_space(void)
{
    init("one\ntwo\nthree\n");
    /* The prompt's own colon, extra colons and leading blanks are all
     * stripped before parsing. */
    ex(":::  2");
    CHECK(g.dot == 4);
}

static void
test_comment(void)
{
    init("one\ntwo\n");
    g.dot = 0;
    ex("\" a comment");
    CHECK(strcmp(dump(), "one\ntwo\n") == 0);
    CHECK(g.dot == 0);
}

static void
test_unknown_command(void)
{
    init("one\n");
    ex("nosuchcommand");
    CHECK(said("not implemented"));
    CHECK(strcmp(dump(), "one\n") == 0);
}

/* ---- addresses ----------------------------------------------------------- */

static void
test_bare_line_address(void)
{
    /* o0..o3 "one\n" t4..t7 "two\n" t8.. "three\n" */
    init("one\ntwo\nthree\n");
    ex("1");
    CHECK(g.dot == 0);
    ex("2");
    CHECK(g.dot == 4);
    ex("3");
    CHECK(g.dot == 8);
}

static void
test_dollar_address(void)
{
    init("one\ntwo\nthree\n");
    ex("$");
    /* $ is the last line. */
    CHECK(g.dot == 8);
}

static void
test_dot_address(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    ex(".d");
    /* '.' is the current line, so this deletes line 2. */
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
}

static void
test_relative_address(void)
{
    init("one\ntwo\nthree\nfour\n");
    g.dot = 4; /* line 2 */
    ex(".+1d");
    CHECK(strcmp(dump(), "one\ntwo\nfour\n") == 0);

    init("one\ntwo\nthree\nfour\n");
    g.dot = 8; /* line 3 */
    ex(".-1d");
    CHECK(strcmp(dump(), "one\nthree\nfour\n") == 0);
}

static void
test_range_address(void)
{
    init("one\ntwo\nthree\nfour\n");
    ex("2,3d");
    CHECK(strcmp(dump(), "one\nfour\n") == 0);
}

static void
test_whole_file_address(void)
{
    init("one\ntwo\nthree\n");
    ex("%d");
    /* % is 1,$ — the whole buffer. */
    CHECK(text_len(&g) <= 1);
}

static void
test_search_address(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 0;
    /* A // address selects the line the pattern is found on. */
    ex("/three/d");
    CHECK(strcmp(dump(), "one\ntwo\n") == 0);
}

static void
test_mark_address(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    ex("ma a");
    g.dot = 0;
    ex("'ad");
    /* The mark remembers line 2. */
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
}

static void
test_invalid_address(void)
{
    init("one\ntwo\n");
    ex("99d");
    CHECK(said("Invalid range"));
    CHECK(strcmp(dump(), "one\ntwo\n") == 0);
}

static void
test_reversed_range(void)
{
    init("one\ntwo\nthree\n");
    ex("3,1d");
    /* A range whose start is after its end is refused rather than
     * silently swapped. */
    CHECK(said("Invalid range"));
    CHECK(strcmp(dump(), "one\ntwo\nthree\n") == 0);
}

static void
test_line_number_report(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    ex("=");
    /* := reports the current line number. */
    CHECK(said("2"));
}

/* ---- :delete and :yank -------------------------------------------------- */

static void
test_delete(void)
{
    init("one\ntwo\nthree\n");
    ex("2d");
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
    /* The deleted line goes to the register. */
    CHECK(g.reg[g.ydreg] != NULL);
    CHECK(strcmp(g.reg[g.ydreg], "two\n") == 0);
}

static void
test_delete_is_undoable(void)
{
    init("one\ntwo\nthree\n");
    ex("2d");
    CHECK(strcmp(dump(), "one\nthree\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "one\ntwo\nthree\n") == 0);
}

static void
test_delete_range(void)
{
    init("a\nb\nc\nd\n");
    ex("2,3d");
    CHECK(strcmp(dump(), "a\nd\n") == 0);
}

static void
test_delete_abbreviations(void)
{
    /* One letter is enough for :delete. */
    init("one\ntwo\n");
    ex("1d");
    CHECK(strcmp(dump(), "two\n") == 0);

    init("one\ntwo\n");
    ex("1del");
    CHECK(strcmp(dump(), "two\n") == 0);

    init("one\ntwo\n");
    ex("1delete");
    CHECK(strcmp(dump(), "two\n") == 0);
}

static void
test_yank(void)
{
    init("one\ntwo\nthree\n");
    ex("2y");
    /* A yank leaves the text alone. */
    CHECK(strcmp(dump(), "one\ntwo\nthree\n") == 0);
    CHECK(strcmp(g.reg[g.ydreg], "two\n") == 0);
}

static void
test_yank_range(void)
{
    init("a\nb\nc\n");
    ex("1,2y");
    CHECK(strcmp(g.reg[g.ydreg], "a\nb\n") == 0);
}

/* ---- :substitute -------------------------------------------------------- */

static void
test_substitute(void)
{
    init("foo bar\n");
    ex("s/foo/XX/");
    CHECK(strcmp(dump(), "XX bar\n") == 0);
}

static void
test_substitute_first_only(void)
{
    init("aa aa aa\n");
    ex("s/aa/X/");
    /* Without g, only the first match on the line changes. */
    CHECK(strcmp(dump(), "X aa aa\n") == 0);
}

static void
test_substitute_global_flag(void)
{
    init("aa aa aa\n");
    ex("s/aa/X/g");
    CHECK(strcmp(dump(), "X X X\n") == 0);
}

static void
test_substitute_over_a_range(void)
{
    init("aa\naa\naa\n");
    ex("1,2s/aa/X/");
    CHECK(strcmp(dump(), "X\nX\naa\n") == 0);
}

static void
test_substitute_whole_file(void)
{
    init("aa\naa\naa\n");
    ex("%s/aa/X/");
    CHECK(strcmp(dump(), "X\nX\nX\n") == 0);
}

static void
test_substitute_regex(void)
{
    init("abc123def\n");
    ex("s/[0-9][0-9]*/N/");
    CHECK(strcmp(dump(), "abcNdef\n") == 0);
}

static void
test_substitute_anchors(void)
{
    init("aba\n");
    ex("s/^a/X/");
    CHECK(strcmp(dump(), "Xba\n") == 0);

    init("aba\n");
    ex("s/a$/X/");
    CHECK(strcmp(dump(), "abX\n") == 0);
}

static void
test_substitute_delete(void)
{
    init("foo bar\n");
    /* An empty replacement deletes the match. */
    ex("s/foo //");
    CHECK(strcmp(dump(), "bar\n") == 0);
}

static void
test_substitute_no_match(void)
{
    init("foo\n");
    ex("s/zzz/X/");
    CHECK(strcmp(dump(), "foo\n") == 0);
}

static void
test_substitute_is_undoable(void)
{
    init("aa aa\n");
    ex("s/aa/X/g");
    CHECK(strcmp(dump(), "X X\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "aa aa\n") == 0);
}

static void
test_substitute_alternative_delimiter(void)
{
    init("/usr/bin\n");
    /* Any character can delimit, which is what makes paths editable. */
    ex("s,/usr,/opt,");
    CHECK(strcmp(dump(), "/opt/bin\n") == 0);
}

/* ---- :global and :vglobal ----------------------------------------------- */

static void
test_global_delete(void)
{
    init("keep\ndrop x\nkeep\ndrop y\n");
    ex("g/drop/d");
    CHECK(strcmp(dump(), "keep\nkeep\n") == 0);
}

static void
test_global_substitute(void)
{
    init("a1\nb2\na3\n");
    ex("g/a/s/[0-9]/N/");
    CHECK(strcmp(dump(), "aN\nb2\naN\n") == 0);
}

static void
test_vglobal_delete(void)
{
    init("keep\ndrop x\nkeep\n");
    /* :v acts on the lines that do NOT match. */
    ex("v/keep/d");
    CHECK(strcmp(dump(), "keep\nkeep\n") == 0);
}

static void
test_global_no_match(void)
{
    init("a\nb\n");
    ex("g/zzz/d");
    CHECK(strcmp(dump(), "a\nb\n") == 0);
}

/* ---- :set --------------------------------------------------------------- */

static void
test_set_boolean(void)
{
    init("x\n");
    ex("se ai");
    CHECK((g.setops & VI_AUTOINDENT) != 0);

    ex("se noai");
    CHECK((g.setops & VI_AUTOINDENT) == 0);
}

static void
test_set_tabstop(void)
{
    init("x\n");
    ex("se ts=4");
    CHECK(g.tabstop == 4);
}

static void
test_set_several_options(void)
{
    init("x\n");
    ex("se ai et nu");
    /* One :set may carry several options. */
    CHECK((g.setops & VI_AUTOINDENT) != 0);
    CHECK((g.setops & VI_EXPANDTAB) != 0);
    CHECK((g.setops & VI_NUMBER) != 0);
}

static void
test_set_bad_option(void)
{
    init("x\n");
    ex("se nosuchopt");
    CHECK(said("bad option"));
}

static void
test_set_needs_two_letters(void)
{
    init("foo\n");
    /* A single 's' is :substitute, not :set, which is why :set needs
     * 'se'.  With no pattern the substitute reports rather than acting. */
    ex("s");
    CHECK(strcmp(dump(), "foo\n") == 0);
}

/* ---- :mark -------------------------------------------------------------- */

static void
test_mark(void)
{
    init("one\ntwo\nthree\n");
    g.dot = 4;
    ex("ma b");
    /* Marks are stored as offsets, 'a' being index 0. */
    CHECK(g.mark['b' - 'a'] == 4);
}

static void
test_mark_with_an_address(void)
{
    init("one\ntwo\nthree\n");
    ex("3ma c");
    CHECK(g.mark['c' - 'a'] == 8);
}

/* ---- :file -------------------------------------------------------------- */

static void
test_file_redisplays_the_status_line(void)
{
    init("one\ntwo\n");
    g.current_filename  = xstrdup("a.c");
    g.last_status_cksum = 12345;
    ex("fi");
    /*
     * :file with no argument does not write a message of its own: it
     * clears the status checksum so that the next show_status_line
     * repaints the line, which is where the filename appears.
     */
    CHECK(g.last_status_cksum == 0);
    CHECK(strcmp(g.current_filename, "a.c") == 0);
}

static void
test_file_changes_the_name(void)
{
    init("one\ntwo\n");
    g.current_filename = xstrdup("a.c");
    ex("fi b.c");
    /* The buffer is not reloaded; only the name changes, and the old one
     * becomes the alternate. */
    CHECK(strcmp(g.current_filename, "b.c") == 0);
    CHECK(g.alt_filename != NULL && strcmp(g.alt_filename, "a.c") == 0);
    CHECK(strcmp(dump(), "one\ntwo\n") == 0);
}

static void
test_file_rejects_an_address(void)
{
    init("one\ntwo\n");
    ex("1 fi");
    CHECK(said("No address allowed"));
}

/* ---- :list -------------------------------------------------------------- */

static void
test_list(void)
{
    init("one\ntwo\n");
    ex("1l");
    /* :l shows the line with its end marked, which is the point of it. */
    CHECK(said("one"));
}

/* ---- :version and :features -------------------------------------------- */

static void
test_version(void)
{
    init("x\n");
    ex("ve");
    CHECK(status_calls >= 0);
    CHECK(g.status_buffer[0] != '\0');
}

static void
test_features(void)
{
    init("x\n");
    ex("f");
    CHECK(g.status_buffer[0] != '\0');
}

/* ---- :write ------------------------------------------------------------- */

static void
mktemp_path(char *path, size_t n)
{
    int fd;

    snprintf(path, n, "/tmp/vic_test_ex_XXXXXX");
    fd = mkstemp(path);
    if (fd < 0)
        abort();
    close(fd);
}

static char *
slurp(const char *path)
{
    static char buf[512];
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0)
        return NULL;
    n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        return NULL;
    buf[n] = '\0';
    return buf;
}

static void
test_write_to_the_current_file(void)
{
    char path[64];

    mktemp_path(path, sizeof(path));
    init("one\ntwo\n");
    g.current_filename = xstrdup(path);
    g.modified_count   = 1;
    ex("w");
    CHECK(strcmp(slurp(path), "one\ntwo\n") == 0);
    /* A full write clears the modified flag. */
    CHECK(g.modified_count == 0);
    unlink(path);
}

static void
test_write_to_a_named_file(void)
{
    char path[64];

    mktemp_path(path, sizeof(path));
    unlink(path); /* :w refuses to clobber an existing different file */
    init("one\ntwo\n");
    ex("w");
    CHECK(said("No current filename"));

    {
        char cmd[128];

        snprintf(cmd, sizeof(cmd), "w %s", path);
        ex(cmd);
    }
    CHECK(strcmp(slurp(path), "one\ntwo\n") == 0);
    unlink(path);
}

static void
test_write_refuses_to_clobber(void)
{
    char path[64];
    char cmd[128];

    mktemp_path(path, sizeof(path));
    init("one\n");
    g.current_filename = xstrdup("other.c");
    snprintf(cmd, sizeof(cmd), "w %s", path);
    ex(cmd);
    /* The file exists and is not the one being edited. */
    CHECK(said("File exists"));

    /* ! overrides. */
    snprintf(cmd, sizeof(cmd), "w! %s", path);
    ex(cmd);
    CHECK(strcmp(slurp(path), "one\n") == 0);
    unlink(path);
}

static void
test_write_a_range(void)
{
    char path[64];
    char cmd[128];

    mktemp_path(path, sizeof(path));
    unlink(path);
    init("one\ntwo\nthree\n");
    snprintf(cmd, sizeof(cmd), "2 w %s", path);
    ex(cmd);
    /* Only the addressed line is written. */
    CHECK(strcmp(slurp(path), "two\n") == 0);
    unlink(path);
}

static void
test_write_readonly(void)
{
    char path[64];

    mktemp_path(path, sizeof(path));
    init("one\n");
    g.current_filename = xstrdup(path);
    SET_READONLY_MODE(g.readonly_mode);
    ex("w");
    CHECK(said("read only"));

    /* ! overrides that too. */
    ex("w!");
    CHECK(strcmp(slurp(path), "one\n") == 0);
    unlink(path);
}

static void
test_write_and_quit(void)
{
    char path[64];

    mktemp_path(path, sizeof(path));
    init("one\n");
    g.current_filename = xstrdup(path);
    g.modified_count   = 1;
    ex("wq");
    CHECK(strcmp(slurp(path), "one\n") == 0);
    /* :wq leaves the edit loop. */
    CHECK(g.editing == 0);
    unlink(path);
}

/* ---- :read -------------------------------------------------------------- */

static void
test_read_a_file(void)
{
    char path[64];
    char cmd[128];
    int fd;

    mktemp_path(path, sizeof(path));
    fd = open(path, O_WRONLY);
    if (fd < 0 || write(fd, "inserted\n", 9) != 9)
        abort();
    close(fd);

    init("one\ntwo\n");
    snprintf(cmd, sizeof(cmd), "1 r %s", path);
    ex(cmd);
    /* The file goes in after the addressed line. */
    CHECK(strcmp(dump(), "one\ninserted\ntwo\n") == 0);
    unlink(path);
}

static void
test_read_is_undoable(void)
{
    char path[64];
    char cmd[128];
    int fd;

    mktemp_path(path, sizeof(path));
    fd = open(path, O_WRONLY);
    if (fd < 0 || write(fd, "x\n", 2) != 2)
        abort();
    close(fd);

    init("one\n");
    snprintf(cmd, sizeof(cmd), "1 r %s", path);
    ex(cmd);
    CHECK(strcmp(dump(), "one\nx\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "one\n") == 0);
    unlink(path);
}

static void
test_read_missing_file(void)
{
    init("one\n");
    ex("1 r /no/such/file");
    CHECK(strcmp(dump(), "one\n") == 0);
}

/* ---- :quit -------------------------------------------------------------- */

static void
test_quit_clean(void)
{
    init("one\n");
    g.cmdline_filecnt = 1;
    ex("q");
    CHECK(g.editing == 0);
}

static void
test_quit_refuses_unsaved_changes(void)
{
    init("one\n");
    g.modified_count  = 1;
    g.cmdline_filecnt = 1;
    ex("q");
    CHECK(said("No write since last change"));
    CHECK(g.editing == 1);
}

static void
test_quit_forced(void)
{
    init("one\n");
    g.modified_count  = 1;
    g.cmdline_filecnt = 1;
    ex("q!");
    CHECK(g.editing == 0);
}

static void
test_quit_with_more_files(void)
{
    init("one\n");
    g.cmdline_filecnt = 3;
    optind            = 0;
    ex("q");
    CHECK(said("more file"));
    CHECK(g.editing == 1);
}

/* ---- :run --------------------------------------------------------------- */

static void
test_run_dispatch(void)
{
    init("hello\n");
    ex("ru upper");
    /* :run hands the range to run.c, which is linked for real. */
    CHECK(strcmp(dump(), "HELLO\n") == 0);
}

static void
test_run_over_a_range(void)
{
    init("one\ntwo\n");
    ex("2 ru upper");
    CHECK(strcmp(dump(), "one\nTWO\n") == 0);
}

static void
test_run_with_arguments(void)
{
    init("c\na\nb\n");
    ex("ru sort -r");
    CHECK(strcmp(dump(), "c\nb\na\n") == 0);
}

static void
test_run_unknown(void)
{
    init("x\n");
    ex("ru nosuchthing");
    CHECK(said("unknown command"));
}

/* ---- command-name resolution -------------------------------------------- */

static void
test_prefix_resolution(void)
{
    /* The table is ordered and each entry has a minimum length, so these
     * abbreviations must each reach the intended command. */
    init("one\ntwo\n");
    ex("1d"); /* delete */
    CHECK(strcmp(dump(), "two\n") == 0);

    init("x\n");
    ex("se nu"); /* set, not substitute */
    CHECK((g.setops & VI_NUMBER) != 0);

    init("aa\n");
    ex("s/aa/X/"); /* substitute, not set */
    CHECK(strcmp(dump(), "X\n") == 0);

    init("one\ntwo\n");
    ex("1y"); /* yank */
    CHECK(strcmp(g.reg[g.ydreg], "one\n") == 0);
}

static void
test_dot_is_clamped_after_every_command(void)
{
    init("one\ntwo\nthree\n");
    /* Deleting the last line leaves dot past the end unless colon()
     * clamps it, which it does on the way out. */
    g.dot = 8;
    ex("3d");
    CHECK(g.dot >= 0);
    CHECK(g.dot <= text_len(&g) - 1);
}

static void
test_commands_survive_a_minimal_buffer(void)
{
    static const char *cmds[] = {"1d",     "1y",   "s/x/y/", "g/x/d",
                                 "v/x/d",  "se nu", "ma a",   "1l",
                                 "=",      "f",     "ve",     "ru upper"};
    size_t i;

    /* A one-byte buffer is the smallest legal one.  No command may crash
     * or corrupt the piece tree on it. */
    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        init("\n");
        ex(cmds[i]);
        CHECK(ptcheck(g.pt) == 1);
        CHECK(g.dot >= 0);
    }
}

int
main(void)
{
    static char tmpl[] = "/tmp/vic_test_ex_cache_XXXXXX";

    /* operator.c persists the shared register under XDG_CACHE_HOME, and
     * ex.c writes rows and escapes to stdout. */
    if (!mkdtemp(tmpl))
        abort();
    setenv("XDG_CACHE_HOME", tmpl, 1);
    if (!freopen("/dev/null", "w", stdout))
        abort();

    test_empty_command();
    test_leading_colons_and_space();
    test_comment();
    test_unknown_command();

    test_bare_line_address();
    test_dollar_address();
    test_dot_address();
    test_relative_address();
    test_range_address();
    test_whole_file_address();
    test_search_address();
    test_mark_address();
    test_invalid_address();
    test_reversed_range();
    test_line_number_report();

    test_delete();
    test_delete_is_undoable();
    test_delete_range();
    test_delete_abbreviations();
    test_yank();
    test_yank_range();

    test_substitute();
    test_substitute_first_only();
    test_substitute_global_flag();
    test_substitute_over_a_range();
    test_substitute_whole_file();
    test_substitute_regex();
    test_substitute_anchors();
    test_substitute_delete();
    test_substitute_no_match();
    test_substitute_is_undoable();
    test_substitute_alternative_delimiter();

    test_global_delete();
    test_global_substitute();
    test_vglobal_delete();
    test_global_no_match();

    test_set_boolean();
    test_set_tabstop();
    test_set_several_options();
    test_set_bad_option();
    test_set_needs_two_letters();

    test_mark();
    test_mark_with_an_address();

    test_file_redisplays_the_status_line();
    test_file_changes_the_name();
    test_file_rejects_an_address();
    test_list();
    test_version();
    test_features();

    test_write_to_the_current_file();
    test_write_to_a_named_file();
    test_write_refuses_to_clobber();
    test_write_a_range();
    test_write_readonly();
    test_write_and_quit();

    test_read_a_file();
    test_read_is_undoable();
    test_read_missing_file();

    test_quit_clean();
    test_quit_refuses_unsaved_changes();
    test_quit_forced();
    test_quit_with_more_files();

    test_run_dispatch();
    test_run_over_a_range();
    test_run_with_arguments();
    test_run_unknown();

    test_prefix_resolution();
    test_dot_is_clamped_after_every_command();
    test_commands_survive_a_minimal_buffer();

    SUMMARY();
}
