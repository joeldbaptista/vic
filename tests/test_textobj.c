/*
 * test_textobj.c - unit tests for textobj.c
 *
 * textobj_find_range() resolves the ranges behind iw/aw, i(/a(, i"/a"
 * and it/at.  It reads the text, g->dot and g->cmdcnt, and writes a
 * start/stop pair plus a buftype.  Two symbols have to be supplied here:
 *
 *   indicate_error  — counted, so the tests can check the error beep.
 *   find_pair       — a verbatim copy of the function in vic.c, which
 *                     cannot be linked because vic.c drags in the whole
 *                     editor.  Keep the copy in step with the original.
 *
 * Ranges are inclusive at both ends: [start, stop].  buftype is PARTIAL
 * when the range stays inside one line and MULTI when it crosses a '\n'.
 */
#include "testutil.h"
#include "textobj.h"
#include "pt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

static struct editor g;
static int errors; /* indicate_error() call count */

void
indicate_error(struct editor *x)
{
    (void)x;
    errors++;
}

/* Verbatim from vic.c; see the file comment above. */
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

static int r_start, r_stop, r_buftype;

/* Resolve one text object; returns textobj_find_range's own result. */
static int
obj_at(const char *content, int dot, int cnt, int ai, int obj)
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
    g.dot     = dot;
    g.cmdcnt  = cnt;
    errors    = 0;
    r_start = r_stop = r_buftype = -9;
    return textobj_find_range(&g, ai, obj, &r_start, &r_stop, &r_buftype);
}

/* True when the last resolved range covers exactly the bytes of `want`. */
static int
spans(const char *want)
{
    char buf[128];
    int n = r_stop - r_start + 1;

    if (r_start < 0 || n <= 0 || n > (int)sizeof(buf) - 1)
        return 0;
    text_read(&g, r_start, n, buf);
    buf[n] = '\0';
    return strcmp(buf, want) == 0;
}

/* ---- delimited pairs ----------------------------------------------------- */

static void
test_paren_inner(void)
{
    /* x0 ' '1 (2 a3 b4 ' '5 c6 d7 )8 ' '9 y10 \n11 */
    const char *t = "x (ab cd) y\n";
    int p;

    /* From anywhere between the brackets, including on them. */
    for (p = 2; p <= 8; p++) {
        CHECK(obj_at(t, p, 0, 'i', '(') == 0);
        CHECK(r_start == 3 && r_stop == 7);
        CHECK(spans("ab cd"));
        CHECK(r_buftype == PARTIAL);
    }
}

static void
test_paren_around(void)
{
    const char *t = "x (ab cd) y\n";

    /* "around" takes the brackets in too. */
    CHECK(obj_at(t, 5, 0, 'a', '(') == 0);
    CHECK(r_start == 2 && r_stop == 8);
    CHECK(spans("(ab cd)"));
}

static void
test_paren_outside(void)
{
    const char *t = "x (ab cd) y\n";

    /* Outside the brackets there is no object, and the error beep fires. */
    CHECK(obj_at(t, 0, 0, 'i', '(') == -1);
    CHECK(errors == 1);
    CHECK(obj_at(t, 10, 0, 'i', '(') == -1);
    CHECK(errors == 1);
}

static void
test_every_bracket_pair(void)
{
    /* Either half of a pair names the same object, so i) behaves as i(. */
    CHECK(obj_at("x (ab) y\n", 3, 0, 'i', '(') == 0 && spans("ab"));
    CHECK(obj_at("x (ab) y\n", 3, 0, 'i', ')') == 0 && spans("ab"));
    CHECK(obj_at("x [ab] y\n", 3, 0, 'i', '[') == 0 && spans("ab"));
    CHECK(obj_at("x [ab] y\n", 3, 0, 'i', ']') == 0 && spans("ab"));
    CHECK(obj_at("x {ab} y\n", 3, 0, 'i', '{') == 0 && spans("ab"));
    CHECK(obj_at("x {ab} y\n", 3, 0, 'i', '}') == 0 && spans("ab"));
    CHECK(obj_at("x <ab> y\n", 3, 0, 'i', '<') == 0 && spans("ab"));
    CHECK(obj_at("x <ab> y\n", 3, 0, 'i', '>') == 0 && spans("ab"));
}

static void
test_paren_nesting(void)
{
    /* a0 ' '1 (2 b3 ' '4 (5 c6 ' '7 d8 )9 ' '10 e11 )12 ' '13 f14 \n15 */
    const char *t = "a (b (c d) e) f\n";

    /* The innermost pair wins. */
    CHECK(obj_at(t, 7, 0, 'i', '(') == 0 && spans("c d"));
    CHECK(obj_at(t, 7, 0, 'a', '(') == 0 && spans("(c d)"));
}

static void
test_paren_count_steps_out(void)
{
    const char *t = "a (b (c d) e) f\n";

    /* A count climbs that many nesting levels. */
    CHECK(obj_at(t, 7, 1, 'i', '(') == 0 && spans("c d"));
    CHECK(obj_at(t, 7, 2, 'i', '(') == 0 && spans("b (c d) e"));
    CHECK(obj_at(t, 7, 2, 'a', '(') == 0 && spans("(b (c d) e)"));
    /* Past the outermost pair the object fails. */
    CHECK(obj_at(t, 7, 3, 'i', '(') == -1);
}

static void
test_paren_unbalanced(void)
{
    /* An opener with no closer, and a closer with no opener. */
    CHECK(obj_at("a (b c\n", 3, 0, 'i', '(') == -1);
    CHECK(obj_at("a )b\n", 2, 0, 'i', '(') == -1);
}

static void
test_paren_empty(void)
{
    /* "()" has no inner bytes, so the inner object fails while the
     * around object still selects the two brackets. */
    CHECK(obj_at("a ()b\n", 2, 0, 'i', '(') == -1);
    CHECK(obj_at("a ()b\n", 2, 0, 'a', '(') == 0 && spans("()"));
}

static void
test_paren_multiline(void)
{
    /* {0 \n1 l2 i3 n4 e5 \n6 }7 \n8 — the range crosses a newline, so
     * buftype is MULTI rather than PARTIAL. */
    const char *t = "{\nline\n}\n";

    CHECK(obj_at(t, 3, 0, 'i', '{') == 0);
    CHECK(spans("\nline\n"));
    CHECK(r_buftype == MULTI);

    CHECK(obj_at(t, 3, 0, 'a', '{') == 0);
    CHECK(spans("{\nline\n}"));
    CHECK(r_buftype == MULTI);
}

/* ---- quotes -------------------------------------------------------------- */

static void
test_quote_inner(void)
{
    /* a0 ' '1 "2 h3 i4 ' '5 t6 h7 e8 r9 e10 "11 ' '12 b13 \n14 */
    const char *t = "a \"hi there\" b\n";
    int p;

    /* The opening quote counts as inside the string; the closing one
     * does not.  That asymmetry is current behaviour, pinned here. */
    for (p = 2; p <= 10; p++) {
        CHECK(obj_at(t, p, 0, 'i', '"') == 0);
        CHECK(spans("hi there"));
        CHECK(r_buftype == PARTIAL);
    }
    CHECK(obj_at(t, 11, 0, 'i', '"') == -1);
}

static void
test_quote_around(void)
{
    CHECK(obj_at("a \"hi there\" b\n", 5, 0, 'a', '"') == 0);
    CHECK(spans("\"hi there\""));
}

static void
test_quote_kinds(void)
{
    /* All three quote characters are their own pair. */
    CHECK(obj_at("a `b c` d\n", 3, 0, 'i', '`') == 0 && spans("b c"));
    CHECK(obj_at("a 'b c' d\n", 3, 0, 'i', '\'') == 0 && spans("b c"));
    CHECK(obj_at("a \"b c\" d\n", 3, 0, 'i', '"') == 0 && spans("b c"));
}

static void
test_quote_escaped(void)
{
    /* a0 ' '1 "2 x3 \4 "5 y6 "7 ' '8 b9 \n10 — the quote at 5 is escaped,
     * so the string runs to the quote at 7 and not to that one. */
    const char *t = "a \"x\\\"y\" b\n";

    CHECK(obj_at(t, 4, 0, 'i', '"') == 0);
    CHECK(spans("x\\\"y"));
    CHECK(obj_at(t, 2, 0, 'i', '"') == 0);
    CHECK(spans("x\\\"y"));
}

static void
test_quote_unterminated(void)
{
    CHECK(obj_at("a \"b\n", 3, 0, 'i', '"') == -1);
    CHECK(errors == 1);
}

static void
test_quote_empty(void)
{
    /* Nothing between the quotes, so the inner object fails. */
    CHECK(obj_at("a \"\"b\n", 2, 0, 'i', '"') == -1);
}

static void
test_quote_outside(void)
{
    CHECK(obj_at("a \"hi\" b\n", 0, 0, 'i', '"') == -1);
    CHECK(obj_at("a \"hi\" b\n", 7, 0, 'i', '"') == -1);
}

/* ---- words --------------------------------------------------------------- */

static void
test_word_inner(void)
{
    /* f0 o1 o2 ' '3 b4 a5 r6 ' '7 ' '8 b9 a10 z11 \n12 */
    const char *t = "foo bar  baz\n";

    CHECK(obj_at(t, 1, 0, 'i', 'w') == 0 && spans("foo"));
    CHECK(obj_at(t, 5, 0, 'i', 'w') == 0 && spans("bar"));
    CHECK(obj_at(t, 10, 0, 'i', 'w') == 0 && spans("baz"));
    CHECK(r_buftype == PARTIAL);
}

static void
test_word_around_takes_trailing_space(void)
{
    const char *t = "foo bar  baz\n";

    /* "around" adds the whitespace that follows the word. */
    CHECK(obj_at(t, 1, 0, 'a', 'w') == 0 && spans("foo "));
    CHECK(obj_at(t, 5, 0, 'a', 'w') == 0 && spans("bar  "));
    /* The last word has no trailing space, so the leading run is taken
     * instead, which is what vi does. */
    CHECK(obj_at(t, 10, 0, 'a', 'w') == 0 && spans("  baz"));
}

static void
test_word_from_whitespace(void)
{
    const char *t = "foo bar  baz\n";

    /* Sitting on whitespace selects the next word. */
    CHECK(obj_at(t, 3, 0, 'i', 'w') == 0 && spans("bar"));
    CHECK(obj_at(t, 7, 0, 'i', 'w') == 0 && spans("baz"));
}

static void
test_word_punctuation(void)
{
    /* a0 .1 b2 ' '3 c4 d5 \n6 — lower case splits on punctuation. */
    const char *t = "a.b cd\n";

    CHECK(obj_at(t, 0, 0, 'i', 'w') == 0 && spans("a"));
    CHECK(obj_at(t, 1, 0, 'i', 'w') == 0 && spans("."));
    CHECK(obj_at(t, 2, 0, 'i', 'w') == 0 && spans("b"));
    /* Upper case takes the whole non-whitespace run. */
    CHECK(obj_at(t, 0, 0, 'i', 'W') == 0 && spans("a.b"));
    CHECK(obj_at(t, 1, 0, 'i', 'W') == 0 && spans("a.b"));
    CHECK(obj_at(t, 2, 0, 'i', 'W') == 0 && spans("a.b"));
}

static void
test_word_count(void)
{
    /* a0 a1 ' '2 b3 b4 ' '5 c6 c7 ' '8 d9 d10 \n11 */
    const char *t = "aa bb cc dd\n";

    /* A count extends the object over that many words, starting from the
     * one after dot. */
    CHECK(obj_at(t, 0, 1, 'i', 'w') == 0 && spans("aa"));
    CHECK(obj_at(t, 0, 2, 'i', 'w') == 0 && spans("bb"));
    CHECK(obj_at(t, 0, 3, 'i', 'w') == 0 && spans("cc"));
    CHECK(obj_at(t, 0, 2, 'a', 'w') == 0 && spans("bb "));
}

static void
test_word_on_blank_line(void)
{
    /* A line holding only whitespace has no word object. */
    CHECK(obj_at("   \n", 1, 0, 'i', 'w') == -1);
    CHECK(obj_at("\n", 0, 0, 'i', 'w') == -1);
}

static void
test_word_on_empty_line_between_words(void)
{
    /*
     * a0 b1 \n2 \n3 c4 d5 \n6 — dot on the empty line's '\n'.  The object
     * resolves backwards to the word on the line above rather than to the
     * empty line itself.  Pinned as current behaviour; vi selects the
     * empty line here.
     */
    CHECK(obj_at("ab\n\ncd\n", 3, 0, 'i', 'w') == 0);
    CHECK(spans("ab"));
}

static void
test_word_multibyte(void)
{
    /* "foéo bar": the accented character is part of the word, and stop
     * must land on its last byte, never inside it. */
    const char *t = "fo\xc3\xa9o bar\n";

    CHECK(obj_at(t, 0, 0, 'i', 'w') == 0);
    CHECK(spans("fo\xc3\xa9o"));
    CHECK(r_start == 0 && r_stop == 4);
}

/* ---- tags ---------------------------------------------------------------- */

static void
test_tag_inner(void)
{
    /* <0 a1 >2 <3 b4 >5 h6 i7 <8 /9 b10 >11 <12 /13 a14 >15 \n16 */
    const char *t = "<a><b>hi</b></a>\n";
    int p;

    /* Inside the inner element, it selects that element's content. */
    for (p = 3; p <= 11; p++) {
        CHECK(obj_at(t, p, 0, 'i', 't') == 0);
        CHECK(spans("hi"));
    }
}

static void
test_tag_around(void)
{
    const char *t = "<a><b>hi</b></a>\n";

    /* "around" takes the tags themselves. */
    CHECK(obj_at(t, 7, 0, 'a', 't') == 0);
    CHECK(spans("<b>hi</b>"));
}

static void
test_tag_outer_element(void)
{
    const char *t = "<a><b>hi</b></a>\n";

    /* From the outer element's own tags, the outer content is selected. */
    CHECK(obj_at(t, 1, 0, 'i', 't') == 0 && spans("<b>hi</b>"));
    CHECK(obj_at(t, 13, 0, 'i', 't') == 0 && spans("<b>hi</b>"));
}

static void
test_tag_count(void)
{
    const char *t = "<a><b>hi</b></a>\n";

    /* A count climbs out of the nested element. */
    CHECK(obj_at(t, 7, 2, 'i', 't') == 0 && spans("<b>hi</b>"));
}

static void
test_tag_absent(void)
{
    CHECK(obj_at("abc\n", 1, 0, 'i', 't') == -1);
    CHECK(errors == 1);
    /* Past the last tag there is no enclosing element either. */
    CHECK(obj_at("<a><b>hi</b></a>\n", 16, 0, 'i', 't') == -1);
}

/* ---- dispatch and errors ------------------------------------------------- */

static void
test_unknown_object(void)
{
    /* A character that names no object fails, with the beep. */
    CHECK(obj_at("abc\n", 1, 0, 'i', 'z') == -1);
    CHECK(errors == 1);
    CHECK(obj_at("abc\n", 1, 0, 'i', '!') == -1);
    CHECK(errors == 1);
}

static void
test_escape_is_silent(void)
{
    /* ESC means the user abandoned the command, so it fails without the
     * beep: obj 27 is the one value excluded from indicate_error. */
    CHECK(obj_at("abc\n", 1, 0, 'i', 27) == -1);
    CHECK(errors == 0);
}

static void
test_failure_leaves_outputs_untouched(void)
{
    /* On failure the caller's start/stop/buftype must not be written,
     * because range.c leaves its own values in them. */
    CHECK(obj_at("abc\n", 1, 0, 'i', '(') == -1);
    CHECK(r_start == -9 && r_stop == -9 && r_buftype == -9);
}

static void
test_range_is_ordered_and_in_bounds(void)
{
    static const char *texts[] = {"x (ab cd) y\n", "a \"hi\" b\n",
                                  "foo bar  baz\n", "<a><b>hi</b></a>\n"};
    static const char objs[] = "(){}[]<>\"'`wWt";
    size_t ti, oi;

    /* Whatever is asked for, a successful range must be ordered and lie
     * inside the buffer. */
    for (ti = 0; ti < sizeof(texts) / sizeof(texts[0]); ti++) {
        int len = (int)strlen(texts[ti]);
        int p;

        for (p = 0; p < len; p++)
            for (oi = 0; oi < sizeof(objs) - 1; oi++) {
                if (obj_at(texts[ti], p, 0, 'i', objs[oi]) != 0)
                    continue;
                CHECK(r_start >= 0);
                CHECK(r_stop < len);
                CHECK(r_start <= r_stop);
            }
    }
}

int
main(void)
{
    test_paren_inner();
    test_paren_around();
    test_paren_outside();
    test_every_bracket_pair();
    test_paren_nesting();
    test_paren_count_steps_out();
    test_paren_unbalanced();
    test_paren_empty();
    test_paren_multiline();

    test_quote_inner();
    test_quote_around();
    test_quote_kinds();
    test_quote_escaped();
    test_quote_unterminated();
    test_quote_empty();
    test_quote_outside();

    test_word_inner();
    test_word_around_takes_trailing_space();
    test_word_from_whitespace();
    test_word_punctuation();
    test_word_count();
    test_word_on_blank_line();
    test_word_on_empty_line_between_words();
    test_word_multibyte();

    test_tag_inner();
    test_tag_around();
    test_tag_outer_element();
    test_tag_count();
    test_tag_absent();

    test_unknown_object();
    test_escape_is_silent();
    test_failure_leaves_outputs_untouched();
    test_range_is_ordered_and_in_bounds();

    SUMMARY();
}
