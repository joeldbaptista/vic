/*
 * test_buffer.c - unit tests for buffer.c
 *
 * buffer.c owns every mutation of the text and both halves of file I/O.
 * It reaches out to four other translation units (undo.c, status.c,
 * input.c and search.c for showmatching), so this file links against the
 * real piece tree (pt.c, text.c), the real line helpers (line.c,
 * codepoint.c, utf8.c) and stubs for the rest.  The stubs record what was
 * asked of them, which is how the undo bookkeeping is asserted here
 * without dragging in the undo stack itself.
 *
 * Buffer contract: a live editor buffer always ends with '\n' (the
 * sentinel).  Positions are int byte offsets; text_len(&g) is one past
 * the last byte.
 */
#include "testutil.h"
#include "buffer.h"
#include "line.h"
#include "pt.h"
#include "text.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- stubs ---------------------------------------------------------------
 *
 * Every undo call is logged instead of performed.  kind is 'd' for
 * undo_push (a delete record) and 'i' for undo_push_insert; mode holds
 * the UNDO_* type for 'd' and the NO_UNDO/ALLOW_UNDO* argument for 'i'.
 */

struct urec {
    char kind;
    int  p;
    int  len;
    int  mode;
};

static struct urec u_log[64];
static int u_n;
static int u_commits;
static int u_pops;
static int u_flushes;
static int u_loads;
static int u_saves;
static int sm_calls;     /* showmatching() */
static int ydreg_resets; /* reset_ydreg() */
static int status_calls;
static char status_last[256];
static int next_char; /* what get_one_char() hands back */

static void
reset_log(void)
{
    u_n = u_commits = u_pops = u_flushes = u_loads = u_saves = 0;
    sm_calls = ydreg_resets = status_calls = 0;
    status_last[0] = '\0';
    next_char = 0;
}

static void
log_undo(char kind, int p, int len, int mode)
{
    if (u_n < (int)(sizeof(u_log) / sizeof(u_log[0]))) {
        u_log[u_n].kind = kind;
        u_log[u_n].p    = p;
        u_log[u_n].len  = len;
        u_log[u_n].mode = mode;
    }
    u_n++;
}

void
undo_push(struct editor *g, int src, unsigned length, int u_type)
{
    (void)g;
    log_undo('d', src, (int)length, u_type);
}

void
undo_push_insert(struct editor *g, int p, int len, int undo)
{
    (void)g;
    log_undo('i', p, len, undo);
}

void
undo_queue_commit(struct editor *g)
{
    (void)g;
    u_commits++;
}

void
undo_pop(struct editor *g)
{
    (void)g;
    u_pops++;
}

void
flush_undo_data(struct editor *g)
{
    (void)g;
    u_flushes++;
}

void
undo_load(struct editor *g, const char *fn)
{
    (void)g;
    (void)fn;
    u_loads++;
}

void
undo_save(struct editor *g, const char *fn)
{
    (void)g;
    (void)fn;
    u_saves++;
}

void
status_line_bold(struct editor *g, const char *format, ...)
{
    va_list ap;

    (void)g;
    status_calls++;
    va_start(ap, format);
    vsnprintf(status_last, sizeof(status_last), format, ap);
    va_end(ap);
}

void
status_line_bold_errno(struct editor *g, const char *fn)
{
    (void)g;
    status_calls++;
    snprintf(status_last, sizeof(status_last), "%s", fn);
}

int
get_one_char(struct editor *g)
{
    (void)g;
    return next_char;
}

void
reset_ydreg(struct editor *g)
{
    (void)g;
    ydreg_resets++;
}

void
showmatching(struct editor *g, int p)
{
    (void)g;
    (void)p;
    sm_calls++;
}

/* ---- fixture ------------------------------------------------------------- */

static struct editor g;

static void
init(const char *content)
{
    size_t n = strlen(content);
    char *copy = malloc(n + 1);

    if (!copy)
        abort();
    memcpy(copy, content, n + 1);
    ptfree(g.pt);
    free(g.current_filename);
    free(g.alt_filename);
    memset(&g, 0, sizeof(g));
    g.pt                   = ptnew(copy, n);
    g.tabstop              = 8;
    g.rstart               = -1;
    g.newindent            = -1;
    g.term_orig.c_cc[VERASE] = 127;
    reset_log();
}

/* Whole buffer as a NUL-terminated string, for comparison. */
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

/* A temporary file holding `content`; the path is copied into `path`. */
static void
mkfile(char *path, size_t pathsz, const char *content)
{
    int fd;

    snprintf(path, pathsz, "/tmp/vic_test_buffer_XXXXXX");
    fd = mkstemp(path);
    if (fd < 0)
        abort();
    if (write(fd, content, strlen(content)) != (ssize_t)strlen(content))
        abort();
    close(fd);
}

static char *
slurp(const char *path)
{
    static char buf[1024];
    int fd  = open(path, O_RDONLY);
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

/* ---- text_insert_bytes --------------------------------------------------- */

static void
test_insert_bytes(void)
{
    init("abc\n");
    /* The return value is the old realloc bias and is always 0. */
    CHECK(text_insert_bytes(&g, 1, "XY", 2) == 0);
    CHECK(strcmp(dump(), "aXYbc\n") == 0);
    CHECK(text_len(&g) == 6);
    /* No undo record: this is the raw path. */
    CHECK(u_n == 0);

    /* Insert at the very end (after the sentinel). */
    init("abc\n");
    text_insert_bytes(&g, 4, "Z", 1);
    CHECK(strcmp(dump(), "abc\nZ") == 0);
}

static void
test_insert_bytes_clamped(void)
{
    init("abc\n");
    text_insert_bytes(&g, -5, "X", 1);
    CHECK(strcmp(dump(), "Xabc\n") == 0);

    init("abc\n");
    text_insert_bytes(&g, 99, "X", 1);
    CHECK(strcmp(dump(), "abc\nX") == 0);
}

static void
test_insert_bytes_empty(void)
{
    init("abc\n");
    CHECK(text_insert_bytes(&g, 1, "XY", 0) == 0);
    CHECK(text_insert_bytes(&g, 1, "XY", -3) == 0);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(u_n == 0);
}

/* ---- text_hole_delete ---------------------------------------------------- */

static void
test_hole_delete(void)
{
    init("abcdef\n");
    /* [1,3] is "bcd" — the range is inclusive at both ends. */
    CHECK(text_hole_delete(&g, 1, 3, NO_UNDO) == 1);
    CHECK(strcmp(dump(), "aef\n") == 0);
    CHECK(u_n == 0);
}

static void
test_hole_delete_reversed(void)
{
    init("abcdef\n");
    /* q < p deletes the same bytes and returns the lower end. */
    CHECK(text_hole_delete(&g, 3, 1, NO_UNDO) == 1);
    CHECK(strcmp(dump(), "aef\n") == 0);
}

static void
test_hole_delete_undo_records(void)
{
    init("abcdef\n");
    text_hole_delete(&g, 1, 3, ALLOW_UNDO);
    CHECK(u_n == 1);
    CHECK(u_log[0].kind == 'd');
    CHECK(u_log[0].p == 1);
    CHECK(u_log[0].len == 3); /* q - p + 1 */
    CHECK(u_log[0].mode == UNDO_DEL);

    init("abcdef\n");
    text_hole_delete(&g, 1, 3, ALLOW_UNDO_CHAIN);
    CHECK(u_n == 1 && u_log[0].mode == UNDO_DEL_CHAIN);

    init("abcdef\n");
    text_hole_delete(&g, 1, 3, ALLOW_UNDO_QUEUED);
    CHECK(u_n == 1 && u_log[0].mode == UNDO_DEL_QUEUED);
}

static void
test_hole_delete_whole_buffer(void)
{
    init("abc\n");
    /* Deleting everything leaves an empty buffer and position 0. */
    CHECK(text_hole_delete(&g, 0, 3, NO_UNDO) == 0);
    CHECK(text_len(&g) == 0);
}

static void
test_hole_delete_tail(void)
{
    init("abcdef\n");
    /* Deleting the tail clamps the returned position to the last byte. */
    CHECK(text_hole_delete(&g, 4, 6, NO_UNDO) == 3);
    CHECK(strcmp(dump(), "abcd") == 0);
}

static void
test_hole_delete_out_of_range(void)
{
    init("abc\n");
    /* dest past the end: nothing is removed, dest comes back unchanged. */
    CHECK(text_hole_delete(&g, 10, 12, NO_UNDO) == 10);
    CHECK(strcmp(dump(), "abc\n") == 0);
    /* An undo record is still pushed before the range check. */
    init("abc\n");
    text_hole_delete(&g, 10, 12, ALLOW_UNDO);
    CHECK(u_n == 1);
}

/* ---- string_insert ------------------------------------------------------- */

static void
test_string_insert(void)
{
    init("abc\n");
    CHECK(string_insert(&g, 1, "XYZ", ALLOW_UNDO) == 0);
    CHECK(strcmp(dump(), "aXYZbc\n") == 0);
    /* One insert record for the whole string. */
    CHECK(u_n == 1);
    CHECK(u_log[0].kind == 'i');
    CHECK(u_log[0].p == 1);
    CHECK(u_log[0].len == 3);
    CHECK(u_log[0].mode == ALLOW_UNDO);
}

static void
test_string_insert_empty(void)
{
    init("abc\n");
    string_insert(&g, 1, "", ALLOW_UNDO);
    CHECK(strcmp(dump(), "abc\n") == 0);
    /* The record is pushed with length 0; the insert itself is a no-op. */
    CHECK(u_n == 1 && u_log[0].len == 0);
}

/* ---- char_insert --------------------------------------------------------- */

static void
test_char_insert_plain(void)
{
    init("ac\n");
    /* Returns the offset just past the inserted byte. */
    CHECK(char_insert(&g, 1, 'b', ALLOW_UNDO) == 2);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(u_n == 1 && u_log[0].kind == 'i' && u_log[0].len == 1);
    CHECK(u_commits == 0);
}

static void
test_char_insert_cr_becomes_newline(void)
{
    init("ab\n");
    CHECK(char_insert(&g, 2, ASCII_CR, ALLOW_UNDO) == 3);
    CHECK(strcmp(dump(), "ab\n\n") == 0);
    /* '\n' commits the queued undo run first. */
    CHECK(u_commits == 1);
}

static void
test_char_insert_expandtab(void)
{
    init("\n");
    g.setops |= VI_EXPANDTAB;
    /* At column 0 a tab fills out to the next stop: 8 spaces. */
    CHECK(char_insert(&g, 0, '\t', ALLOW_UNDO) == 8);
    CHECK(strcmp(dump(), "        \n") == 0);
    /* One record per space. */
    CHECK(u_n == 8);

    /* Without expandtab the byte goes in as-is. */
    init("\n");
    CHECK(char_insert(&g, 0, '\t', ALLOW_UNDO) == 1);
    CHECK(strcmp(dump(), "\t\n") == 0);
}

static void
test_char_insert_literal(void)
{
    init("\n");
    next_char = 'A';
    /* Ctrl-V writes a placeholder '^' then overwrites it with the next
     * character read from the terminal. */
    CHECK(char_insert(&g, 0, ASCII_CTRL_V, ALLOW_UNDO) == 1);
    CHECK(strcmp(dump(), "A\n") == 0);
    CHECK(u_n == 1 && u_log[0].kind == 'i' && u_log[0].len == 1);
}

static void
test_char_insert_backspace(void)
{
    init("abc\n");
    /* Backspace in insert mode removes the character before p. */
    CHECK(char_insert(&g, 2, 127, ALLOW_UNDO) == 1);
    CHECK(strcmp(dump(), "ac\n") == 0);
    CHECK(u_n == 1 && u_log[0].kind == 'd' && u_log[0].mode == UNDO_DEL_QUEUED);

    /* At the start of the buffer there is nothing to delete. */
    init("abc\n");
    CHECK(char_insert(&g, 0, 127, ALLOW_UNDO) == 0);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(u_n == 0);
}

static void
test_char_insert_backspace_multibyte(void)
{
    /* "é" is two bytes; one backspace removes both. */
    init("a\xc3\xa9z\n");
    CHECK(char_insert(&g, 3, 8, ALLOW_UNDO) == 1);
    CHECK(strcmp(dump(), "az\n") == 0);
}

static void
test_char_insert_backspace_replace_mode(void)
{
    init("abc\n");
    g.cmd_mode = 2; /* REPLACE */
    g.rstart   = 1;
    /* Replace mode rewinds by popping the undo stack instead of deleting. */
    CHECK(char_insert(&g, 2, 127, ALLOW_UNDO) == 1);
    CHECK(u_pops == 1);
    CHECK(strcmp(dump(), "abc\n") == 0);

    /* Not past the start of the replace run: nothing happens. */
    init("abc\n");
    g.cmd_mode = 2;
    g.rstart   = 2;
    CHECK(char_insert(&g, 2, 127, ALLOW_UNDO) == 2);
    CHECK(u_pops == 0);
}

static void
test_char_insert_showmatch(void)
{
    init("(x\n");
    g.setops |= VI_SHOWMATCH;
    char_insert(&g, 2, ')', ALLOW_UNDO);
    CHECK(strcmp(dump(), "(x)\n") == 0);
    CHECK(sm_calls == 1);

    /* A non-bracket does not trigger the match display. */
    init("(x\n");
    g.setops |= VI_SHOWMATCH;
    char_insert(&g, 2, 'y', ALLOW_UNDO);
    CHECK(sm_calls == 0);
}

static void
test_char_insert_ctrl_d(void)
{
    init("\t\tx\n");
    /* Ctrl-D removes one tab stop of indent. */
    CHECK(char_insert(&g, 2, ASCII_CTRL_D, ALLOW_UNDO) == 1);
    CHECK(strcmp(dump(), "\tx\n") == 0);

    /* With no indent there is nothing to remove. */
    init("x\n");
    CHECK(char_insert(&g, 1, ASCII_CTRL_D, ALLOW_UNDO) == 1);
    CHECK(strcmp(dump(), "x\n") == 0);
    CHECK(u_n == 0);
}

static void
test_char_insert_autoindent(void)
{
    init("    ab\n");
    g.setops |= VI_AUTOINDENT;
    /* Newline after an indented line copies that indent to the new line.
     * expandtab is off, tabstop is 8, so four columns become four spaces. */
    CHECK(char_insert(&g, 6, '\n', ALLOW_UNDO) == 11);
    CHECK(strcmp(dump(), "    ab\n    \n") == 0);
    CHECK(g.indent_col == 0); /* cmd_mode is 0 here */
}

static void
test_char_insert_autoindent_off(void)
{
    init("    ab\n");
    CHECK(char_insert(&g, 6, '\n', ALLOW_UNDO) == 7);
    CHECK(strcmp(dump(), "    ab\n\n") == 0);
}

static void
test_char_insert_esc(void)
{
    init("abc\n");
    g.dot      = 3;
    g.cmd_mode = 1;
    /* ESC leaves insert mode, commits the undo queue and steps back one. */
    CHECK(char_insert(&g, 3, ASCII_ESC, ALLOW_UNDO) == 2);
    CHECK(g.cmd_mode == 0);
    CHECK(u_commits == 1);
    CHECK(ydreg_resets == 1);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_char_insert_esc_strips_autoindent(void)
{
    init("    \n");
    g.setops    |= VI_AUTOINDENT;
    g.dot        = 4;
    g.cmd_mode   = 1;
    g.indent_col = 4;
    /* Leaving insert mode on a line that holds only autoindent whitespace
     * discards that whitespace. */
    CHECK(char_insert(&g, 4, ASCII_ESC, ALLOW_UNDO) == 0);
    CHECK(strcmp(dump(), "\n") == 0);
}

/* ---- buffer_replace_range ------------------------------------------------ */

static void
test_replace_range(void)
{
    init("abcdef\n");
    CHECK(buffer_replace_range(&g, 1, 3, "XY", 2) == 0);
    CHECK(strcmp(dump(), "aXYef\n") == 0);
    /* A delete record then a chained insert, so one 'u' reverses both. */
    CHECK(u_n == 2);
    CHECK(u_log[0].kind == 'd' && u_log[0].mode == UNDO_DEL);
    CHECK(u_log[1].kind == 'i' && u_log[1].mode == ALLOW_UNDO_CHAIN);
}

static void
test_replace_range_delete_only(void)
{
    init("abcdef\n");
    CHECK(buffer_replace_range(&g, 1, 3, NULL, 0) == 0);
    CHECK(strcmp(dump(), "aef\n") == 0);
    CHECK(u_n == 1 && u_log[0].kind == 'd');
}

static void
test_replace_range_insert_only(void)
{
    init("abc\n");
    /* An empty range (rs > re) inserts without deleting, so the insert
     * record must NOT be chained to anything. */
    CHECK(buffer_replace_range(&g, 2, 1, "XY", 2) == 0);
    CHECK(strcmp(dump(), "abXYc\n") == 0);
    CHECK(u_n == 1);
    CHECK(u_log[0].kind == 'i' && u_log[0].mode == ALLOW_UNDO);
}

static void
test_replace_range_sentinel(void)
{
    init("abc\n");
    /* The range spans the sentinel '\n' and the new text ends in '\n', so
     * the final byte is left in place: the delete is one byte short and
     * the insert carries one byte less. */
    CHECK(buffer_replace_range(&g, 0, 3, "xy\n", 3) == 0);
    CHECK(strcmp(dump(), "xy\n") == 0);
    CHECK(u_n == 2);
    CHECK(u_log[0].kind == 'd' && u_log[0].len == 3); /* [0,2], not [0,3] */
    CHECK(u_log[1].kind == 'i' && u_log[1].len == 2); /* "xy", not "xy\n" */
}

static void
test_replace_range_sentinel_not_newline(void)
{
    /* Last byte is not '\n': the trim must not apply, or the byte would be
     * duplicated. */
    init("abcZ");
    CHECK(buffer_replace_range(&g, 0, 3, "xy\n", 3) == 0);
    CHECK(strcmp(dump(), "xy\n") == 0);
    CHECK(u_log[0].len == 4);
    CHECK(u_log[1].len == 3);
}

static void
test_replace_range_sentinel_new_text_no_newline(void)
{
    /* New content does not end in '\n': the trim must not apply either. */
    init("abc\n");
    CHECK(buffer_replace_range(&g, 0, 3, "xy", 2) == 0);
    CHECK(strcmp(dump(), "xy") == 0);
    CHECK(u_log[0].len == 4);
    CHECK(u_log[1].len == 2);
}

/* ---- filenames ----------------------------------------------------------- */

static void
test_init_filename(void)
{
    init("\n");
    init_filename(&g, "one.txt");
    CHECK(g.current_filename != NULL);
    CHECK(strcmp(g.current_filename, "one.txt") == 0);
    CHECK(g.alt_filename == NULL);

    /* A second call does not overwrite the current name; it fills the
     * alternate ('#') slot instead. */
    init_filename(&g, "two.txt");
    CHECK(strcmp(g.current_filename, "one.txt") == 0);
    CHECK(g.alt_filename != NULL && strcmp(g.alt_filename, "two.txt") == 0);
}

static void
test_update_filename(void)
{
    init("\n");
    update_filename(&g, "one.txt");
    CHECK(strcmp(g.current_filename, "one.txt") == 0);
    CHECK(g.alt_filename == NULL);

    /* A different name demotes the old one to the alternate slot. */
    update_filename(&g, "two.txt");
    CHECK(strcmp(g.current_filename, "two.txt") == 0);
    CHECK(strcmp(g.alt_filename, "one.txt") == 0);

    /* The same name again is a no-op. */
    update_filename(&g, "two.txt");
    CHECK(strcmp(g.current_filename, "two.txt") == 0);
    CHECK(strcmp(g.alt_filename, "one.txt") == 0);

    /* NULL is a no-op. */
    update_filename(&g, NULL);
    CHECK(strcmp(g.current_filename, "two.txt") == 0);
}

/* ---- file_insert --------------------------------------------------------- */

static void
test_file_insert(void)
{
    char path[64];

    mkfile(path, sizeof(path), "hello\n");
    init("XY\n");
    CHECK(file_insert(&g, path, 1, 0) == 6);
    CHECK(strcmp(dump(), "Xhello\nY\n") == 0);
    CHECK(u_n == 1 && u_log[0].kind == 'i' && u_log[0].len == 6);
    CHECK(status_calls == 0);
    unlink(path);
}

static void
test_file_insert_clamps(void)
{
    char path[64];

    mkfile(path, sizeof(path), "ab");
    init("Z\n");
    CHECK(file_insert(&g, path, 999, 0) == 2);
    CHECK(strcmp(dump(), "Z\nab") == 0);
    unlink(path);
}

static void
test_file_insert_missing(void)
{
    init("Z\n");
    /* initial = 0 reports the error; the buffer is untouched. */
    CHECK(file_insert(&g, "/no/such/file", 0, 0) == -1);
    CHECK(status_calls == 1);
    CHECK(strcmp(dump(), "Z\n") == 0);

    /* initial = 1 stays quiet: it is the "editing a new file" case. */
    init("Z\n");
    CHECK(file_insert(&g, "/no/such/file", 0, 1) == -1);
    CHECK(status_calls == 0);
}

static void
test_file_insert_not_regular(void)
{
    init("Z\n");
    CHECK(file_insert(&g, "/tmp", 0, 0) == -1);
    CHECK(status_calls == 1);
    CHECK(strcmp(dump(), "Z\n") == 0);
}

static void
test_file_insert_readonly(void)
{
    char path[64];

    mkfile(path, sizeof(path), "ro\n");
    chmod(path, 0444);
    init("\n");
    file_insert(&g, path, 0, 1);
    /* Bit 0 of readonly_mode is the read-only-file flag. */
    CHECK((g.readonly_mode & 0x01) != 0);
    chmod(path, 0644);
    unlink(path);
}

/* ---- init_text_buffer ---------------------------------------------------- */

static void
test_init_text_buffer(void)
{
    char path[64];

    mkfile(path, sizeof(path), "one\ntwo\n");
    init("junk\n");
    CHECK(init_text_buffer(&g, path) == 8);
    CHECK(strcmp(dump(), "one\ntwo\n") == 0);
    CHECK(g.dot == 0 && g.screenbegin == 0);
    CHECK(g.modified_count == 0);
    CHECK(g.last_modified_count == -1);
    CHECK(u_flushes == 1);
    CHECK(u_loads == 1);
    CHECK(strcmp(g.current_filename, path) == 0);
    /* Every mark starts unset. */
    CHECK(g.mark[0] == -1 && g.mark[25] == -1 && g.mark[29] == -1);
    unlink(path);
}

static void
test_init_text_buffer_appends_sentinel(void)
{
    char path[64];

    mkfile(path, sizeof(path), "no newline");
    init("junk\n");
    init_text_buffer(&g, path);
    CHECK(strcmp(dump(), "no newline\n") == 0);
    unlink(path);
}

static void
test_init_text_buffer_new_file(void)
{
    init("junk\n");
    /* A file that does not exist yet still yields a one-byte buffer. */
    CHECK(init_text_buffer(&g, "/no/such/file") < 0);
    CHECK(strcmp(dump(), "\n") == 0);
    CHECK(text_len(&g) == 1);
}

/* ---- file_write ---------------------------------------------------------- */

static void
test_file_write(void)
{
    char path[64];

    mkfile(path, sizeof(path), "");
    init("one\ntwo\n");
    /* [first, last] is inclusive, so the whole buffer is [0, len-1]. */
    CHECK(file_write(&g, path, 0, text_len(&g) - 1) == 8);
    CHECK(strcmp(slurp(path), "one\ntwo\n") == 0);
    CHECK(u_saves == 1);
    unlink(path);
}

static void
test_file_write_range(void)
{
    char path[64];

    mkfile(path, sizeof(path), "");
    init("one\ntwo\n");
    CHECK(file_write(&g, path, 4, 7) == 4);
    CHECK(strcmp(slurp(path), "two\n") == 0);
    unlink(path);
}

static void
test_file_write_truncates(void)
{
    char path[64];

    /* The file is opened without O_TRUNC, so the ftruncate afterwards is
     * what stops leftovers from a longer previous version surviving. */
    mkfile(path, sizeof(path), "a much longer previous version\n");
    init("short\n");
    CHECK(file_write(&g, path, 0, text_len(&g) - 1) == 6);
    CHECK(strcmp(slurp(path), "short\n") == 0);
    unlink(path);
}

static void
test_file_write_no_name(void)
{
    init("x\n");
    CHECK(file_write(&g, NULL, 0, 1) == -2);
    CHECK(status_calls == 1);
    CHECK(u_saves == 0);
}

static void
test_file_write_bad_path(void)
{
    init("x\n");
    CHECK(file_write(&g, (char *)"/no/such/dir/f", 0, 1) == -1);
    CHECK(u_saves == 0);
}

int
main(void)
{
    test_insert_bytes();
    test_insert_bytes_clamped();
    test_insert_bytes_empty();

    test_hole_delete();
    test_hole_delete_reversed();
    test_hole_delete_undo_records();
    test_hole_delete_whole_buffer();
    test_hole_delete_tail();
    test_hole_delete_out_of_range();

    test_string_insert();
    test_string_insert_empty();

    test_char_insert_plain();
    test_char_insert_cr_becomes_newline();
    test_char_insert_expandtab();
    test_char_insert_literal();
    test_char_insert_backspace();
    test_char_insert_backspace_multibyte();
    test_char_insert_backspace_replace_mode();
    test_char_insert_showmatch();
    test_char_insert_ctrl_d();
    test_char_insert_autoindent();
    test_char_insert_autoindent_off();
    test_char_insert_esc();
    test_char_insert_esc_strips_autoindent();

    test_replace_range();
    test_replace_range_delete_only();
    test_replace_range_insert_only();
    test_replace_range_sentinel();
    test_replace_range_sentinel_not_newline();
    test_replace_range_sentinel_new_text_no_newline();

    test_init_filename();
    test_update_filename();

    test_file_insert();
    test_file_insert_clamps();
    test_file_insert_missing();
    test_file_insert_not_regular();
    test_file_insert_readonly();

    test_init_text_buffer();
    test_init_text_buffer_appends_sentinel();
    test_init_text_buffer_new_file();

    test_file_write();
    test_file_write_range();
    test_file_write_truncates();
    test_file_write_no_name();
    test_file_write_bad_path();

    SUMMARY();
}
