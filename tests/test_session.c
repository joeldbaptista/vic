/*
 * test_session.c - unit tests for session.c
 *
 * session.c is startup: CLI parsing, the EXINIT/.exrc startup commands,
 * the per-file option seeding, the global initialisation, and the
 * top-level file loop.
 *
 * buffer.c and the colorizer registry (color.c and every colour module)
 * are linked for real, so init_text_buffer and apply_filetype_options do
 * their actual work.  Eleven symbols are stubbed: colon and edit_file
 * (recorded, which is how the command splitting and the file loop are
 * checked), show_help, term_cursor_shape_restore_startup, and the status,
 * screen and keyboard calls.
 *
 * Two functions are NOT covered, and both for the same reason:
 *   setup_stdin_file  — it drains the real stdin and dup2s /dev/tty onto
 *                       it, and calls die() (which exits) when either is
 *                       unavailable.
 *   run_editor_session with a real file — it enters the alternate screen.
 *     Its command sequence is checked with edit_file stubbed out instead.
 *
 * session.c writes escape sequences to stdout, so main() redirects it.
 * show_usage writes to stderr, so fd 2 is redirected around the one test
 * that reaches it and put back afterwards.
 */
#include "testutil.h"
#include "session.h"
#include "buffer.h"
#include "config.h"
#include "pt.h"
#include "text.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;

/* colon stub: record every command string it is handed. */
#define MAX_COLON 32
static char colon_log[MAX_COLON][128];
static int colon_calls;

void
colon(struct editor *x, char *buf)
{
    (void)x;
    if (colon_calls < MAX_COLON)
        snprintf(colon_log[colon_calls], sizeof(colon_log[0]), "%s", buf);
    colon_calls++;
}

/* edit_file stub: record the filenames the loop asks for. */
#define MAX_FILES 16
static char file_log[MAX_FILES][64];
static int file_calls;

void
edit_file(struct editor *x, char *fn)
{
    (void)x;
    if (file_calls < MAX_FILES)
        snprintf(file_log[file_calls], sizeof(file_log[0]), "%s",
                 fn ? fn : "(null)");
    file_calls++;
}

static int help_calls;

void
show_help(void)
{
    help_calls++;
}

static int shape_restores;

void
term_cursor_shape_restore_startup(void)
{
    shape_restores++;
}

static char status_last[256];
static int status_calls;

void
status_line(struct editor *x, const char *format, ...)
{
    (void)x;
    (void)format;
}

void
status_line_bold(struct editor *x, const char *format, ...)
{
    va_list ap;

    (void)x;
    status_calls++;
    va_start(ap, format);
    vsnprintf(status_last, sizeof(status_last), format, ap);
    va_end(ap);
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

/* ---- fixture ------------------------------------------------------------- */

static void
init(void)
{
    ptfree(g.pt);
    free(g.last_search_pattern);
    memset(&g, 0, sizeof(g));
    g.pt               = ptnew(NULL, 0);
    g.tabstop          = 8;
    g.undo_queue_state = UNDO_EMPTY;
    colon_calls        = 0;
    file_calls         = 0;
    help_calls         = 0;
    shape_restores     = 0;
    status_calls       = 0;
    status_last[0]     = '\0';
}

/* ---- run_cmds ------------------------------------------------------------ */

static void
test_run_cmds_single(void)
{
    char buf[] = "set number";

    init();
    run_cmds(&g, buf);
    CHECK(colon_calls == 1);
    CHECK(strcmp(colon_log[0], "set number") == 0);
}

static void
test_run_cmds_splits_on_newlines(void)
{
    char buf[] = "set number\nset ai\nset ts=4";

    init();
    run_cmds(&g, buf);
    CHECK(colon_calls == 3);
    CHECK(strcmp(colon_log[0], "set number") == 0);
    CHECK(strcmp(colon_log[1], "set ai") == 0);
    CHECK(strcmp(colon_log[2], "set ts=4") == 0);
}

static void
test_run_cmds_collapses_blank_lines(void)
{
    char buf[] = "one\n\n\ntwo\n";

    init();
    run_cmds(&g, buf);
    /*
     * A run of newlines counts as one separator.  The trailing newline
     * does leave a final empty command, which colon() itself ignores.
     */
    CHECK(colon_calls == 3);
    CHECK(strcmp(colon_log[0], "one") == 0);
    CHECK(strcmp(colon_log[1], "two") == 0);
    CHECK(strcmp(colon_log[2], "") == 0);
}

static void
test_run_cmds_empty(void)
{
    char buf[] = "";

    init();
    run_cmds(&g, buf);
    /* An empty string is still one (empty) command: colon itself decides
     * that there is nothing to do. */
    CHECK(colon_calls == 1);
    CHECK(strcmp(colon_log[0], "") == 0);

    init();
    run_cmds(&g, NULL);
    CHECK(colon_calls == 0);
}

/* ---- the -c queue -------------------------------------------------------- */

static void
test_initial_cmds(void)
{
    init();
    append_initial_cmd(&g, "set number");
    append_initial_cmd(&g, "1");
    CHECK(g.initial_cmdc == 2);

    run_initial_cmds(&g);
    /* They run in the order given. */
    CHECK(colon_calls == 2);
    CHECK(strcmp(colon_log[0], "set number") == 0);
    CHECK(strcmp(colon_log[1], "1") == 0);
    /* And the queue is emptied and freed. */
    CHECK(g.initial_cmdc == 0);
    CHECK(g.initial_cmdv == NULL);
}

static void
test_initial_cmds_multiline(void)
{
    init();
    /* One -c argument may itself hold several commands. */
    append_initial_cmd(&g, "set ai\nset number");
    run_initial_cmds(&g);
    CHECK(colon_calls == 2);
}

static void
test_initial_cmds_null(void)
{
    init();
    append_initial_cmd(&g, NULL);
    CHECK(g.initial_cmdc == 0);
}

static void
test_initial_cmds_empty_queue(void)
{
    init();
    run_initial_cmds(&g);
    CHECK(colon_calls == 0);
}

/* ---- parse_cli_options --------------------------------------------------- */

static void
test_parse_no_flags(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"file.c", NULL};

    init();
    CHECK(parse_cli_options(&g, 2, argv, &o) == 1);
    CHECK(o.readonly == 0 && o.pager == 0 && o.help == 0 && o.usage_only == 0);
}

static void
test_parse_readonly(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-R", (char *)"file.c", NULL};

    init();
    CHECK(parse_cli_options(&g, 3, argv, &o) == 2);
    CHECK(o.readonly == 1);
}

static void
test_parse_pager(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-p", NULL};

    init();
    CHECK(parse_cli_options(&g, 2, argv, &o) == 2);
    CHECK(o.pager == 1);
}

static void
test_parse_help_flags(void)
{
    struct cli_options o;
    char *argv1[] = {(char *)"vic", (char *)"-H", NULL};
    char *argv2[] = {(char *)"vic", (char *)"-h", NULL};

    init();
    parse_cli_options(&g, 2, argv1, &o);
    CHECK(o.help == 1 && o.usage_only == 0);

    init();
    parse_cli_options(&g, 2, argv2, &o);
    CHECK(o.usage_only == 1 && o.help == 0);
}

static void
test_parse_command_flag(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-c", (char *)"set number",
                    (char *)"file.c", NULL};

    init();
    CHECK(parse_cli_options(&g, 4, argv, &o) == 3);
    /* -c goes straight into the deferred queue. */
    CHECK(g.initial_cmdc == 1);
    CHECK(strcmp(g.initial_cmdv[0], "set number") == 0);
}

static void
test_parse_repeated_command_flag(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-c", (char *)"one",
                    (char *)"-c",  (char *)"two", NULL};

    init();
    parse_cli_options(&g, 5, argv, &o);
    CHECK(g.initial_cmdc == 2);
    CHECK(strcmp(g.initial_cmdv[0], "one") == 0);
    CHECK(strcmp(g.initial_cmdv[1], "two") == 0);
}

static void
test_parse_combined_flags(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-Rp", (char *)"f", NULL};

    init();
    CHECK(parse_cli_options(&g, 3, argv, &o) == 2);
    CHECK(o.readonly == 1 && o.pager == 1);
}

static void
test_parse_unknown_flag(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-Z", NULL};

    init();
    /* -1 tells the caller to give up rather than opening a file named
     * after the bad flag. */
    CHECK(parse_cli_options(&g, 2, argv, &o) == -1);
}

static void
test_parse_is_repeatable(void)
{
    struct cli_options o;
    char *argv[] = {(char *)"vic", (char *)"-R", (char *)"f", NULL};

    /* optind is reset on entry, so a second parse gives the same answer
     * rather than continuing where the first left off. */
    init();
    CHECK(parse_cli_options(&g, 3, argv, &o) == 2);
    CHECK(parse_cli_options(&g, 3, argv, &o) == 2);
}

/* ---- apply_cli_options --------------------------------------------------- */

static void
test_apply_readonly(void)
{
    struct cli_options o;

    init();
    memset(&o, 0, sizeof(o));
    o.readonly = 1;
    CHECK(apply_cli_options(&g, &o) == 0);
    /* Bit 1 of readonly_mode is the read-only-mode flag. */
    CHECK((g.readonly_mode & 0x02) != 0);
    CHECK(g.pager_mode == 0);
}

static void
test_apply_pager_implies_readonly(void)
{
    struct cli_options o;

    init();
    memset(&o, 0, sizeof(o));
    o.pager = 1;
    CHECK(apply_cli_options(&g, &o) == 0);
    /* A pager must never write the document back. */
    CHECK((g.readonly_mode & 0x02) != 0);
    CHECK(g.pager_mode == 1);
}

static void
test_apply_default(void)
{
    struct cli_options o;

    init();
    memset(&o, 0, sizeof(o));
    CHECK(apply_cli_options(&g, &o) == 0);
    CHECK(g.readonly_mode == 0);
    CHECK(g.pager_mode == 0);
}

/* show_usage (compat.c, linked for real) writes to stderr, so fd 2 is
 * redirected for the one test that reaches it. */
static int saved_stderr = -1;

static void
quiet_stderr(void)
{
    int devnull = open("/dev/null", O_WRONLY);

    saved_stderr = dup(2);
    if (devnull < 0 || saved_stderr < 0 || dup2(devnull, 2) < 0)
        abort();
    close(devnull);
}

static void
restore_stderr(void)
{
    if (dup2(saved_stderr, 2) < 0)
        abort();
    close(saved_stderr);
    saved_stderr = -1;
}

static void
test_apply_help_exits(void)
{
    struct cli_options o;

    quiet_stderr();
    init();
    memset(&o, 0, sizeof(o));
    o.help = 1;
    /* -1 means "print and quit". */
    CHECK(apply_cli_options(&g, &o) == -1);
    CHECK(help_calls == 1);

    init();
    memset(&o, 0, sizeof(o));
    o.usage_only = 1;
    CHECK(apply_cli_options(&g, &o) == -1);
    /* -h prints the usage summary only, not the full help. */
    CHECK(help_calls == 0);
    restore_stderr();
}

/* ---- apply_filetype_options ---------------------------------------------- */

static void
test_filetype_tabstop(void)
{
    init();
    g.tabstop = 99;
    /* YAML declares a tabstop of 2. */
    apply_filetype_options(&g, "config.yaml");
    CHECK(g.tabstop == 2);

    /* Python declares 4. */
    apply_filetype_options(&g, "script.py");
    CHECK(g.tabstop == 4);

    /* C declares 8. */
    apply_filetype_options(&g, "main.c");
    CHECK(g.tabstop == 8);
}

static void
test_filetype_resets_to_the_default(void)
{
    init();
    apply_filetype_options(&g, "config.yaml");
    CHECK(g.tabstop == 2);
    /*
     * An unrecognised type must go back to the compiled-in default rather
     * than inheriting the previous file's tabstop.
     */
    apply_filetype_options(&g, "notes.unknownext");
    CHECK(g.tabstop == CFG_TABSTOP);
}

static void
test_filetype_null_name(void)
{
    init();
    g.tabstop = 99;
    apply_filetype_options(&g, NULL);
    CHECK(g.tabstop == CFG_TABSTOP);
}

/* ---- init_globals -------------------------------------------------------- */

static void
test_init_globals(void)
{
    int i;

    memset(&g, 0, sizeof(g));
    init_globals(&g);

    /* The compile-time defaults are seeded from config.h. */
    CHECK(g.tabstop == CFG_TABSTOP);
    CHECK(((g.setops & VI_NUMBER) != 0) == (CFG_NUMBER != 0));
    CHECK(((g.setops & VI_SYNTAX) != 0) == (CFG_SYNTAX != 0));
    CHECK(((g.setops & VI_AUTOINDENT) != 0) == (CFG_AUTOINDENT != 0));
    CHECK(g.color_escape == (CFG_COLORESCAPE ? 1 : 0));

    /* Every offset field that used to be a NULL pointer starts at -1. */
    CHECK(g.rstart == -1);
    CHECK(g.visual_anchor == -1);
    CHECK(g.refresh_last_screenbegin == -1);
    CHECK(g.undo_queue_spos == -1);
    CHECK(g.dot_line == -1);
    CHECK(g.newindent == -1);

    /* No mark is set yet. */
    for (i = 0; i < (int)(sizeof(g.mark) / sizeof(g.mark[0])); i++)
        CHECK(g.mark[i] == -1);

    /* The cache stamps start at the lowest value so the first refresh
     * cannot be mistaken for a cache hit. */
    CHECK(g.line_count_cache_stamp == INT_MIN);
    CHECK(g.refresh_last_modified_count == INT_MIN);
    CHECK(g.last_modified_count == -1);

    /* The search pattern is a two-byte buffer holding the direction byte
     * and its terminator. */
    CHECK(g.last_search_pattern != NULL);
    CHECK(g.last_search_pattern[0] == '\0');
    CHECK(g.undo_queue_state == UNDO_EMPTY);
    CHECK(g.session_epoch > 0);
}

/* ---- load_startup_cmds --------------------------------------------------- */

static void
test_startup_from_exinit(void)
{
    char *cmds;

    init();
    setenv("EXINIT", "set number", 1);
    cmds = load_startup_cmds(&g);
    CHECK(cmds != NULL);
    CHECK(strcmp(cmds, "set number") == 0);
    free(cmds);
    unsetenv("EXINIT");
}

static void
test_startup_from_exrc(void)
{
    char dir[] = "/tmp/vic_test_session_XXXXXX";
    char path[128];
    char *cmds;
    FILE *f;

    if (!mkdtemp(dir))
        abort();
    snprintf(path, sizeof(path), "%s/.exrc", dir);
    f = fopen(path, "w");
    if (!f)
        abort();
    fputs("set ai\n", f);
    fclose(f);
    chmod(path, 0600);

    init();
    unsetenv("EXINIT");
    setenv("HOME", dir, 1);
    cmds = load_startup_cmds(&g);
    CHECK(cmds != NULL);
    CHECK(strstr(cmds, "set ai") != NULL);
    free(cmds);

    unlink(path);
    rmdir(dir);
}

static void
test_exinit_wins_over_exrc(void)
{
    char dir[] = "/tmp/vic_test_session_XXXXXX";
    char path[128];
    char *cmds;
    FILE *f;

    if (!mkdtemp(dir))
        abort();
    snprintf(path, sizeof(path), "%s/.exrc", dir);
    f = fopen(path, "w");
    if (!f)
        abort();
    fputs("set ai\n", f);
    fclose(f);
    chmod(path, 0600);

    init();
    setenv("HOME", dir, 1);
    setenv("EXINIT", "set number", 1);
    cmds = load_startup_cmds(&g);
    /* EXINIT is checked first and .exrc is not read at all. */
    CHECK(cmds != NULL);
    CHECK(strcmp(cmds, "set number") == 0);
    free(cmds);
    unsetenv("EXINIT");

    unlink(path);
    rmdir(dir);
}

static void
test_exrc_group_writable_is_refused(void)
{
    char dir[] = "/tmp/vic_test_session_XXXXXX";
    char path[128];
    char *cmds;
    FILE *f;

    if (!mkdtemp(dir))
        abort();
    snprintf(path, sizeof(path), "%s/.exrc", dir);
    f = fopen(path, "w");
    if (!f)
        abort();
    fputs("set ai\n", f);
    fclose(f);
    /* Group-writable: another user could have planted the commands. */
    chmod(path, 0620);

    init();
    unsetenv("EXINIT");
    setenv("HOME", dir, 1);
    cmds = load_startup_cmds(&g);
    CHECK(cmds == NULL);
    CHECK(status_calls == 1);
    CHECK(strstr(status_last, "permission denied") != NULL);

    unlink(path);
    rmdir(dir);
}

static void
test_exrc_world_writable_is_refused(void)
{
    char dir[] = "/tmp/vic_test_session_XXXXXX";
    char path[128];
    char *cmds;
    FILE *f;

    if (!mkdtemp(dir))
        abort();
    snprintf(path, sizeof(path), "%s/.exrc", dir);
    f = fopen(path, "w");
    if (!f)
        abort();
    fputs("set ai\n", f);
    fclose(f);
    chmod(path, 0602);

    init();
    unsetenv("EXINIT");
    setenv("HOME", dir, 1);
    cmds = load_startup_cmds(&g);
    CHECK(cmds == NULL);
    CHECK(status_calls == 1);

    unlink(path);
    rmdir(dir);
}

static void
test_no_startup_cmds(void)
{
    char dir[] = "/tmp/vic_test_session_XXXXXX";

    if (!mkdtemp(dir))
        abort();
    init();
    unsetenv("EXINIT");
    setenv("HOME", dir, 1);
    /* No .exrc at all: nothing to run and nothing to report. */
    CHECK(load_startup_cmds(&g) == NULL);
    CHECK(status_calls == 0);
    rmdir(dir);
}

static void
test_run_startup_cmds(void)
{
    init();
    setenv("EXINIT", "set number\nset ai", 1);
    run_startup_cmds(&g);
    /* A scratch buffer is created so colon has something to act on, then
     * both commands run. */
    CHECK(text_len(&g) == 1);
    CHECK(colon_calls == 2);
    unsetenv("EXINIT");
}

static void
test_run_startup_cmds_with_none(void)
{
    char dir[] = "/tmp/vic_test_session_XXXXXX";

    if (!mkdtemp(dir))
        abort();
    init();
    unsetenv("EXINIT");
    setenv("HOME", dir, 1);
    run_startup_cmds(&g);
    /* No commands: the scratch buffer is not even created. */
    CHECK(colon_calls == 0);
    CHECK(text_len(&g) == 0);
    rmdir(dir);
}

/* ---- the file loop ------------------------------------------------------- */

static void
test_file_loop_one_file(void)
{
    char *argv[] = {(char *)"a.c", NULL};

    init();
    g.cmdline_filecnt = 1;
    run_file_loop(&g, argv);
    CHECK(file_calls == 1);
    CHECK(strcmp(file_log[0], "a.c") == 0);
}

static void
test_file_loop_several_files(void)
{
    char *argv[] = {(char *)"a.c", (char *)"b.c", (char *)"c.c", NULL};

    init();
    g.cmdline_filecnt = 3;
    run_file_loop(&g, argv);
    CHECK(file_calls == 3);
    CHECK(strcmp(file_log[0], "a.c") == 0);
    CHECK(strcmp(file_log[1], "b.c") == 0);
    CHECK(strcmp(file_log[2], "c.c") == 0);
}

static void
test_file_loop_no_files(void)
{
    char *argv[] = {NULL};

    init();
    g.cmdline_filecnt = 0;
    run_file_loop(&g, argv);
    /* With no filenames the loop still runs once, on a scratch buffer. */
    CHECK(file_calls == 1);
    CHECK(strcmp(file_log[0], "(null)") == 0);
}

static void
test_editor_session(void)
{
    char *argv[] = {(char *)"a.c", NULL};
    char dir[] = "/tmp/vic_test_session_XXXXXX";

    if (!mkdtemp(dir))
        abort();
    init();
    unsetenv("EXINIT");
    setenv("HOME", dir, 1);
    g.cmdline_filecnt = 1;
    run_editor_session(&g, argv);
    /* The lifecycle is: startup commands, then the file loop, then the
     * cursor shape is put back. */
    CHECK(file_calls == 1);
    CHECK(shape_restores == 1);
    rmdir(dir);
}

int
main(void)
{
    /* session.c writes the alternate-screen escapes to stdout. */
    if (!freopen("/dev/null", "w", stdout))
        abort();

    test_run_cmds_single();
    test_run_cmds_splits_on_newlines();
    test_run_cmds_collapses_blank_lines();
    test_run_cmds_empty();

    test_initial_cmds();
    test_initial_cmds_multiline();
    test_initial_cmds_null();
    test_initial_cmds_empty_queue();

    test_parse_no_flags();
    test_parse_readonly();
    test_parse_pager();
    test_parse_help_flags();
    test_parse_command_flag();
    test_parse_repeated_command_flag();
    test_parse_combined_flags();
    test_parse_unknown_flag();
    test_parse_is_repeatable();

    test_apply_readonly();
    test_apply_pager_implies_readonly();
    test_apply_default();
    test_apply_help_exits();

    test_filetype_tabstop();
    test_filetype_resets_to_the_default();
    test_filetype_null_name();

    test_init_globals();

    test_startup_from_exinit();
    test_startup_from_exrc();
    test_exinit_wins_over_exrc();
    test_exrc_group_writable_is_refused();
    test_exrc_world_writable_is_refused();
    test_no_startup_cmds();
    test_run_startup_cmds();
    test_run_startup_cmds_with_none();

    test_file_loop_one_file();
    test_file_loop_several_files();
    test_file_loop_no_files();
    test_editor_session();

    SUMMARY();
}
