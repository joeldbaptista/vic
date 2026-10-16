/*
 * test_run.c - unit tests for run.c
 *
 * run.c is the `:run <command>` dispatch table and its built-in text
 * transformations.  Each command is handed a byte range and either
 * rewrites it or reports something on the status line, so the tests drive
 * run_dispatch and then check the buffer text or the captured message.
 *
 * buffer.c and undo.c are linked for real, so a transformation is also
 * checked by undoing it.  Eight symbols are stubbed: status_line (the
 * reporting commands have no other output), status_line_bold,
 * status_line_bold_errno, screen_erase, refresh, get_one_char,
 * reset_ydreg and showmatching.
 *
 * The fixture sets every mark to -1, as the editor does on load: trimsel
 * reads MARK_LT and MARK_GT and falls back to the ex range only when they
 * are unset, so a zeroed mark array would silently change its behaviour.
 */
#include "testutil.h"
#include "run.h"
#include "buffer.h"
#include "pt.h"
#include "text.h"
#include "undo.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

static struct editor g;
static char status_last[512];
static int status_calls;

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
    status_calls++;
    snprintf(status_last, sizeof(status_last), "%s", fn);
}

void
screen_erase(struct editor *x)
{
    (void)x;
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
    free(g.highlight_pattern);
    memset(&g, 0, sizeof(g));
    g.pt               = ptnew(copy, n);
    g.tabstop          = 8;
    g.columns          = 80;
    g.undo_queue_state = UNDO_EMPTY;
    for (i = 0; i < (int)(sizeof(g.mark) / sizeof(g.mark[0])); i++)
        g.mark[i] = -1;
    status_calls   = 0;
    status_last[0] = '\0';
}

static const char *
dump(void)
{
    static char buf[1024];
    int n = text_len(&g);

    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    text_read(&g, 0, n, buf);
    buf[n] = '\0';
    return buf;
}

/* Run a :run command over the whole buffer. */
static void
run(const char *content, const char *a0, const char *a1, const char *a2)
{
    char *argv[3];
    int argc = 1;

    init(content);
    argv[0] = (char *)a0;
    if (a1) {
        argv[1] = (char *)a1;
        argc    = 2;
    }
    if (a2) {
        argv[2] = (char *)a2;
        argc    = 3;
    }
    run_dispatch(&g, argc, argv, 0, text_len(&g) - 1);
}

/* ---- dispatch ------------------------------------------------------------ */

static void
test_unknown_command(void)
{
    run("abc\n", "nosuchthing", NULL, NULL);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(strstr(status_last, "unknown command") != NULL);
    CHECK(strstr(status_last, "nosuchthing") != NULL);
}

static void
test_no_arguments_is_a_no_op(void)
{
    char *argv[1];

    init("abc\n");
    argv[0] = (char *)"echo";
    /* argc 0 means nothing was typed after :run. */
    run_dispatch(&g, 0, argv, 0, text_len(&g) - 1);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(status_calls == 0);
}

/* ---- upper and lower ----------------------------------------------------- */

static void
test_upper_lower(void)
{
    run("hello World\n", "upper", NULL, NULL);
    CHECK(strcmp(dump(), "HELLO WORLD\n") == 0);

    run("Hello World\n", "lower", NULL, NULL);
    CHECK(strcmp(dump(), "hello world\n") == 0);

    /* Digits and punctuation are untouched. */
    run("a1-B2\n", "upper", NULL, NULL);
    CHECK(strcmp(dump(), "A1-B2\n") == 0);
}

static void
test_upper_is_undoable(void)
{
    run("hello\n", "upper", NULL, NULL);
    CHECK(strcmp(dump(), "HELLO\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "hello\n") == 0);
}

static void
test_upper_over_a_sub_range(void)
{
    char *argv[1];

    /* a0 b1 c2 \n3 d4 e5 f6 \n7 — only the second line. */
    init("abc\ndef\n");
    argv[0] = (char *)"upper";
    run_dispatch(&g, 1, argv, 4, 7);
    CHECK(strcmp(dump(), "abc\nDEF\n") == 0);
}

/* ---- trim and trimsel ---------------------------------------------------- */

static void
test_trim(void)
{
    /* Trailing blanks go from every line; the text itself stays. */
    run("a  \nb\t\n", "trim", NULL, NULL);
    CHECK(strcmp(dump(), "a\nb\n") == 0);

    /* Leading blanks are left alone: that is what trimsel is for. */
    run("  a  \n", "trim", NULL, NULL);
    CHECK(strcmp(dump(), "  a\n") == 0);
}

static void
test_trimsel_both_ends(void)
{
    /* With no visual marks set, trimsel falls back to the ex range. */
    run("  ab  \n", "trimsel", NULL, NULL);
    CHECK(strcmp(dump(), "ab") == 0);

    run("  ab  \n", "trimsel", "0", NULL);
    CHECK(strcmp(dump(), "ab") == 0);
}

static void
test_trimsel_one_end(void)
{
    /* 1 strips the start only. */
    run("  ab  \n", "trimsel", "1", NULL);
    CHECK(strcmp(dump(), "ab  \n") == 0);

    /* 2 strips the end only. */
    run("  ab  \n", "trimsel", "2", NULL);
    CHECK(strcmp(dump(), "  ab") == 0);
}

static void
test_trimsel_uses_the_visual_marks(void)
{
    char *argv[1];

    /* x0 ' '1 ' '2 a3 b4 ' '5 y6 \n7 — the selection is [1,5]. */
    init("x  ab y\n");
    g.mark[MARK_LT] = 1;
    g.mark[MARK_GT] = 5;
    argv[0]         = (char *)"trimsel";
    run_dispatch(&g, 1, argv, 0, text_len(&g) - 1);
    /* Only the selected bytes are considered, so the x and y survive. */
    CHECK(strcmp(dump(), "xaby\n") == 0);
}

static void
test_trimsel_all_blanks(void)
{
    char *argv[1];

    init("x    y\n");
    g.mark[MARK_LT] = 1;
    g.mark[MARK_GT] = 4;
    argv[0]         = (char *)"trimsel";
    run_dispatch(&g, 1, argv, 0, text_len(&g) - 1);
    /* A selection of nothing but blanks is deleted outright. */
    CHECK(strcmp(dump(), "xy\n") == 0);
}

/* ---- uniq and sort ------------------------------------------------------- */

static void
test_uniq(void)
{
    /* Only *consecutive* duplicates go, like the uniq tool. */
    run("a\na\nb\na\n", "uniq", NULL, NULL);
    CHECK(strcmp(dump(), "a\nb\na\n") == 0);

    run("a\nb\nc\n", "uniq", NULL, NULL);
    CHECK(strcmp(dump(), "a\nb\nc\n") == 0);

    run("x\nx\nx\n", "uniq", NULL, NULL);
    CHECK(strcmp(dump(), "x\n") == 0);
}

static void
test_sort(void)
{
    run("c\na\nb\n", "sort", NULL, NULL);
    CHECK(strcmp(dump(), "a\nb\nc\n") == 0);

    /* -r reverses the order. */
    run("c\na\nb\n", "sort", "-r", NULL);
    CHECK(strcmp(dump(), "c\nb\na\n") == 0);
}

static void
test_sort_is_undoable(void)
{
    run("c\na\nb\n", "sort", NULL, NULL);
    CHECK(strcmp(dump(), "a\nb\nc\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "c\na\nb\n") == 0);
}

static void
test_sort_one_line(void)
{
    run("only\n", "sort", NULL, NULL);
    CHECK(strcmp(dump(), "only\n") == 0);
}

/* ---- wrap, number, deindent, align -------------------------------------- */

static void
test_wrap(void)
{
    run("aaa bbb ccc ddd\n", "wrap", "7", NULL);
    CHECK(strcmp(dump(), "aaa bbb\nccc ddd\n") == 0);
}

static void
test_wrap_no_break_needed(void)
{
    run("short\n", "wrap", "40", NULL);
    CHECK(strcmp(dump(), "short\n") == 0);
}

static void
test_number(void)
{
    run("x\ny\n", "number", NULL, NULL);
    CHECK(strcmp(dump(), "1. x\n2. y\n") == 0);
}

static void
test_deindent(void)
{
    /* One level by default. */
    run("\t\tx\n", "deindent", NULL, NULL);
    CHECK(strcmp(dump(), "\tx\n") == 0);

    /* A count removes that many levels. */
    run("\t\tx\n", "deindent", "2", NULL);
    CHECK(strcmp(dump(), "x\n") == 0);

    /* Nothing to remove: unchanged. */
    run("x\n", "deindent", NULL, NULL);
    CHECK(strcmp(dump(), "x\n") == 0);
}

static void
test_align(void)
{
    /* Each line is padded so its first '=' lines up. */
    run("a=1\nbb=2\n", "align", "=", NULL);
    CHECK(strcmp(dump(), "a =1\nbb=2\n") == 0);
}

static void
test_align_missing_character(void)
{
    /* A line without the character is left as it is. */
    run("a=1\nplain\n", "align", "=", NULL);
    CHECK(strstr(dump(), "plain") != NULL);
}

/* ---- url encoding -------------------------------------------------------- */

static void
test_urlencode(void)
{
    run("a b/c\n", "urlencode", NULL, NULL);
    /* Space, slash and the newline are all percent-encoded. */
    CHECK(strcmp(dump(), "a%20b%2Fc%0A") == 0);
}

static void
test_urlencode_safe_characters(void)
{
    run("aA0-_.~", "urlencode", NULL, NULL);
    /* The unreserved set passes through unchanged. */
    CHECK(strcmp(dump(), "aA0-_.~") == 0);
}

static void
test_urldecode(void)
{
    run("a%20b%2Fc\n", "urldecode", NULL, NULL);
    CHECK(strcmp(dump(), "a b/c\n") == 0);
}

static void
test_url_round_trip(void)
{
    run("a b&c=d?e\n", "urlencode", NULL, NULL);
    {
        char *argv[1];

        argv[0] = (char *)"urldecode";
        run_dispatch(&g, 1, argv, 0, text_len(&g) - 1);
    }
    CHECK(strcmp(dump(), "a b&c=d?e\n") == 0);
}

/* ---- base64 -------------------------------------------------------------- */

static void
test_base64enc_exact_multiple(void)
{
    /* An input whose length is a multiple of three needs no padding and
     * is encoded correctly. */
    run("abc", "base64enc", NULL, NULL);
    CHECK(strcmp(dump(), "YWJj\n") == 0);

    run("abcdef", "base64enc", NULL, NULL);
    CHECK(strcmp(dump(), "YWJjZGVm\n") == 0);
}

static void
test_base64enc_padding_is_wrong(void)
{
    /*
     * DEFECT, pinned rather than endorsed.  When the input length is not
     * a multiple of three, the encoder must pad the output with '='.  It
     * emits 'A' (the digit for a zero sextet) instead, because the `take`
     * calculation tests how many bytes have been consumed in total rather
     * than how many this group had.  The result is not valid base64: an
     * external decoder reads trailing NUL bytes that were never in the
     * buffer.
     *
     * Correct answers are in the comments.  The defect is present on main
     * and is not introduced by the piece-tree port.
     */
    run("a", "base64enc", NULL, NULL);
    CHECK(strcmp(dump(), "YQAA\n") == 0); /* should be "YQ==" */

    run("ab", "base64enc", NULL, NULL);
    CHECK(strcmp(dump(), "YWIA\n") == 0); /* should be "YWI=" */

    run("abcd", "base64enc", NULL, NULL);
    CHECK(strcmp(dump(), "YWJjZAAA\n") == 0); /* should be "YWJjZA==" */

    run("abcde", "base64enc", NULL, NULL);
    CHECK(strcmp(dump(), "YWJjZGUA\n") == 0); /* should be "YWJjZGU=" */
}

static void
test_base64dec(void)
{
    run("YWJj", "base64dec", NULL, NULL);
    CHECK(strcmp(dump(), "abc") == 0);

    /* Properly padded input decodes correctly. */
    run("YWJjZA==", "base64dec", NULL, NULL);
    CHECK(strcmp(dump(), "abcd") == 0);
}

static void
test_base64_round_trip(void)
{
    /* The encoder's own output still decodes back to the original here,
     * because the decoder drops the trailing zero bytes.  The round trip
     * working is why the padding defect has gone unnoticed. */
    run("abcd", "base64enc", NULL, NULL);
    {
        char *argv[1];

        argv[0] = (char *)"base64dec";
        run_dispatch(&g, 1, argv, 0, text_len(&g) - 1);
    }
    CHECK(strcmp(dump(), "abcd") == 0);
}

/* ---- JSON escaping ------------------------------------------------------- */

static void
test_jsonesc(void)
{
    run("a\"b\\c\n", "jsonesc", NULL, NULL);
    /* Quote, backslash and newline become their two-character escapes. */
    CHECK(strcmp(dump(), "a\\\"b\\\\c\\n") == 0);
}

static void
test_jsonunesc(void)
{
    run("a\\\"b\\\\c", "jsonunesc", NULL, NULL);
    CHECK(strcmp(dump(), "a\"b\\c") == 0);
}

static void
test_json_round_trip(void)
{
    run("a\"b\\c", "jsonesc", NULL, NULL);
    {
        char *argv[1];

        argv[0] = (char *)"jsonunesc";
        run_dispatch(&g, 1, argv, 0, text_len(&g) - 1);
    }
    CHECK(strcmp(dump(), "a\"b\\c") == 0);
}

/* ---- reporting commands -------------------------------------------------- */

static void
test_echo(void)
{
    run("x\n", "echo", "hello", "world");
    /* Arguments are joined with single spaces. */
    CHECK(strcmp(status_last, "hello world") == 0);
    /* The buffer is not touched. */
    CHECK(strcmp(dump(), "x\n") == 0);
}

static void
test_echo_no_arguments(void)
{
    run("x\n", "echo", NULL, NULL);
    CHECK(strcmp(status_last, "") == 0);
}

static void
test_wc(void)
{
    /* o0..o6 "one two\n", t8.. "three\n" — 2 newlines, 3 words, 14 bytes. */
    run("one two\nthree\n", "wc", NULL, NULL);
    CHECK(strstr(status_last, "2 lines") != NULL);
    CHECK(strstr(status_last, "3 words") != NULL);
    CHECK(strstr(status_last, "14 bytes") != NULL);
}

static void
test_wc_tabs_separate_words(void)
{
    run("a\tb c\n", "wc", NULL, NULL);
    CHECK(strstr(status_last, "3 words") != NULL);
}

static void
test_col(void)
{
    run("ab\nabcd\n", "col", NULL, NULL);
    CHECK(strstr(status_last, "4") != NULL);
    CHECK(strcmp(dump(), "ab\nabcd\n") == 0);
}

static void
test_freq(void)
{
    run("aa bb aa cc aa bb\n", "freq", NULL, NULL);
    /* The three distinct words, most frequent first. */
    CHECK(strstr(status_last, "3 unique") != NULL);
    CHECK(strstr(status_last, "aa") != NULL);
    CHECK(strstr(status_last, "bb") != NULL);
    CHECK(strcmp(dump(), "aa bb aa cc aa bb\n") == 0);
}

static void
test_hash(void)
{
    run("abc\n", "hash", NULL, NULL);
    CHECK(strstr(status_last, "hash:") != NULL);
    /* The length reported is the range length, newline included. */
    CHECK(strstr(status_last, "len=4") != NULL);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_hash_is_stable(void)
{
    char first[512];

    run("abc\n", "hash", NULL, NULL);
    snprintf(first, sizeof(first), "%s", status_last);
    run("abc\n", "hash", NULL, NULL);
    /* The same bytes must hash to the same value, or :run hash replace
     * would not be reproducible. */
    CHECK(strcmp(first, status_last) == 0);
}

static void
test_hash_mod(void)
{
    run("foo bar\n", "hash", "mod", "4");
    CHECK(strstr(status_last, "2 identifiers") != NULL);
    CHECK(strcmp(dump(), "foo bar\n") == 0);
}

static void
test_hash_replace(void)
{
    run("foo bar\n", "hash", "replace", NULL);
    /* Each identifier is replaced by its hash literal, so no letters are
     * left. */
    CHECK(strstr(dump(), "foo") == NULL);
    CHECK(strstr(dump(), "bar") == NULL);
    CHECK(strlen(dump()) > 0);
}

/* ---- convert ------------------------------------------------------------- */

static void
test_convert_snake_to_camel(void)
{
    run("my_var\n", "convert", "s2c", NULL);
    CHECK(strcmp(dump(), "myVar\n") == 0);
    CHECK(strstr(status_last, "1 identifier changed") != NULL);
}

static void
test_convert_snake_to_upper_camel(void)
{
    run("my_var\n", "convert", "s2uc", NULL);
    CHECK(strcmp(dump(), "MyVar\n") == 0);
}

static void
test_convert_camel_to_snake(void)
{
    run("myVar\n", "convert", "c2s", NULL);
    CHECK(strcmp(dump(), "my_var\n") == 0);
}

static void
test_convert_nothing_to_do(void)
{
    run("x\n", "convert", "c2s", NULL);
    CHECK(strcmp(dump(), "x\n") == 0);
    CHECK(strstr(status_last, "nothing to change") != NULL);
}

static void
test_convert_round_trip(void)
{
    run("my_long_name\n", "convert", "s2c", NULL);
    CHECK(strcmp(dump(), "myLongName\n") == 0);
    {
        char *argv[2];

        argv[0] = (char *)"convert";
        argv[1] = (char *)"c2s";
        run_dispatch(&g, 2, argv, 0, text_len(&g) - 1);
    }
    CHECK(strcmp(dump(), "my_long_name\n") == 0);
}

/* ---- highlight and color-escape ------------------------------------------ */

static void
test_highlight_set_and_clear(void)
{
    run("x\n", "highlight", "pat", NULL);
    CHECK(g.highlight_pattern != NULL);
    CHECK(strcmp(g.highlight_pattern, "pat") == 0);

    {
        char *argv[1];

        argv[0] = (char *)"highlight";
        run_dispatch(&g, 1, argv, 0, text_len(&g) - 1);
    }
    /* No argument clears it. */
    CHECK(g.highlight_pattern == NULL);
}

static void
test_color_escape(void)
{
    run("x\n", "color-escape", "on", NULL);
    CHECK(strstr(status_last, "on") != NULL);

    {
        char *argv[2];

        argv[0] = (char *)"color-escape";
        argv[1] = (char *)"off";
        run_dispatch(&g, 2, argv, 0, text_len(&g) - 1);
    }
    CHECK(strstr(status_last, "off") != NULL);
}

/* ---- empty and degenerate ranges ---------------------------------------- */

static void
test_commands_survive_an_empty_buffer(void)
{
    static const char *cmds[] = {"upper",     "lower",   "trim",   "uniq",
                                 "sort",      "number",  "deindent",
                                 "urlencode", "jsonesc", "wc",     "col",
                                 "freq",      "hash"};
    size_t i;

    /* An empty buffer gives the range [0, -1].  No command may crash or
     * corrupt the buffer on it. */
    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        char *argv[1];

        init("");
        argv[0] = (char *)cmds[i];
        run_dispatch(&g, 1, argv, 0, -1);
        CHECK(text_len(&g) >= 0);
        CHECK(ptcheck(g.pt) == 1);
    }
}

static void
test_commands_survive_a_one_byte_buffer(void)
{
    static const char *cmds[] = {"upper", "lower",     "trim",    "uniq",
                                 "sort",  "number",    "deindent",
                                 "urlencode", "urldecode", "base64enc",
                                 "jsonesc", "jsonunesc", "wc",    "col",
                                 "freq",  "hash"};
    size_t i;

    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        char *argv[1];

        init("\n");
        argv[0] = (char *)cmds[i];
        run_dispatch(&g, 1, argv, 0, 0);
        CHECK(ptcheck(g.pt) == 1);
    }
}

int
main(void)
{
    test_unknown_command();
    test_no_arguments_is_a_no_op();

    test_upper_lower();
    test_upper_is_undoable();
    test_upper_over_a_sub_range();

    test_trim();
    test_trimsel_both_ends();
    test_trimsel_one_end();
    test_trimsel_uses_the_visual_marks();
    test_trimsel_all_blanks();

    test_uniq();
    test_sort();
    test_sort_is_undoable();
    test_sort_one_line();

    test_wrap();
    test_wrap_no_break_needed();
    test_number();
    test_deindent();
    test_align();
    test_align_missing_character();

    test_urlencode();
    test_urlencode_safe_characters();
    test_urldecode();
    test_url_round_trip();

    test_base64enc_exact_multiple();
    test_base64enc_padding_is_wrong();
    test_base64dec();
    test_base64_round_trip();

    test_jsonesc();
    test_jsonunesc();
    test_json_round_trip();

    test_echo();
    test_echo_no_arguments();
    test_wc();
    test_wc_tabs_separate_words();
    test_col();
    test_freq();
    test_hash();
    test_hash_is_stable();
    test_hash_mod();
    test_hash_replace();

    test_convert_snake_to_camel();
    test_convert_snake_to_upper_camel();
    test_convert_camel_to_snake();
    test_convert_nothing_to_do();
    test_convert_round_trip();

    test_highlight_set_and_clear();
    test_color_escape();

    test_commands_survive_an_empty_buffer();
    test_commands_survive_a_one_byte_buffer();

    SUMMARY();
}
