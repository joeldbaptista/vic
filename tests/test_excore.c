/*
 * test_excore.c - unit tests for excore.c
 *
 * excore.c holds the two ex helpers that session.c needs without the rest
 * of ex.c:
 *
 *   setops       — parse one ':set' argument and update g->setops,
 *                  g->tabstop or the cursor shapes.
 *   expand_args  — expand '%' and '#' in an ex argument string.
 *
 * Four symbols are stubbed: status_line_bold, which the tests use to
 * detect a rejected option, and the three term_cursor_shape_set_*
 * setters, which record the shape they were handed and reject anything
 * outside 0..6 the way term.c does.
 *
 * setops mutates its argument in place around the '=', so every test
 * passes a writable copy.
 */
#include "testutil.h"
#include "excore.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static struct editor g;
static int status_calls;
static char status_last[256];
static int shape_configured, shape_insert, shape_ex;

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

/* term.c accepts shapes 0..6 and rejects the rest; the stubs do the same
 * so that setops's error path is exercised. */
static int
shape_ok(int s)
{
    return s >= 0 && s <= 6;
}

int
term_cursor_shape_set_configured(int shape)
{
    if (!shape_ok(shape))
        return -1;
    shape_configured = shape;
    return 0;
}

int
term_cursor_shape_set_insert(int shape)
{
    if (!shape_ok(shape))
        return -1;
    shape_insert = shape;
    return 0;
}

int
term_cursor_shape_set_ex(int shape)
{
    if (!shape_ok(shape))
        return -1;
    shape_ex = shape;
    return 0;
}

/* ---- fixture ------------------------------------------------------------- */

static void
init(void)
{
    memset(&g, 0, sizeof(g));
    g.tabstop                   = 8;
    g.refresh_last_screenbegin  = 0;
    status_calls                = 0;
    status_last[0]              = '\0';
    shape_configured = shape_insert = shape_ex = -1;
}

/* Run setops on a writable copy of `opt`. */
static void
set(const char *opt, int flg_no)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%s", opt);
    setops(&g, buf, flg_no);
}

/* ---- boolean options ----------------------------------------------------- */

static void
test_boolean_options(void)
{
    /* Every boolean option, by long name and by short name.  This list is
     * exhaustive over the boolean entries of OPTS_STR. */
    init();
    set("autoindent", 0);
    CHECK(g.setops == VI_AUTOINDENT);
    CHECK(status_calls == 0);

    init();
    set("ai", 0);
    CHECK(g.setops == VI_AUTOINDENT);

    init();
    set("expandtab", 0);
    CHECK(g.setops == VI_EXPANDTAB);
    init();
    set("et", 0);
    CHECK(g.setops == VI_EXPANDTAB);

    init();
    set("flash", 0);
    CHECK(g.setops == VI_ERR_METHOD);
    init();
    set("fl", 0);
    CHECK(g.setops == VI_ERR_METHOD);

    init();
    set("ignorecase", 0);
    CHECK(g.setops == VI_IGNORECASE);
    init();
    set("ic", 0);
    CHECK(g.setops == VI_IGNORECASE);

    init();
    set("showmatch", 0);
    CHECK(g.setops == VI_SHOWMATCH);
    init();
    set("sm", 0);
    CHECK(g.setops == VI_SHOWMATCH);

    init();
    set("number", 0);
    CHECK(g.setops == VI_NUMBER);
    init();
    set("nu", 0);
    CHECK(g.setops == VI_NUMBER);

    init();
    set("relativenumber", 0);
    CHECK(g.setops == VI_RELATIVENUMBER);
    init();
    set("rnu", 0);
    CHECK(g.setops == VI_RELATIVENUMBER);

    init();
    set("undofile", 0);
    CHECK(g.setops == VI_UNDOFILE);
    init();
    set("uf", 0);
    CHECK(g.setops == VI_UNDOFILE);
}

static void
test_boolean_no_prefix(void)
{
    /* flg_no is the number of bytes of "no" prefix to skip, so the
     * caller in ex.c passes 2, not a boolean 1.  args still carries the
     * prefix; setops indexes past it. */
    init();
    g.setops = VI_AUTOINDENT | VI_NUMBER;
    set("noautoindent", 2);
    CHECK(g.setops == VI_NUMBER);

    init();
    g.setops = VI_NUMBER;
    set("nonu", 2);
    CHECK(g.setops == 0);
}

static void
test_boolean_set_twice(void)
{
    /* Setting an option that is already set is idempotent: the flag is
     * OR-ed in rather than toggled. */
    init();
    set("number", 0);
    set("number", 0);
    CHECK(g.setops == VI_NUMBER);
}

static void
test_clearing_an_unset_option(void)
{
    init();
    set("nonumber", 2);
    CHECK(g.setops == 0);
    CHECK(status_calls == 0);
}

/* ---- options that invalidate the screen cache ---------------------------- */

static void
test_number_invalidates_cache(void)
{
    /* The line-number gutter changes the text width, so the refresh cache
     * stamp must be cleared. */
    init();
    set("number", 0);
    CHECK(g.refresh_last_screenbegin == -1);

    init();
    set("relativenumber", 0);
    CHECK(g.refresh_last_screenbegin == -1);

    /* An option that does not affect layout leaves the stamp alone. */
    init();
    set("ignorecase", 0);
    CHECK(g.refresh_last_screenbegin == 0);
}

/* ---- syntax toggles rather than sets ------------------------------------- */

static void
test_syntax_toggles(void)
{
    init();
    /* ':set syntax' flips the flag, unlike every other boolean option. */
    set("syntax", 0);
    CHECK(g.setops == VI_SYNTAX);
    set("syntax", 0);
    CHECK(g.setops == 0);
    set("syn", 0);
    CHECK(g.setops == VI_SYNTAX);

    /* ':set nosyntax' forces it off and keeps it off. */
    set("nosyntax", 2);
    CHECK(g.setops == 0);
    set("nosyntax", 2);
    CHECK(g.setops == 0);
    CHECK(g.refresh_last_screenbegin == -1);
}

/* ---- tabstop ------------------------------------------------------------- */

static void
test_tabstop(void)
{
    init();
    set("tabstop=4", 0);
    CHECK(g.tabstop == 4);
    CHECK(g.refresh_last_screenbegin == -1);
    CHECK(status_calls == 0);

    init();
    set("ts=2", 0);
    CHECK(g.tabstop == 2);

    /* The accepted range is 1 to 32 inclusive. */
    init();
    set("ts=1", 0);
    CHECK(g.tabstop == 1);
    init();
    set("ts=32", 0);
    CHECK(g.tabstop == 32);
}

static void
test_tabstop_rejected(void)
{
    /* Out of range, zero, negative and missing values are all refused,
     * and the old tabstop survives. */
    init();
    set("ts=0", 0);
    CHECK(g.tabstop == 8);
    CHECK(status_calls == 1);

    init();
    set("ts=33", 0);
    CHECK(g.tabstop == 8);
    CHECK(status_calls == 1);

    init();
    set("ts", 0);
    CHECK(g.tabstop == 8);
    CHECK(status_calls == 1);

    /* ':set notabstop' makes no sense for an integer option. */
    init();
    set("notabstop", 2);
    CHECK(g.tabstop == 8);
    CHECK(status_calls == 1);
}

static void
test_tabstop_non_numeric(void)
{
    init();
    /* strtoul yields 0 for a non-numeric value, which the range check
     * then rejects. */
    set("ts=abc", 0);
    CHECK(g.tabstop == 8);
    CHECK(status_calls == 1);
}

/* ---- cursor shapes ------------------------------------------------------- */

static void
test_cursor_shapes(void)
{
    init();
    set("cursorshape=2", 0);
    CHECK(shape_configured == 2);
    CHECK(status_calls == 0);

    init();
    set("cshp=4", 0);
    CHECK(shape_configured == 4);

    init();
    set("cursorshapeinsert=5", 0);
    CHECK(shape_insert == 5);
    init();
    set("cshpi=1", 0);
    CHECK(shape_insert == 1);

    init();
    set("cursorshapeex=6", 0);
    CHECK(shape_ex == 6);
    init();
    set("cshpe=0", 0);
    CHECK(shape_ex == 0);
}

static void
test_cursor_shape_rejected(void)
{
    /* A shape the terminal layer refuses is reported, and no shape is
     * recorded. */
    init();
    set("cursorshape=9", 0);
    CHECK(shape_configured == -1);
    CHECK(status_calls == 1);

    /* Like tabstop, these need a value and reject the "no" form. */
    init();
    set("cursorshape", 0);
    CHECK(status_calls == 1);
    init();
    set("nocursorshape", 2);
    CHECK(status_calls == 1);
}

/* ---- unknown options ----------------------------------------------------- */

static void
test_unknown_option(void)
{
    init();
    set("nosuchoption", 0);
    CHECK(g.setops == 0);
    CHECK(status_calls == 1);
    CHECK(strstr(status_last, "bad option") != NULL);
    /* The message quotes the option as given, '=' value included. */
    CHECK(strstr(status_last, "nosuchoption") != NULL);

    init();
    set("", 0);
    CHECK(status_calls == 1);
}

static void
test_boolean_with_value_rejected(void)
{
    /* A boolean option takes no '=' value. */
    init();
    set("number=3", 0);
    CHECK(g.setops == 0);
    CHECK(status_calls == 1);

    init();
    set("ai=1", 0);
    CHECK(g.setops == 0);
    CHECK(status_calls == 1);
}

static void
test_argument_is_restored(void)
{
    /* setops writes a NUL over the '=' to look the name up, then puts the
     * '=' back, because the caller still owns the string. */
    char buf[32];

    init();
    snprintf(buf, sizeof(buf), "ts=4");
    setops(&g, buf, 0);
    CHECK(strcmp(buf, "ts=4") == 0);

    snprintf(buf, sizeof(buf), "number=3");
    setops(&g, buf, 0);
    CHECK(strcmp(buf, "number=3") == 0);
}

/* ---- expand_args --------------------------------------------------------- */

static void
test_expand_percent(void)
{
    char *s;

    init();
    g.current_filename = (char *)"file.c";
    s                  = expand_args(&g, "e %");
    CHECK(s != NULL);
    CHECK(strcmp(s, "e file.c") == 0);
    free(s);

    /* Every occurrence is expanded, not just the first. */
    s = expand_args(&g, "% %");
    CHECK(strcmp(s, "file.c file.c") == 0);
    free(s);
}

static void
test_expand_hash(void)
{
    char *s;

    init();
    g.current_filename = (char *)"cur.c";
    g.alt_filename     = (char *)"alt.c";
    s                  = expand_args(&g, "e #");
    CHECK(strcmp(s, "e alt.c") == 0);
    free(s);

    /* Both in one string, each from its own slot. */
    s = expand_args(&g, "% #");
    CHECK(strcmp(s, "cur.c alt.c") == 0);
    free(s);
}

static void
test_expand_unset(void)
{
    char *s;

    /* With no current filename, '%' is an error rather than an empty
     * expansion. */
    init();
    s = expand_args(&g, "e %");
    CHECK(s == NULL);
    CHECK(status_calls == 1);
    CHECK(strstr(status_last, "No previous filename") != NULL);

    /* Same for '#'. */
    init();
    g.current_filename = (char *)"cur.c";
    s                  = expand_args(&g, "e #");
    CHECK(s == NULL);
    CHECK(status_calls == 1);
}

static void
test_expand_escape(void)
{
    char *s;

    init();
    g.current_filename = (char *)"file.c";
    /* A backslash protects the next character and is itself removed. */
    s = expand_args(&g, "a\\%b");
    CHECK(strcmp(s, "a%b") == 0);
    free(s);

    s = expand_args(&g, "a\\#b");
    CHECK(strcmp(s, "a#b") == 0);
    free(s);

    /* A backslash before an ordinary character is still removed. */
    s = expand_args(&g, "a\\xb");
    CHECK(strcmp(s, "axb") == 0);
    free(s);

    /* A trailing backslash has nothing to escape and survives. */
    s = expand_args(&g, "ab\\");
    CHECK(strcmp(s, "ab\\") == 0);
    free(s);

    /* An escaped backslash followed by '%' hits the same off-by-one as
     * test_expand_adjacent: the '%' is stepped over unexpanded. */
    s = expand_args(&g, "\\\\%");
    CHECK(strcmp(s, "\\%") == 0); /* should be "\\a.c" */
    free(s);
}

static void
test_expand_no_metacharacters(void)
{
    char *s;

    init();
    /* A plain string comes back as an independent copy. */
    s = expand_args(&g, "plain text");
    CHECK(s != NULL);
    CHECK(strcmp(s, "plain text") == 0);
    free(s);

    s = expand_args(&g, "");
    CHECK(s != NULL && s[0] == '\0');
    free(s);
}

static void
test_expand_adjacent(void)
{
    char *s;

    init();
    g.current_filename = (char *)"a.c";
    g.alt_filename     = (char *)"b.c";
    /*
     * A byte immediately after an expansion is skipped, so a second
     * metacharacter touching the first is left unexpanded.  The scan
     * resumes at args + n + rlen and the loop's own s++ then steps one
     * byte past it.  This is a pre-existing defect, present on main and
     * unchanged by the piece-tree port, so the test pins what the code
     * does rather than what it should do.
     */
    s = expand_args(&g, "%#");
    CHECK(strcmp(s, "a.c#") == 0); /* should be "a.cb.c" */
    free(s);

    s = expand_args(&g, "%%");
    CHECK(strcmp(s, "a.c%") == 0); /* should be "a.ca.c" */
    free(s);

    /* With any byte in between, both expansions happen. */
    s = expand_args(&g, "% #");
    CHECK(strcmp(s, "a.c b.c") == 0);
    free(s);

    s = expand_args(&g, "%x%");
    CHECK(strcmp(s, "a.cxa.c") == 0);
    free(s);

    s = expand_args(&g, "x%y");
    CHECK(strcmp(s, "xa.cy") == 0);
    free(s);
}

static void
test_expand_longer_than_input(void)
{
    char *s;

    init();
    /* The replacement is longer than the whole argument, so the buffer
     * has to grow. */
    g.current_filename = (char *)"/a/very/long/path/to/a/file.c";
    s                  = expand_args(&g, "%");
    CHECK(strcmp(s, "/a/very/long/path/to/a/file.c") == 0);
    free(s);
}

int
main(void)
{
    test_boolean_options();
    test_boolean_no_prefix();
    test_boolean_set_twice();
    test_clearing_an_unset_option();
    test_number_invalidates_cache();
    test_syntax_toggles();

    test_tabstop();
    test_tabstop_rejected();
    test_tabstop_non_numeric();

    test_cursor_shapes();
    test_cursor_shape_rejected();

    test_unknown_option();
    test_boolean_with_value_rejected();
    test_argument_is_restored();

    test_expand_percent();
    test_expand_hash();
    test_expand_unset();
    test_expand_escape();
    test_expand_no_metacharacters();
    test_expand_adjacent();
    test_expand_longer_than_input();

    SUMMARY();
}
