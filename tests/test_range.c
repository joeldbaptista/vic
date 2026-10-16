/*
 * test_range.c - unit tests for range.c
 *
 * range_find() turns an operator's motion key into a byte range plus a
 * buftype (PARTIAL, MULTI or WHOLE), or -1 when the motion failed.  The
 * motion itself is performed by do_cmd() in vic.c, which cannot be
 * linked here, so do_cmd is stubbed as a programmable move: the test says
 * where the cursor should end up and whether the motion failed, and the
 * test then checks range.c's own decisions — the buftype classification,
 * the p <= q ordering, and the per-key endpoint trimming.
 *
 * That split is deliberate.  Reimplementing vic.c's dispatch in the stub
 * would test vic.c, not range.c.
 *
 * textobj.c is linked for real, so the 'a' and 'i' paths resolve genuine
 * text objects; find_pair is copied in, as in test_textobj.c.
 */
#include "testutil.h"
#include "range.h"
#include "pt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

static struct editor g;
static int errors;

/* do_cmd stub: move dot to dc_target (when >= 0) and report dc_error. */
static int dc_calls;
static int dc_last_key;
static int dc_target;
static int dc_error;
static char dc_last_anchor;
static int dc_had_ctx;

void
do_cmd(struct editor *x, int c, const struct cmd_ctx *ctx)
{
    dc_calls++;
    dc_last_key = c;
    dc_had_ctx  = ctx != NULL;
    dc_last_anchor = ctx ? ctx->anchor : 0;
    if (dc_target >= 0)
        x->dot = dc_target;
    x->cmd_error = dc_error;
}

static int motion_key;  /* what get_motion_char returns */
static int one_char;    /* what get_one_char returns */

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

    if (n) {
        copy = malloc(n + 1);
        if (!copy)
            abort();
        memcpy(copy, content, n + 1);
    }
    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt      = ptnew(copy, n);
    g.tabstop = 8;
    errors         = 0;
    dc_calls       = 0;
    dc_last_key    = 0;
    dc_target      = -1;
    dc_error       = 0;
    dc_last_anchor = 0;
    dc_had_ctx     = 0;
    motion_key     = 0;
    one_char       = 0;
}

static int r_start, r_stop;

/* Resolve a range for operator `op` with range key `rg`, where the motion
 * leaves the cursor at `land` (-1 meaning "the motion does not move") and
 * reports `fail` through g->cmd_error. */
static int
range_f(const char *content, int dot, char op, char rg, int land, int fail)
{
    struct cmd_ctx ctx;

    init(content);
    g.dot     = dot;
    dc_target = land;
    dc_error  = fail;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op  = op;
    ctx.rg  = rg;
    r_start = r_stop = -9;
    return range_find(&g, &r_start, &r_stop, &ctx);
}

static int
range(const char *content, int dot, char op, char rg, int land)
{
    return range_f(content, dot, op, rg, land, 0);
}

/* ---- doubled operators: dd, cc, yy, Y ------------------------------------ */

static void
test_doubled_operator_is_whole_line(void)
{
    /* dd, cc, yy, >>, <<, !! all select whole lines. */
    CHECK(range("abc\ndef\n", 1, 'd', 'd', -1) == WHOLE);
    CHECK(range("abc\ndef\n", 1, 'c', 'c', -1) == WHOLE);
    CHECK(range("abc\ndef\n", 1, 'y', 'y', -1) == WHOLE);
    CHECK(range("abc\ndef\n", 1, '>', '>', -1) == WHOLE);
    CHECK(range("abc\ndef\n", 1, '<', '<', -1) == WHOLE);
    CHECK(range("abc\ndef\n", 1, '!', '!', -1) == WHOLE);
    /* The range is the single position, which the operator widens to the
     * whole line itself. */
    CHECK(r_start == 1 && r_stop == 1);
    /* No motion was needed. */
    CHECK(dc_calls == 0);
}

static void
test_Y_is_a_doubled_y(void)
{
    struct cmd_ctx ctx;

    init("abc\ndef\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'Y';
    /* Y carries no range key; range.c substitutes 'y'. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == WHOLE);
}

static void
test_doubled_operator_with_count(void)
{
    struct cmd_ctx ctx;

    init("abc\ndef\nghi\n");
    g.dot     = 0;
    g.cmdcnt  = 3;
    dc_target = 8;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'd';
    /* 3dd extends the range downwards with a 'j' motion. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == WHOLE);
    CHECK(dc_calls == 1);
    CHECK(dc_last_key == 'j');
    CHECK(r_start == 0 && r_stop == 8);
}

static void
test_doubled_operator_count_past_end(void)
{
    struct cmd_ctx ctx;

    init("abc\n");
    g.dot    = 0;
    g.cmdcnt = 5;
    dc_error = 1; /* the 'j' could not move */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'd';
    /* A count past the last line fails rather than deleting the wrong
     * range. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == -1);
    CHECK(errors == 1);
}

/* ---- characterwise motions ----------------------------------------------- */

static void
test_characterwise_motions(void)
{
    /* These keys give a PARTIAL (characterwise) range.  The list is
     * exhaustive over range.c's characterwise class, minus the search
     * keys, which are MULTI. */
    static const char keys[] = "^%$0bBeEfFtTh|{}";
    size_t i;

    for (i = 0; i < sizeof(keys) - 1; i++) {
        int bt = range("abcdef\n", 2, 'd', keys[i], 4);

        /* { and } reclassify themselves below; the rest stay PARTIAL. */
        if (keys[i] == '{' || keys[i] == '}')
            CHECK(bt == MULTI || bt == WHOLE);
        else
            CHECK(bt == PARTIAL);
    }
}

static void
test_search_motions_are_multi(void)
{
    static const char keys[] = "nN/?";
    size_t i;

    for (i = 0; i < sizeof(keys) - 1; i++)
        CHECK(range("abcdef\n", 2, 'd', keys[i], 4) == MULTI);
}

static void
test_motion_that_does_not_move_fails(void)
{
    /* A characterwise motion that leaves dot where it was is a failed
     * motion, so the operator must not run. */
    CHECK(range("abcdef\n", 2, 'd', '$', 2) == -1);
    CHECK(errors == 1);

    CHECK(range("abcdef\n", 2, 'd', 'b', 2) == -1);
    CHECK(errors == 1);
}

static void
test_forward_motion_range(void)
{
    /* d$ from 2 landing on 4: the range runs from the cursor to the
     * landing point, and a forward characterwise motion is not trimmed. */
    CHECK(range("abcdef\n", 2, 'd', '$', 4) == PARTIAL);
    CHECK(r_start == 2 && r_stop == 4);
}

static void
test_backward_motion_is_ordered_and_trimmed(void)
{
    /*
     * db from 4 landing on 1.  The range is normalised so start <= stop,
     * and the exclusive end is trimmed by one codepoint, because b leaves
     * the cursor on the first byte of the word it moved to and that byte
     * must not be deleted twice.
     */
    CHECK(range("abcdef\n", 4, 'd', 'b', 1) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 3);
}

static void
test_trimmed_keys(void)
{
    /* The complete set of keys whose endpoint is trimmed by one
     * codepoint: they all leave the cursor past the last byte the
     * operator should touch. */
    static const char keys[] = "^0bBFTh|";
    size_t i;

    for (i = 0; i < sizeof(keys) - 1; i++) {
        range("abcdef\n", 4, 'd', keys[i], 1);
        CHECK(r_start == 1);
        CHECK(r_stop == 3);
    }
}

static void
test_untrimmed_keys(void)
{
    /* These keys land on the last byte to operate on, so the endpoint
     * stands as it is. */
    static const char keys[] = "$eEft%";
    size_t i;

    for (i = 0; i < sizeof(keys) - 1; i++) {
        range("abcdef\n", 1, 'd', keys[i], 4);
        CHECK(r_start == 1);
        CHECK(r_stop == 4);
    }
}

static void
test_find_char_passes_the_anchor_through(void)
{
    struct cmd_ctx ctx;

    init("abcabc\n");
    g.dot     = 0;
    dc_target = 2;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'd';
    ctx.rg     = 'f';
    ctx.anchor = 'c';
    /* dfc must hand the target character to the motion, or the replay of
     * a '.' command would prompt for it again. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(dc_had_ctx);
    CHECK(dc_last_anchor == 'c');
}

/* ---- linewise motions ---------------------------------------------------- */

static void
test_linewise_motions(void)
{
    /* The complete linewise class. */
    static const char keys[] = "GHL+-gjk'\r\n";
    size_t i;

    for (i = 0; i < sizeof(keys) - 1; i++)
        CHECK(range("abc\ndef\nghi\n", 0, 'd', keys[i], 8) == WHOLE);
}

static void
test_linewise_motion_failure(void)
{
    /* dj on the last line: the motion sets cmd_error, so the range fails
     * and nothing is deleted. */
    CHECK(range_f("abc\n", 0, 'd', 'j', 0, 1) == -1);
    CHECK(errors == 1);
}

static void
test_linewise_mark_passes_the_anchor(void)
{
    struct cmd_ctx ctx;

    init("abc\ndef\n");
    g.dot     = 0;
    dc_target = 4;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'd';
    ctx.rg     = '\'';
    ctx.anchor = 'a';
    /* d'a needs the mark letter. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == WHOLE);
    CHECK(dc_had_ctx);
    CHECK(dc_last_anchor == 'a');
}

/* ---- l and Space --------------------------------------------------------- */

static void
test_l_and_space(void)
{
    /* dl from 1 with the motion landing on 2 moved exactly one codepoint,
     * which is the count, so the endpoint steps back: dl deletes one
     * character, not two. */
    CHECK(range("abcdef\n", 1, 'd', 'l', 2) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 1);

    CHECK(range("abcdef\n", 1, 'd', ' ', 2) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 1);
}

static void
test_l_with_count(void)
{
    struct cmd_ctx ctx;

    init("abcdef\n");
    g.dot     = 1;
    g.cmdcnt  = 3;
    dc_target = 4; /* three codepoints along */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'l';
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 3);
}

static void
test_l_short_of_the_count(void)
{
    struct cmd_ctx ctx;

    init("abc\n");
    g.dot     = 1;
    g.cmdcnt  = 5;
    dc_target = 3; /* the line end stopped it after two codepoints */
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'l';
    /* Fewer codepoints were crossed than asked for, so the endpoint is
     * not stepped back: d5l on a short line deletes to the line end. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 3);
}

/* ---- w and W ------------------------------------------------------------- */

static void
test_word_range_trims_the_landing_byte(void)
{
    /* f0 o1 o2 ' '3 b4 a5 r6 \n7.  dw from 0 with w landing on 4 must
     * delete "foo " and not the 'b'. */
    CHECK(range("foo bar\n", 0, 'd', 'w', 4) == MULTI);
    CHECK(r_start == 0 && r_stop == 3);
}

static void
test_word_range_drops_trailing_space_for_change(void)
{
    /*
     * cw behaves as ce: it changes the word without the whitespace that
     * follows it.  Note the asymmetry with dw above, which keeps it.
     */
    CHECK(range("foo bar\n", 0, 'c', 'w', 4) == MULTI);
    CHECK(r_start == 0 && r_stop == 2);
}

static void
test_word_range_at_end_of_buffer(void)
{
    /* f0 o1 o2 \n3.  dw on the last word of a line stops before the
     * newline: the trailing-whitespace loop walks the endpoint back off
     * the '\n', so the line break survives the delete. */
    CHECK(range("foo\n", 0, 'd', 'w', 3) == MULTI);
    CHECK(r_start == 0 && r_stop == 2);
}

static void
test_blank_word_range(void)
{
    /* a0 .1 b2 ' '3 c4 d5 \n6 — W treats "a.b" as one token. */
    CHECK(range("a.b cd\n", 0, 'd', 'W', 4) == MULTI);
    CHECK(r_start == 0 && r_stop == 3);
}

/* ---- paragraph motions: { and } ------------------------------------------ */

static void
test_paragraph_range_whole_lines(void)
{
    /* a0 b1 \n2 \n3 c4 d5 \n6.  From the start of a line to a blank
     * line is a whole-line range. */
    int bt = range("ab\n\ncd\n", 0, 'd', '}', 3);

    CHECK(bt == WHOLE);
    /* The blank line that ended the paragraph is not part of the range. */
    CHECK(r_start == 0 && r_stop == 2);
}

static void
test_paragraph_range_partial_line(void)
{
    /* Starting mid-line, the range cannot be whole lines. */
    int bt = range("ab\n\ncd\n", 1, 'd', '}', 3);

    CHECK(bt == MULTI);
    CHECK(r_start == 1);
}

/* ---- text objects -------------------------------------------------------- */

static void
test_text_object_from_ctx(void)
{
    struct cmd_ctx ctx;

    init("x (ab cd) y\n");
    g.dot = 5;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'd';
    ctx.rg     = 'i';
    ctx.anchor = '(';
    /* di( delegates to textobj.c, which is linked for real. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 3 && r_stop == 7);
    /* No motion is performed for a text object. */
    CHECK(dc_calls == 0);
}

static void
test_text_object_around(void)
{
    struct cmd_ctx ctx;

    init("x (ab cd) y\n");
    g.dot = 5;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'd';
    ctx.rg     = 'a';
    ctx.anchor = '(';
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 2 && r_stop == 8);
}

static void
test_text_object_word(void)
{
    struct cmd_ctx ctx;

    init("foo bar\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'd';
    ctx.rg     = 'i';
    ctx.anchor = 'w';
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 0 && r_stop == 2);
}

static void
test_text_object_failure(void)
{
    struct cmd_ctx ctx;

    init("abc\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op     = 'd';
    ctx.rg     = 'i';
    ctx.anchor = '(';
    /* No enclosing bracket: the range fails.  textobj.c raises the error
     * itself, so range.c does not raise a second one. */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == -1);
    CHECK(errors == 1);
}

static void
test_text_object_reads_the_object_interactively(void)
{
    struct cmd_ctx ctx;

    init("x (ab cd) y\n");
    g.dot    = 5;
    one_char = '(';
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'i'; /* no anchor, so the object comes from the keyboard */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 3 && r_stop == 7);
}

/* ---- the interactive fallback -------------------------------------------- */

static void
test_motion_key_from_terminal(void)
{
    struct cmd_ctx ctx;

    init("abcdef\n");
    g.dot      = 1;
    dc_target  = 4;
    motion_key = '$';
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd'; /* rg is 0, so the key is read interactively */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 4);
}

static void
test_null_ctx(void)
{
    init("abcdef\n");
    g.dot      = 1;
    dc_target  = 4;
    motion_key = '$';
    /* The '.' replay path passes no context at all. */
    CHECK(range_find(&g, &r_start, &r_stop, NULL) == PARTIAL);
    CHECK(r_start == 1 && r_stop == 4);
}

/* ---- abandoning the command --------------------------------------------- */

static void
test_escape_is_silent(void)
{
    struct cmd_ctx ctx;

    init("abcdef\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 27; /* ESC: the user gave up */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == -1);
    /* No beep for a deliberate cancel. */
    CHECK(errors == 0);
}

static void
test_unknown_range_key(void)
{
    struct cmd_ctx ctx;

    init("abcdef\n");
    g.dot = 1;
    memset(&ctx, 0, sizeof(ctx));
    ctx.op = 'd';
    ctx.rg = 'Z'; /* no motion by that name */
    CHECK(range_find(&g, &r_start, &r_stop, &ctx) == -1);
    CHECK(errors == 1);
    CHECK(dc_calls == 0);
}

/* ---- the range is always ordered ---------------------------------------- */

static void
test_range_is_always_ordered(void)
{
    static const char keys[] = "^%$0bBeEfFtTh|{}nN/?wWGHL+-gjk\r\n l";
    size_t i;
    int lands[] = {0, 1, 3, 5, 6};
    size_t li;

    for (i = 0; i < sizeof(keys) - 1; i++)
        for (li = 0; li < sizeof(lands) / sizeof(lands[0]); li++) {
            int bt = range("abc\ndef\n", 3, 'd', keys[i], lands[li]);

            if (bt == -1)
                continue;
            CHECK(r_start <= r_stop);
            CHECK(r_start >= 0);
            CHECK(r_stop < text_len(&g));
        }
}

int
main(void)
{
    test_doubled_operator_is_whole_line();
    test_Y_is_a_doubled_y();
    test_doubled_operator_with_count();
    test_doubled_operator_count_past_end();

    test_characterwise_motions();
    test_search_motions_are_multi();
    test_motion_that_does_not_move_fails();
    test_forward_motion_range();
    test_backward_motion_is_ordered_and_trimmed();
    test_trimmed_keys();
    test_untrimmed_keys();
    test_find_char_passes_the_anchor_through();

    test_linewise_motions();
    test_linewise_motion_failure();
    test_linewise_mark_passes_the_anchor();

    test_l_and_space();
    test_l_with_count();
    test_l_short_of_the_count();

    test_word_range_trims_the_landing_byte();
    test_word_range_drops_trailing_space_for_change();
    test_word_range_at_end_of_buffer();
    test_blank_word_range();

    test_paragraph_range_whole_lines();
    test_paragraph_range_partial_line();

    test_text_object_from_ctx();
    test_text_object_around();
    test_text_object_word();
    test_text_object_failure();
    test_text_object_reads_the_object_interactively();

    test_motion_key_from_terminal();
    test_null_ctx();

    test_escape_is_silent();
    test_unknown_range_key();

    test_range_is_always_ordered();

    SUMMARY();
}
