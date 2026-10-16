/*
 * test_search.c - unit tests for search.c
 *
 * search.c holds the regex search commands (/ ? n N), the word-under-
 * cursor searches (* # g* g#), and char_search(), the primitive both use.
 * The vendored regex engine (regex.c) is linked for real, as are line.c
 * and codepoint.c.
 *
 * Five symbols are stubbed: status_line_bold (captured, because
 * "Pattern not found" and the wrap-around notice are the only visible
 * results of a failed or wrapped search), indicate_error and refresh
 * (counted), and get_one_char and go_bottom_and_clear_to_eol, which only
 * the interactive prompt reaches.
 *
 * get_input_line() is not covered: it reads keys from the terminal and
 * draws on the status line, so it belongs to the PTY harness in
 * tools/check-pty rather than here.  Every path through search_run_cmd
 * that this test drives supplies the pattern through cmd_ctx, which is
 * what the command parser does.
 *
 * The direction/scope argument of char_search is encoded: positive is
 * forward, negative is backward, and bit 0 ("FULL") widens the scope from
 * the current line to the whole buffer.
 */
#include "testutil.h"
#include "search.h"
#include "pt.h"
#include "text.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* Mirrors the private enum in search.c. */
enum {
    FORWARD = 1,
    BACK    = -1,
    FULL    = 1
};

#define FWD_LINE 2          /* forward, current line only */
#define FWD_ALL (2 | FULL)  /* forward, whole buffer */
#define BWD_LINE (-2)       /* backward, current line only */
#define BWD_ALL ((-2) | FULL)

static struct editor g;
static int errors;
static int refreshes;
static int status_calls;
static char status_last[256];

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
indicate_error(struct editor *x)
{
    (void)x;
    errors++;
}

void
refresh(struct editor *x, int full_screen)
{
    (void)x;
    (void)full_screen;
    refreshes++;
}

int
get_one_char(struct editor *x)
{
    (void)x;
    return 0;
}

void
go_bottom_and_clear_to_eol(struct editor *x)
{
    (void)x;
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
    free(g.last_search_pattern);
    memset(&g, 0, sizeof(g));
    g.pt      = ptnew(copy, n);
    g.tabstop = 8;
    errors         = 0;
    refreshes      = 0;
    status_calls   = 0;
    status_last[0] = '\0';
}

/* ---- char_search: forward ------------------------------------------------ */

static void
test_char_search_forward(void)
{
    /* f0 o1 o2 ' '3 b4 a5 r6 \n7 f8 o9 o10 \n11 */
    init("foo bar\nfoo\n");
    CHECK(char_search(&g, 0, "foo", FWD_ALL) == 0);
    CHECK(char_search(&g, 1, "foo", FWD_ALL) == 8);
    CHECK(char_search(&g, 0, "bar", FWD_ALL) == 4);
    /* The match offset is the start of the match, not its end. */
    CHECK(char_search(&g, 0, "ar", FWD_ALL) == 5);
}

static void
test_char_search_forward_absent(void)
{
    init("foo bar\n");
    CHECK(char_search(&g, 0, "zzz", FWD_ALL) == -1);
}

static void
test_char_search_scope_is_one_line(void)
{
    /* Without FULL the search stops at the end of the starting line. */
    init("foo\nbar\n");
    CHECK(char_search(&g, 0, "bar", FWD_LINE) == -1);
    CHECK(char_search(&g, 0, "bar", FWD_ALL) == 4);
    /* A match on the starting line is found either way. */
    CHECK(char_search(&g, 0, "foo", FWD_LINE) == 0);
}

/* ---- char_search: backward ----------------------------------------------- */

static void
test_char_search_backward(void)
{
    /* f0 o1 o2 \n3 f4 o5 o6 \n7 f8 o9 o10 \n11 */
    init("foo\nfoo\nfoo\n");
    /* Backward finds the last match strictly before p. */
    CHECK(char_search(&g, 11, "foo", BWD_ALL) == 8);
    CHECK(char_search(&g, 8, "foo", BWD_ALL) == 4);
    CHECK(char_search(&g, 4, "foo", BWD_ALL) == 0);
    /* Nothing before the first one. */
    CHECK(char_search(&g, 0, "foo", BWD_ALL) == -1);
}

static void
test_char_search_backward_rightmost_on_a_line(void)
{
    /* a0 b1 a2 b3 a4 \n5 — the last "a" before p, not the first. */
    init("ababa\n");
    CHECK(char_search(&g, 5, "a", BWD_ALL) == 4);
    CHECK(char_search(&g, 4, "a", BWD_ALL) == 2);
    CHECK(char_search(&g, 2, "a", BWD_ALL) == 0);
}

static void
test_char_search_backward_absent(void)
{
    init("foo\n");
    CHECK(char_search(&g, 3, "zzz", BWD_ALL) == -1);
}

/* ---- regex features ------------------------------------------------------ */

static void
test_regex_anchors(void)
{
    /* a0 b1 c2 \n3 x4 a5 b6 c7 \n8 */
    init("abc\nxabc\n");
    /* ^ anchors to the start of a line, so only the first "abc" matches. */
    CHECK(char_search(&g, 0, "^abc", FWD_ALL) == 0);
    CHECK(char_search(&g, 1, "^abc", FWD_ALL) == -1);
    /* $ anchors to the end of a line, so both do. */
    CHECK(char_search(&g, 0, "abc$", FWD_ALL) == 0);
    CHECK(char_search(&g, 1, "abc$", FWD_ALL) == 5);
}

static void
test_regex_classes_and_repetition(void)
{
    init("ab12cd\n");
    CHECK(char_search(&g, 0, "[0-9]+", FWD_ALL) == 2);
    CHECK(char_search(&g, 0, "[0-9][0-9]", FWD_ALL) == 2);
    CHECK(char_search(&g, 0, "a.*d", FWD_ALL) == 0);
    CHECK(char_search(&g, 0, "x*b", FWD_ALL) == 1);
}

static void
test_regex_alternation(void)
{
    init("hello world\n");
    CHECK(char_search(&g, 0, "world|hello", FWD_ALL) == 0);
    CHECK(char_search(&g, 1, "world|hello", FWD_ALL) == 6);
}

static void
test_regex_ignorecase(void)
{
    init("Hello\n");
    /* Case matters by default. */
    CHECK(char_search(&g, 0, "hello", FWD_ALL) == -1);
    /* With ignorecase set it does not. */
    g.setops |= VI_IGNORECASE;
    CHECK(char_search(&g, 0, "hello", FWD_ALL) == 0);
    CHECK(char_search(&g, 0, "HELLO", FWD_ALL) == 0);
}

static void
test_regex_bad_pattern(void)
{
    init("abc\n");
    /*
     * A pattern the engine refuses is reported and the search returns the
     * start position unchanged, rather than -1, so the caller leaves the
     * cursor where it was.
     */
    CHECK(char_search(&g, 2, "(unclosed", FWD_ALL) == 2);
    CHECK(status_calls == 1);
    CHECK(strstr(status_last, "bad search pattern") != NULL);
}

static void
test_regex_unclosed_bracket_is_accepted(void)
{
    /*
     * An unterminated '[' is not refused: the engine reads it as a
     * character set running to the end of the pattern.  So "[unclosed"
     * matches any of those letters, and 'c' at offset 2 is one of them.
     */
    init("abc\n");
    CHECK(char_search(&g, 2, "[unclosed", FWD_ALL) == 2);
    CHECK(status_calls == 0);
}

/*
 * NOT TESTED: a pattern consisting of a lone trailing backslash.
 *
 * regcomp() in src/regex.c steps past the backslash onto the terminating
 * NUL and then scans forward from it, so it reads past the end of the
 * pattern and never terminates.  A test for it would hang the suite, so
 * there is none.  The defect is in the vendored engine, which this branch
 * does not change, and it is reachable from the editor by typing / then a
 * single backslash.
 */

static void
test_regex_multibyte(void)
{
    /* The engine matches bytes, so a UTF-8 literal works as a literal. */
    init("caf\xc3\xa9 bar\n");
    CHECK(char_search(&g, 0, "caf\xc3\xa9", FWD_ALL) == 0);
    CHECK(char_search(&g, 0, "\xc3\xa9", FWD_ALL) == 3);
}

/* ---- search_run_cmd: / and ? -------------------------------------------- */

static int
run_search(const char *content, int dot, char op, const char *pat, int cnt)
{
    struct cmd_ctx ctx;

    init(content);
    g.dot    = dot;
    g.cmdcnt = cnt;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = op;
    ctx.str    = pat;
    ctx.count  = 1;
    ctx.rcount = 1;
    search_run_cmd(&g, &ctx);
    return g.dot;
}

static void
test_search_forward_command(void)
{
    /* f0 o1 o2 \n3 b4 a5 r6 \n7 f8 o9 o10 \n11 */
    CHECK(run_search("foo\nbar\nfoo\n", 0, '/', "foo", 0) == 8);
    /* The pattern is stored with its direction prefix for n and N. */
    CHECK(g.last_search_pattern != NULL);
    CHECK(strcmp(g.last_search_pattern, "/foo") == 0);
    CHECK(status_calls == 0);
}

static void
test_search_backward_command(void)
{
    CHECK(run_search("foo\nbar\nfoo\n", 8, '?', "foo", 0) == 0);
    CHECK(strcmp(g.last_search_pattern, "?foo") == 0);
}

static void
test_search_starts_past_the_cursor(void)
{
    /* A forward search begins one codepoint past dot, so the match the
     * cursor already sits on is not found again. */
    CHECK(run_search("foo foo\n", 0, '/', "foo", 0) == 4);
}

static void
test_search_wraps_forward(void)
{
    /* Nothing after the cursor: the search wraps to the top and says so. */
    CHECK(run_search("foo\nbar\n", 4, '/', "foo", 0) == 0);
    CHECK(status_calls == 1);
    CHECK(strstr(status_last, "BOTTOM") != NULL);
    CHECK(strstr(status_last, "TOP") != NULL);
}

static void
test_search_wraps_backward(void)
{
    CHECK(run_search("foo\nbar\n", 0, '?', "bar", 0) == 4);
    CHECK(status_calls == 1);
    CHECK(strstr(status_last, "TOP") != NULL);
}

static void
test_search_not_found(void)
{
    /* Not found anywhere: the cursor stays and the count is cleared so a
     * repeat count does not keep retrying. */
    CHECK(run_search("foo\nbar\n", 0, '/', "zzz", 3) == 0);
    CHECK(strstr(status_last, "Pattern not found") != NULL);
    /* The count is zeroed so the loop stops; the loop's own decrement
     * then leaves it at -1. */
    CHECK(g.cmdcnt == -1);
}

static void
test_search_with_count(void)
{
    /* 3/foo moves to the third following match. */
    CHECK(run_search("foo foo foo foo\n", 0, '/', "foo", 3) == 12);
}

static void
test_empty_pattern_reuses_the_last(void)
{
    struct cmd_ctx ctx;

    /* An empty pattern repeats the previous search, changing only the
     * direction. */
    run_search("foo\nbar\nfoo\n", 0, '/', "foo", 0);
    CHECK(strcmp(g.last_search_pattern, "/foo") == 0);

    g.dot    = 8;
    g.cmdcnt = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op  = '?';
    ctx.str = "";
    search_run_cmd(&g, &ctx);
    CHECK(strcmp(g.last_search_pattern, "?foo") == 0);
    CHECK(g.dot == 0);
}

/* ---- search_run_cmd: n and N -------------------------------------------- */

static void
test_repeat_search_same_direction(void)
{
    struct cmd_ctx ctx;

    run_search("foo\nbar\nfoo\nfoo\n", 0, '/', "foo", 0);
    CHECK(g.dot == 8);

    memset(&ctx, 0, sizeof(ctx));
    ctx.op   = 'n';
    g.cmdcnt = 0;
    search_run_cmd(&g, &ctx);
    CHECK(g.dot == 12);
}

static void
test_repeat_search_reverse_direction(void)
{
    struct cmd_ctx ctx;

    run_search("foo\nbar\nfoo\nfoo\n", 0, '/', "foo", 0);
    CHECK(g.dot == 8);

    /* N reverses the stored direction. */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op   = 'N';
    g.cmdcnt = 0;
    search_run_cmd(&g, &ctx);
    CHECK(g.dot == 0);
}

static void
test_repeat_after_backward_search(void)
{
    struct cmd_ctx ctx;

    run_search("foo\nbar\nfoo\nfoo\n", 12, '?', "foo", 0);
    CHECK(g.dot == 8);

    /* n after ? keeps going backwards. */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op   = 'n';
    g.cmdcnt = 0;
    search_run_cmd(&g, &ctx);
    CHECK(g.dot == 0);

    /* N after ? goes forwards. */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op   = 'N';
    g.cmdcnt = 0;
    search_run_cmd(&g, &ctx);
    CHECK(g.dot == 8);
}

static void
test_no_previous_search(void)
{
    struct cmd_ctx ctx;

    init("foo\n");
    /* n with nothing searched for yet.  The stored pattern is just the
     * direction byte, which search.c detects. */
    g.last_search_pattern = xstrdup("/");
    g.dot                 = 0;
    g.cmdcnt              = 0;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'n';
    search_run_cmd(&g, &ctx);
    CHECK(strstr(status_last, "No previous search") != NULL);
    CHECK(g.dot == 0);
}

/* ---- * and # ------------------------------------------------------------- */

static void
test_word_search_forward(void)
{
    /* f0 o1 o2 ' '3 b4 a5 r6 \n7 f8 o9 o10 \n11 */
    init("foo bar\nfoo\n");
    g.dot    = 1;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    /* The pattern is the word under the cursor, with word-boundary
     * anchors, and the search then runs as an 'n'. */
    CHECK(strcmp(g.last_search_pattern, "/\\<foo\\>") == 0);
    CHECK(g.dot == 8);
}

static void
test_word_search_backward(void)
{
    init("foo bar\nfoo\n");
    g.dot    = 9;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '#', 1);
    CHECK(strcmp(g.last_search_pattern, "?\\<foo\\>") == 0);
    CHECK(g.dot == 0);
}

static void
test_word_search_without_anchors(void)
{
    /* g* and g# drop the anchors, so a longer word containing the one
     * under the cursor also matches. */
    init("foo foobar\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 0);
    CHECK(strcmp(g.last_search_pattern, "/foo") == 0);
    CHECK(g.dot == 4);

    /* With anchors the same search finds nothing but itself, so it wraps. */
    init("foo foobar\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    CHECK(strcmp(g.last_search_pattern, "/\\<foo\\>") == 0);
    CHECK(g.dot == 0);
}

static void
test_word_search_finds_the_whole_word(void)
{
    /* The word extent is found from anywhere inside it. */
    init("alpha beta alpha\n");
    g.dot    = 3; /* inside "alpha" */
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    CHECK(strcmp(g.last_search_pattern, "/\\<alpha\\>") == 0);
    CHECK(g.dot == 11);
}

static void
test_word_search_underscore_and_digits(void)
{
    /* '_' and digits are word bytes, so they are part of the word. */
    init("a_1 x a_1\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    CHECK(strcmp(g.last_search_pattern, "/\\<a_1\\>") == 0);
    CHECK(g.dot == 6);
}

static void
test_word_search_not_on_a_word(void)
{
    init("  foo\n");
    g.dot    = 0; /* on a space */
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    CHECK(strstr(status_last, "No word under cursor") != NULL);
    CHECK(errors == 1);
    CHECK(g.dot == 0);

    /* On a newline there is no word either. */
    init("foo\n");
    g.dot    = 3;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    CHECK(errors == 1);
}

static void
test_word_search_multibyte_drops_anchors(void)
{
    /*
     * \< and \> only know ASCII word characters, so they would never
     * match a word starting with a multibyte character.  The anchors are
     * suppressed in that case rather than producing a pattern that
     * cannot match.
     */
    init("\xc3\xa9t\xc3\xa9 x \xc3\xa9t\xc3\xa9\n");
    g.dot    = 0;
    g.cmdcnt = 0;
    search_run_word_cmd(&g, '*', 1);
    CHECK(g.last_search_pattern != NULL);
    CHECK(strcmp(g.last_search_pattern, "/\xc3\xa9t\xc3\xa9") == 0);
    CHECK(g.dot == 8);
}

int
main(void)
{
    test_char_search_forward();
    test_char_search_forward_absent();
    test_char_search_scope_is_one_line();

    test_char_search_backward();
    test_char_search_backward_rightmost_on_a_line();
    test_char_search_backward_absent();

    test_regex_anchors();
    test_regex_classes_and_repetition();
    test_regex_alternation();
    test_regex_ignorecase();
    test_regex_bad_pattern();
    test_regex_unclosed_bracket_is_accepted();
    test_regex_multibyte();

    test_search_forward_command();
    test_search_backward_command();
    test_search_starts_past_the_cursor();
    test_search_wraps_forward();
    test_search_wraps_backward();
    test_search_not_found();
    test_search_with_count();
    test_empty_pattern_reuses_the_last();

    test_repeat_search_same_direction();
    test_repeat_search_reverse_direction();
    test_repeat_after_backward_search();
    test_no_previous_search();

    test_word_search_forward();
    test_word_search_backward();
    test_word_search_without_anchors();
    test_word_search_finds_the_whole_word();
    test_word_search_underscore_and_digits();
    test_word_search_not_on_a_word();
    test_word_search_multibyte_drops_anchors();

    SUMMARY();
}
