/*
 * test_text.c - unit tests for text.c
 *
 * text.c is the read layer between struct editor and the piece tree: it
 * turns the int byte offsets the editor uses into the size_t offsets pt.c
 * takes, clamps out-of-range arguments, and composes the scanning and
 * overwriting helpers that pt.c does not provide.  It calls nothing but
 * pt.c and xmalloc, so this test links the real piece tree and needs no
 * stubs.  A zero-initialised struct editor with g.pt set is sufficient.
 *
 * Positions are byte offsets; text_len(&g) is one past the last byte.
 * Unlike the rest of vic, the texts here are not required to end with
 * '\n': text.c imposes no such contract, and several cases depend on
 * what happens at the very last byte.
 *
 * Each test runs twice, once over a single-piece tree and once over a
 * tree holding one piece per byte, because the chunk-walking paths
 * (text_find, text_read, text_copy) behave differently on a fragmented
 * tree than on a contiguous one.
 */
#include "testutil.h"
#include "text.h"
#include "pt.h"
#include <stdlib.h>
#include <string.h>

static struct editor g;

/* The whole text as one piece. */
static void
init(const char *content)
{
    size_t n = strlen(content);
    char *copy = malloc(n + 1);

    if (!copy)
        abort();
    memcpy(copy, content, n + 1);
    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt = ptnew(copy, n);
}

/*
 * The same text, one piece per byte.  Each insert goes to offset 0, so
 * none of them extends the piece the previous insert made, which is what
 * defeats the coalescing pt.c applies to sequential typing.
 */
static void
init_frag(const char *content)
{
    int n = (int)strlen(content);
    int i;

    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt = ptnew(NULL, 0);
    for (i = n - 1; i >= 0; i--)
        ptinsert(g.pt, 0, content + i, 1);
    if (n > 1 && (int)ptpieces(g.pt) < 2)
        abort(); /* the fixture itself failed: nothing would be exercised */
}

/* Whole text as a NUL-terminated string, read back byte by byte. */
static const char *
dump(void)
{
    static char buf[256];
    int n = text_len(&g);
    int i;

    if (n > (int)sizeof(buf) - 1)
        n = (int)sizeof(buf) - 1;
    for (i = 0; i < n; i++)
        buf[i] = (char)text_char(&g, i);
    buf[n] = '\0';
    return buf;
}

/* ---- text_len ------------------------------------------------------------ */

static void
test_len(void)
{
    init("");
    CHECK(text_len(&g) == 0);

    init("abc\ndef\n");
    CHECK(text_len(&g) == 8);

    init_frag("abc\ndef\n");
    CHECK(text_len(&g) == 8);
}

/* ---- text_char ----------------------------------------------------------- */

static void
test_char(void)
{
    init("abc\n");
    CHECK(text_char(&g, 0) == 'a');
    CHECK(text_char(&g, 2) == 'c');
    CHECK(text_char(&g, 3) == '\n');

    init_frag("abc\n");
    CHECK(text_char(&g, 0) == 'a');
    CHECK(text_char(&g, 2) == 'c');
    CHECK(text_char(&g, 3) == '\n');
}

static void
test_char_out_of_range(void)
{
    init("abc\n");
    /* Outside the text reads as 0, the way the old zero-filled slab did,
     * so a loop that reads one byte too far still terminates. */
    CHECK(text_char(&g, 4) == 0);
    CHECK(text_char(&g, 99) == 0);
    CHECK(text_char(&g, -1) == 0);
    CHECK(text_char(&g, -99) == 0);

    init("");
    CHECK(text_char(&g, 0) == 0);
}

static void
test_char_high_byte(void)
{
    /* "é" is 0xc3 0xa9.  The byte comes back unsigned, so a UTF-8 lead
     * byte is positive rather than negative. */
    init("a\xc3\xa9z");
    CHECK(text_char(&g, 1) == 0xc3);
    CHECK(text_char(&g, 2) == 0xa9);
    CHECK(text_char(&g, 1) > 0);

    init_frag("a\xc3\xa9z");
    CHECK(text_char(&g, 1) == 0xc3);
    CHECK(text_char(&g, 2) == 0xa9);
}

/* ---- text_find ----------------------------------------------------------- */

static void
test_find(void)
{
    init("abcabc");
    /* The byte at p itself counts as a hit. */
    CHECK(text_find(&g, 0, 'a') == 0);
    CHECK(text_find(&g, 1, 'a') == 3);
    CHECK(text_find(&g, 0, 'c') == 2);
    CHECK(text_find(&g, 3, 'c') == 5);

    init_frag("abcabc");
    CHECK(text_find(&g, 0, 'a') == 0);
    CHECK(text_find(&g, 1, 'a') == 3);
    CHECK(text_find(&g, 3, 'c') == 5);
}

static void
test_find_newline(void)
{
    init("ab\ncd\n");
    CHECK(text_find(&g, 0, '\n') == 2);
    CHECK(text_find(&g, 2, '\n') == 2);
    CHECK(text_find(&g, 3, '\n') == 5);

    init_frag("ab\ncd\n");
    CHECK(text_find(&g, 0, '\n') == 2);
    CHECK(text_find(&g, 3, '\n') == 5);
}

static void
test_find_absent(void)
{
    init("abc");
    CHECK(text_find(&g, 0, 'z') == -1);
    CHECK(text_find(&g, 0, '\n') == -1);

    init_frag("abc");
    CHECK(text_find(&g, 0, 'z') == -1);

    init("");
    CHECK(text_find(&g, 0, 'a') == -1);
}

static void
test_find_out_of_range(void)
{
    init("abc");
    /* A negative p is clamped to 0, so the search covers the whole text. */
    CHECK(text_find(&g, -5, 'a') == 0);
    /* At or past the end there is nothing left to scan. */
    CHECK(text_find(&g, 3, 'c') == -1);
    CHECK(text_find(&g, 99, 'a') == -1);

    init_frag("abc");
    CHECK(text_find(&g, -5, 'a') == 0);
    CHECK(text_find(&g, 99, 'a') == -1);
}

static void
test_find_high_byte(void)
{
    init("a\xc3\xa9z");
    CHECK(text_find(&g, 0, 0xc3) == 1);
    CHECK(text_find(&g, 0, 0xa9) == 2);

    init_frag("a\xc3\xa9z");
    CHECK(text_find(&g, 0, 0xc3) == 1);
}

/* ---- text_rfind ---------------------------------------------------------- */

static void
test_rfind(void)
{
    init("abcabc");
    /* The search is strictly before p, so the byte at p is never a hit. */
    CHECK(text_rfind(&g, 6, 'c') == 5);
    CHECK(text_rfind(&g, 5, 'c') == 2);
    CHECK(text_rfind(&g, 3, 'a') == 0);
    CHECK(text_rfind(&g, 4, 'a') == 3);

    init_frag("abcabc");
    CHECK(text_rfind(&g, 6, 'c') == 5);
    CHECK(text_rfind(&g, 5, 'c') == 2);
}

static void
test_rfind_newline(void)
{
    /* The '\n' case takes the line-index path rather than a backward
     * scan, so it is checked separately at every boundary. */
    init("abc\ndef\n");
    CHECK(text_rfind(&g, 8, '\n') == 7);
    CHECK(text_rfind(&g, 7, '\n') == 3); /* the '\n' at 7 is not before 7 */
    CHECK(text_rfind(&g, 4, '\n') == 3);
    CHECK(text_rfind(&g, 3, '\n') == -1); /* the '\n' at 3 is not before 3 */
    CHECK(text_rfind(&g, 1, '\n') == -1);

    init_frag("abc\ndef\n");
    CHECK(text_rfind(&g, 8, '\n') == 7);
    CHECK(text_rfind(&g, 7, '\n') == 3);
    CHECK(text_rfind(&g, 3, '\n') == -1);
}

static void
test_rfind_absent(void)
{
    init("abc");
    CHECK(text_rfind(&g, 3, 'z') == -1);
    CHECK(text_rfind(&g, 3, '\n') == -1);

    init_frag("abc");
    CHECK(text_rfind(&g, 3, 'z') == -1);
}

static void
test_rfind_out_of_range(void)
{
    init("abc\n");
    /* p is clamped to text_len, so a large p searches the whole text. */
    CHECK(text_rfind(&g, 99, 'a') == 0);
    CHECK(text_rfind(&g, 99, '\n') == 3);
    /* Nothing lies before offset 0. */
    CHECK(text_rfind(&g, 0, 'a') == -1);
    CHECK(text_rfind(&g, -5, 'a') == -1);
    CHECK(text_rfind(&g, 0, '\n') == -1);

    init("");
    CHECK(text_rfind(&g, 0, 'a') == -1);
    CHECK(text_rfind(&g, 5, '\n') == -1);
}

/* ---- text_read ----------------------------------------------------------- */

static void
test_read(void)
{
    char buf[16];

    init("abcdef");
    memset(buf, '?', sizeof(buf));
    CHECK(text_read(&g, 1, 3, buf) == 3);
    CHECK(memcmp(buf, "bcd", 3) == 0);
    /* dst is not NUL-terminated: byte 3 is untouched. */
    CHECK(buf[3] == '?');

    init_frag("abcdef");
    memset(buf, '?', sizeof(buf));
    CHECK(text_read(&g, 1, 3, buf) == 3);
    CHECK(memcmp(buf, "bcd", 3) == 0);
}

static void
test_read_clipped(void)
{
    char buf[16];

    init("abcdef");
    /* A range running past the end copies what there is and says so. */
    CHECK(text_read(&g, 4, 10, buf) == 2);
    CHECK(memcmp(buf, "ef", 2) == 0);

    init_frag("abcdef");
    CHECK(text_read(&g, 4, 10, buf) == 2);
    CHECK(memcmp(buf, "ef", 2) == 0);
}

static void
test_read_nothing(void)
{
    char buf[16];

    init("abcdef");
    memset(buf, '?', sizeof(buf));
    CHECK(text_read(&g, -1, 3, buf) == 0);
    CHECK(text_read(&g, 1, 0, buf) == 0);
    CHECK(text_read(&g, 1, -3, buf) == 0);
    CHECK(text_read(&g, 6, 3, buf) == 0); /* at the end */
    CHECK(text_read(&g, 99, 3, buf) == 0);
    /* No case above wrote anything. */
    CHECK(buf[0] == '?');

    init("");
    CHECK(text_read(&g, 0, 3, buf) == 0);
}

/* ---- text_copy ----------------------------------------------------------- */

static void
test_copy(void)
{
    char *s;

    init("abcdef");
    s = text_copy(&g, 1, 3);
    CHECK(s != NULL);
    CHECK(strcmp(s, "bcd") == 0); /* NUL-terminated, unlike text_read */
    CHECK(strlen(s) == 3);        /* the terminator sits at s[n], not beyond */
    free(s);

    init_frag("abcdef");
    s = text_copy(&g, 1, 3);
    CHECK(strcmp(s, "bcd") == 0);
    CHECK(strlen(s) == 3);
    free(s);
}

static void
test_copy_whole(void)
{
    char *s;

    init("abc\ndef\n");
    s = text_copy(&g, 0, text_len(&g));
    CHECK(strcmp(s, "abc\ndef\n") == 0);
    CHECK(strlen(s) == 8);
    free(s);

    init_frag("abc\ndef\n");
    s = text_copy(&g, 0, text_len(&g));
    CHECK(strcmp(s, "abc\ndef\n") == 0);
    CHECK(strlen(s) == 8);
    free(s);
}

static void
test_copy_clipped(void)
{
    char *s;

    init("abcdef");
    /* Past the end the result is short, not padded. */
    s = text_copy(&g, 4, 10);
    CHECK(strcmp(s, "ef") == 0);
    /* The terminator follows the bytes actually copied, not the n asked
     * for, so the string is two bytes long and not ten. */
    CHECK(strlen(s) == 2);
    free(s);
}

static void
test_copy_empty(void)
{
    char *s;

    init("abcdef");
    /* Every degenerate request still yields a freeable empty string. */
    s = text_copy(&g, 1, 0);
    CHECK(s != NULL && s[0] == '\0');
    free(s);

    s = text_copy(&g, 1, -5);
    CHECK(s != NULL && s[0] == '\0');
    free(s);

    s = text_copy(&g, -1, 3);
    CHECK(s != NULL && s[0] == '\0');
    free(s);

    s = text_copy(&g, 99, 3);
    CHECK(s != NULL && s[0] == '\0');
    free(s);
}

/* ---- text_set_char ------------------------------------------------------- */

static void
test_set_char(void)
{
    init("abcdef");
    text_set_char(&g, 2, 'Z');
    CHECK(strcmp(dump(), "abZdef") == 0);
    /* An overwrite is a delete plus an insert, so the length must not move. */
    CHECK(text_len(&g) == 6);
    CHECK(ptcheck(g.pt) == 1);

    /* First and last byte, the two off-by-one risks. */
    text_set_char(&g, 0, 'A');
    text_set_char(&g, 5, 'F');
    CHECK(strcmp(dump(), "AbZdeF") == 0);
    CHECK(text_len(&g) == 6);

    init_frag("abcdef");
    text_set_char(&g, 2, 'Z');
    text_set_char(&g, 5, 'F');
    CHECK(strcmp(dump(), "abZdeF") == 0);
    CHECK(text_len(&g) == 6);
    CHECK(ptcheck(g.pt) == 1);
}

static void
test_set_char_out_of_range(void)
{
    init("abc");
    /* Nothing is appended and nothing is prepended: the text is unchanged. */
    text_set_char(&g, 3, 'Z');
    text_set_char(&g, 99, 'Z');
    text_set_char(&g, -1, 'Z');
    CHECK(strcmp(dump(), "abc") == 0);
    CHECK(text_len(&g) == 3);

    init("");
    text_set_char(&g, 0, 'Z');
    CHECK(text_len(&g) == 0);
}

static void
test_set_char_newline_accounting(void)
{
    init("abc\ndef\n");
    CHECK(text_lines(&g) == 2);
    /* Overwriting a '\n' with a letter must drop the line count. */
    text_set_char(&g, 3, 'X');
    CHECK(strcmp(dump(), "abcXdef\n") == 0);
    CHECK(text_lines(&g) == 1);
    CHECK(text_line_start(&g, 1) == 8);
    /* And writing one back must restore it. */
    text_set_char(&g, 3, '\n');
    CHECK(text_lines(&g) == 2);
    CHECK(text_line_start(&g, 1) == 4);
    CHECK(ptcheck(g.pt) == 1);
}

/* ---- text_set_bytes ------------------------------------------------------ */

static void
test_set_bytes(void)
{
    init("abcdef");
    text_set_bytes(&g, 1, "XYZ", 3);
    CHECK(strcmp(dump(), "aXYZef") == 0);
    CHECK(text_len(&g) == 6);
    CHECK(ptcheck(g.pt) == 1);

    init_frag("abcdef");
    text_set_bytes(&g, 1, "XYZ", 3);
    CHECK(strcmp(dump(), "aXYZef") == 0);
    CHECK(text_len(&g) == 6);
    CHECK(ptcheck(g.pt) == 1);
}

static void
test_set_bytes_clipped(void)
{
    init("abcdef");
    /* A range past the end is clipped rather than extending the text. */
    text_set_bytes(&g, 4, "XYZW", 4);
    CHECK(strcmp(dump(), "abcdXY") == 0);
    CHECK(text_len(&g) == 6);

    init_frag("abcdef");
    text_set_bytes(&g, 4, "XYZW", 4);
    CHECK(strcmp(dump(), "abcdXY") == 0);
    CHECK(text_len(&g) == 6);
}

static void
test_set_bytes_whole(void)
{
    init("abcdef");
    text_set_bytes(&g, 0, "ZZZZZZ", 6);
    CHECK(strcmp(dump(), "ZZZZZZ") == 0);
    CHECK(text_len(&g) == 6);
    CHECK(ptcheck(g.pt) == 1);
}

static void
test_set_bytes_out_of_range(void)
{
    init("abc");
    text_set_bytes(&g, -1, "XYZ", 3);
    text_set_bytes(&g, 3, "XYZ", 3);
    text_set_bytes(&g, 99, "XYZ", 3);
    text_set_bytes(&g, 1, "XYZ", 0);
    text_set_bytes(&g, 1, "XYZ", -3);
    CHECK(strcmp(dump(), "abc") == 0);
    CHECK(text_len(&g) == 3);

    init("");
    text_set_bytes(&g, 0, "XYZ", 3);
    CHECK(text_len(&g) == 0);
}

static void
test_set_bytes_matches_set_char(void)
{
    /* The whole point of text_set_bytes is to replace a loop of
     * text_set_char with one tree operation, so the two must agree. */
    init("abcdef");
    text_set_bytes(&g, 1, "XYZ", 3);
    CHECK(strcmp(dump(), "aXYZef") == 0);

    init("abcdef");
    text_set_char(&g, 1, 'X');
    text_set_char(&g, 2, 'Y');
    text_set_char(&g, 3, 'Z');
    CHECK(strcmp(dump(), "aXYZef") == 0);
}

static void
test_set_bytes_newline_accounting(void)
{
    init("abc\ndef\n");
    /* Overwriting a range that holds a '\n' with one that does not. */
    text_set_bytes(&g, 2, "XXX", 3);
    CHECK(strcmp(dump(), "abXXXef\n") == 0);
    CHECK(text_lines(&g) == 1);
    /* And back the other way. */
    text_set_bytes(&g, 2, "c\nd", 3);
    CHECK(strcmp(dump(), "abc\ndef\n") == 0);
    CHECK(text_lines(&g) == 2);
    CHECK(ptcheck(g.pt) == 1);
}

/* ---- text_line_of -------------------------------------------------------- */

static void
test_line_of(void)
{
    init("abc\ndef\n");
    /* The line number is the count of newlines before the offset. */
    CHECK(text_line_of(&g, 0) == 0);
    CHECK(text_line_of(&g, 3) == 0); /* the '\n' is not before itself */
    CHECK(text_line_of(&g, 4) == 1);
    CHECK(text_line_of(&g, 7) == 1);
    CHECK(text_line_of(&g, 8) == 2); /* the empty last line */

    init_frag("abc\ndef\n");
    CHECK(text_line_of(&g, 3) == 0);
    CHECK(text_line_of(&g, 4) == 1);
    CHECK(text_line_of(&g, 8) == 2);
}

static void
test_line_of_out_of_range(void)
{
    init("abc\ndef\n");
    CHECK(text_line_of(&g, -1) == 0);
    CHECK(text_line_of(&g, -99) == 0);
    /* Past the end gives the total newline count. */
    CHECK(text_line_of(&g, 99) == 2);

    init("");
    CHECK(text_line_of(&g, 0) == 0);
    CHECK(text_line_of(&g, 5) == 0);
}

/* ---- text_line_start ----------------------------------------------------- */

static void
test_line_start(void)
{
    init("abc\ndef\nghi\n");
    CHECK(text_line_start(&g, 0) == 0);
    CHECK(text_line_start(&g, 1) == 4);
    CHECK(text_line_start(&g, 2) == 8);
    CHECK(text_line_start(&g, 3) == 12); /* the empty last line */

    init_frag("abc\ndef\nghi\n");
    CHECK(text_line_start(&g, 1) == 4);
    CHECK(text_line_start(&g, 2) == 8);
}

static void
test_line_start_out_of_range(void)
{
    init("abc\ndef\n");
    CHECK(text_line_start(&g, -1) == 0);
    /* Beyond the last line the answer is text_len. */
    CHECK(text_line_start(&g, 99) == 8);
    CHECK(text_line_start(&g, 99) == text_len(&g));

    init("");
    CHECK(text_line_start(&g, 0) == 0);
    CHECK(text_line_start(&g, 1) == 0);
}

static void
test_line_start_inverts_line_of(void)
{
    int i;

    /* text_line_start is the inverse of text_line_of on line starts. */
    init("abc\n\ndefgh\nij\n");
    for (i = 0; i <= text_lines(&g); i++)
        CHECK(text_line_of(&g, text_line_start(&g, i)) == i);

    init_frag("abc\n\ndefgh\nij\n");
    for (i = 0; i <= text_lines(&g); i++)
        CHECK(text_line_of(&g, text_line_start(&g, i)) == i);
}

/* ---- text_lines ---------------------------------------------------------- */

static void
test_lines(void)
{
    init("");
    CHECK(text_lines(&g) == 0);

    init("abc");
    CHECK(text_lines(&g) == 0); /* no newline at all */

    init("abc\n");
    CHECK(text_lines(&g) == 1);

    init("abc\ndef\n");
    CHECK(text_lines(&g) == 2);

    init("\n\n\n");
    CHECK(text_lines(&g) == 3);

    init_frag("abc\ndef\n");
    CHECK(text_lines(&g) == 2);
}

/* ---- reading after an edit ----------------------------------------------- */

static void
test_read_after_edit(void)
{
    char *s;

    /* An insert splits a piece, so every reader has to cross the seam. */
    init("abcdef");
    ptinsert(g.pt, 3, "XY", 2);
    CHECK(text_len(&g) == 8);
    CHECK(text_char(&g, 3) == 'X');
    CHECK(text_char(&g, 5) == 'd');
    CHECK(text_find(&g, 0, 'd') == 5);
    CHECK(text_rfind(&g, 8, 'X') == 3);
    s = text_copy(&g, 0, 8);
    CHECK(strcmp(s, "abcXYdef") == 0);
    free(s);

    /* A delete removes a piece or trims one. */
    ptdelete(g.pt, 3, 2);
    CHECK(text_len(&g) == 6);
    s = text_copy(&g, 0, 6);
    CHECK(strcmp(s, "abcdef") == 0);
    free(s);
    CHECK(text_find(&g, 0, 'X') == -1);
    CHECK(ptcheck(g.pt) == 1);
}

int
main(void)
{
    test_len();

    test_char();
    test_char_out_of_range();
    test_char_high_byte();

    test_find();
    test_find_newline();
    test_find_absent();
    test_find_out_of_range();
    test_find_high_byte();

    test_rfind();
    test_rfind_newline();
    test_rfind_absent();
    test_rfind_out_of_range();

    test_read();
    test_read_clipped();
    test_read_nothing();

    test_copy();
    test_copy_whole();
    test_copy_clipped();
    test_copy_empty();

    test_set_char();
    test_set_char_out_of_range();
    test_set_char_newline_accounting();

    test_set_bytes();
    test_set_bytes_clipped();
    test_set_bytes_whole();
    test_set_bytes_out_of_range();
    test_set_bytes_matches_set_char();
    test_set_bytes_newline_accounting();

    test_line_of();
    test_line_of_out_of_range();

    test_line_start();
    test_line_start_out_of_range();
    test_line_start_inverts_line_of();

    test_lines();

    test_read_after_edit();

    SUMMARY();
}
