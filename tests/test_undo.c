/*
 * test_undo.c - unit tests for undo.c
 *
 * buffer.c is linked for real alongside undo.c, so these tests drive the
 * pair the way the editor does: make an edit through buffer.c, then undo
 * it and check the text came back.  That is the only way to test undo
 * meaningfully — an undo entry is only correct if replaying it restores
 * the buffer.
 *
 * Seven symbols are stubbed: status_line (captured, because the
 * "Already at oldest change" message is the only signal that a stack was
 * empty), refresh (counted), and get_one_char, reset_ydreg, showmatching,
 * status_line_bold, status_line_bold_errno, which the interactive paths
 * in buffer.c reference but these tests do not reach.
 *
 * Undo types: a _QUEUED push accumulates single keystrokes in
 * g->undo_queue and only becomes a stack entry at undo_queue_commit.  A
 * _CHAIN push links an entry to the one below it so a single undo applies
 * the whole group.
 */
#include "testutil.h"
#include "undo.h"
#include "buffer.h"
#include "pt.h"
#include "text.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static struct editor g;
static int refreshes;
static char status_last[256];
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
    (void)x;
    (void)format;
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
    refreshes++;
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

    if (n) {
        copy = malloc(n + 1);
        if (!copy)
            abort();
        memcpy(copy, content, n + 1);
    }
    flush_undo_data(&g);
    ptfree(g.pt);
    memset(&g, 0, sizeof(g));
    g.pt                     = ptnew(copy, n);
    g.tabstop                = 8;
    g.undo_queue_state       = UNDO_EMPTY;
    g.term_orig.c_cc[VERASE] = 127;
    refreshes                = 0;
    status_calls             = 0;
    status_last[0]           = '\0';
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

static int
stack_depth(void)
{
    struct undo_object *e;
    int n = 0;

    for (e = g.undo_stack_tail; e; e = e->prev)
        n++;
    return n;
}

static int
redo_depth(void)
{
    struct undo_object *e;
    int n = 0;

    for (e = g.redo_stack_tail; e; e = e->prev)
        n++;
    return n;
}

/* ---- undoing an insert --------------------------------------------------- */

static void
test_undo_insert(void)
{
    init("abc\n");
    string_insert(&g, 1, "XYZ", ALLOW_UNDO);
    CHECK(strcmp(dump(), "aXYZbc\n") == 0);
    CHECK(stack_depth() == 1);

    undo_pop(&g);
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(stack_depth() == 0);
    /* The cursor is left where the change was. */
    CHECK(g.dot == 1);
    /* The screen is redrawn once per applied group. */
    CHECK(refreshes == 1);
}

static void
test_undo_insert_at_end(void)
{
    init("abc\n");
    string_insert(&g, 4, "Z", ALLOW_UNDO);
    CHECK(strcmp(dump(), "abc\nZ") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

/* ---- undoing a delete ---------------------------------------------------- */

static void
test_undo_delete(void)
{
    init("abcdef\n");
    text_hole_delete(&g, 1, 3, ALLOW_UNDO);
    CHECK(strcmp(dump(), "aef\n") == 0);

    undo_pop(&g);
    /* The deleted bytes are restored exactly, in place. */
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(g.dot == 1);
}

static void
test_undo_delete_single_byte(void)
{
    init("abc\n");
    text_hole_delete(&g, 1, 1, ALLOW_UNDO);
    CHECK(strcmp(dump(), "ac\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_undo_delete_whole_buffer(void)
{
    init("abc\n");
    /*
     * A DEL record that spans the entire live buffer is stored one byte
     * short: undo_record drops the last byte when length == text_len.
     * So undoing a whole-buffer delete restores all but one byte.
     */
    text_hole_delete(&g, 0, 3, ALLOW_UNDO);
    CHECK(text_len(&g) == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abc") == 0);
}

/* ---- the queue: many keystrokes, one undo -------------------------------- */

static void
test_queued_inserts_bundle(void)
{
    int p;

    init("\n");
    /* Typing three characters in insert mode. */
    p = 0;
    p = char_insert(&g, p, 'a', ALLOW_UNDO_QUEUED);
    p = char_insert(&g, p, 'b', ALLOW_UNDO_QUEUED);
    char_insert(&g, p, 'c', ALLOW_UNDO_QUEUED);
    CHECK(strcmp(dump(), "abc\n") == 0);
    /* Nothing is on the stack yet: the bytes are still in the queue. */
    CHECK(stack_depth() == 0);
    CHECK(g.undo_q == 3);

    undo_queue_commit(&g);
    CHECK(stack_depth() == 1);
    CHECK(g.undo_q == 0);
    CHECK(g.undo_queue_state == UNDO_EMPTY);

    /* One undo removes all three. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "\n") == 0);
}

static void
test_queued_deletes_bundle(void)
{
    init("abcd\n");
    /* Three backspaces in insert mode. */
    text_hole_delete(&g, 3, 3, ALLOW_UNDO_QUEUED);
    text_hole_delete(&g, 2, 2, ALLOW_UNDO_QUEUED);
    text_hole_delete(&g, 1, 1, ALLOW_UNDO_QUEUED);
    CHECK(strcmp(dump(), "a\n") == 0);
    CHECK(stack_depth() == 0);

    undo_queue_commit(&g);
    CHECK(stack_depth() == 1);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcd\n") == 0);
}

static void
test_queue_flushes_on_direction_change(void)
{
    init("ab\n");
    /* An insert queue followed by a delete commits the insert group
     * first, so the two do not merge into one entry. */
    string_insert(&g, 2, "X", ALLOW_UNDO_QUEUED);
    CHECK(stack_depth() == 0);
    text_hole_delete(&g, 0, 0, ALLOW_UNDO_QUEUED);
    CHECK(stack_depth() == 1);
    undo_queue_commit(&g);
    CHECK(stack_depth() == 2);
}

static void
test_queue_commit_when_empty(void)
{
    init("abc\n");
    /* Nothing queued: a commit is a no-op and pushes nothing. */
    undo_queue_commit(&g);
    CHECK(stack_depth() == 0);
    undo_queue_commit(&g);
    CHECK(stack_depth() == 0);
}

static void
test_queued_delete_rejects_multibyte_length(void)
{
    init("abcdef\n");
    /* The delete queue holds one byte per push, so a longer queued
     * delete is dropped rather than recorded wrongly. */
    undo_push(&g, 1, 3, UNDO_DEL_QUEUED);
    CHECK(g.undo_q == 0);
    CHECK(stack_depth() == 0);
}

/* ---- chained groups: one undo for a replace ------------------------------ */

static void
test_chain_replace(void)
{
    init("abcdef\n");
    /* buffer_replace_range pushes a delete and then a chained insert. */
    buffer_replace_range(&g, 1, 3, "XY", 2);
    CHECK(strcmp(dump(), "aXYef\n") == 0);
    CHECK(stack_depth() == 2);

    /* A single undo reverses both halves. */
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(stack_depth() == 0);
    /* The group is one redraw, not two. */
    CHECK(refreshes == 1);
}

static void
test_chain_of_deletes(void)
{
    init("abcdef\n");
    /*
     * A counted delete (3x) pushes one DEL followed by two DEL_CHAIN
     * entries, so the chain flag has to be read from the entry being
     * applied: a single undo must restore all three bytes.
     */
    text_hole_delete(&g, 1, 1, ALLOW_UNDO);
    text_hole_delete(&g, 1, 1, ALLOW_UNDO_CHAIN);
    text_hole_delete(&g, 1, 1, ALLOW_UNDO_CHAIN);
    CHECK(strcmp(dump(), "aef\n") == 0);
    CHECK(stack_depth() == 3);

    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(stack_depth() == 0);
    /* One group, one redraw. */
    CHECK(refreshes == 1);
}

static void
test_chain_of_deletes_redoes(void)
{
    init("abcdef\n");
    text_hole_delete(&g, 1, 1, ALLOW_UNDO);
    text_hole_delete(&g, 1, 1, ALLOW_UNDO_CHAIN);
    undo_with_redo(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    redo_pop(&g);
    CHECK(strcmp(dump(), "adef\n") == 0);
}

static void
test_chain_stops_at_unchained_entry(void)
{
    init("abcdef\n");
    /* An earlier, unrelated edit must survive the undo of the replace. */
    string_insert(&g, 0, "Z", ALLOW_UNDO);
    buffer_replace_range(&g, 2, 4, "XY", 2);
    CHECK(strcmp(dump(), "ZaXYef\n") == 0);
    CHECK(stack_depth() == 3);

    undo_pop(&g);
    CHECK(strcmp(dump(), "Zabcdef\n") == 0);
    CHECK(stack_depth() == 1);

    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
}

/* ---- several undos in order ---------------------------------------------- */

static void
test_undo_is_lifo(void)
{
    init("abc\n");
    string_insert(&g, 0, "1", ALLOW_UNDO);
    string_insert(&g, 0, "2", ALLOW_UNDO);
    string_insert(&g, 0, "3", ALLOW_UNDO);
    CHECK(strcmp(dump(), "321abc\n") == 0);

    undo_pop(&g);
    CHECK(strcmp(dump(), "21abc\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "1abc\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_undo_empty_stack(void)
{
    init("abc\n");
    undo_pop(&g);
    /* Nothing to undo: the text is untouched and the user is told. */
    CHECK(strcmp(dump(), "abc\n") == 0);
    CHECK(strstr(status_last, "Already at oldest change") != NULL);
    /* No redraw, because nothing was applied. */
    CHECK(refreshes == 0);
}

/* ---- NO_UNDO and UNDO_EMPTY push nothing --------------------------------- */

static void
test_no_undo_records_nothing(void)
{
    init("abc\n");
    string_insert(&g, 1, "X", NO_UNDO);
    text_hole_delete(&g, 0, 0, NO_UNDO);
    CHECK(stack_depth() == 0);

    /* UNDO_EMPTY is the queue's idle marker, not an operation. */
    undo_push(&g, 0, 1, UNDO_EMPTY);
    CHECK(stack_depth() == 0);
}

/* ---- redo ---------------------------------------------------------------- */

static void
test_undo_with_redo(void)
{
    init("abc\n");
    string_insert(&g, 1, "XYZ", ALLOW_UNDO);
    CHECK(strcmp(dump(), "aXYZbc\n") == 0);

    undo_with_redo(&g);
    CHECK(strcmp(dump(), "abc\n") == 0);
    /* The inverse is kept so it can be replayed. */
    CHECK(redo_depth() == 1);

    redo_pop(&g);
    CHECK(strcmp(dump(), "aXYZbc\n") == 0);
    CHECK(redo_depth() == 0);
    /* And the entry is back on the undo stack. */
    CHECK(stack_depth() == 1);
}

static void
test_undo_with_redo_delete(void)
{
    init("abcdef\n");
    text_hole_delete(&g, 1, 3, ALLOW_UNDO);
    undo_with_redo(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    redo_pop(&g);
    CHECK(strcmp(dump(), "aef\n") == 0);
}

static void
test_redo_of_a_chain(void)
{
    init("abcdef\n");
    buffer_replace_range(&g, 1, 3, "XY", 2);
    CHECK(strcmp(dump(), "aXYef\n") == 0);

    undo_with_redo(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(redo_depth() == 2);

    /*
     * Both halves of the group must replay.  The inverse group is pushed
     * in the opposite order, so the chain flags are recomputed rather
     * than copied; copying them would leave the redo deleting the range
     * and never re-inserting it.
     */
    redo_pop(&g);
    CHECK(strcmp(dump(), "aXYef\n") == 0);
    CHECK(redo_depth() == 0);
}

static void
test_undo_pop_discards_redo(void)
{
    init("abc\n");
    string_insert(&g, 1, "X", ALLOW_UNDO);
    /* Plain 'u' saves no inverse, so there is nothing to redo. */
    undo_pop(&g);
    CHECK(redo_depth() == 0);
    redo_pop(&g);
    CHECK(strstr(status_last, "Already at newest change") != NULL);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_new_edit_clears_redo(void)
{
    init("abc\n");
    string_insert(&g, 1, "X", ALLOW_UNDO);
    undo_with_redo(&g);
    CHECK(redo_depth() == 1);

    /* Any fresh edit invalidates the redo history. */
    string_insert(&g, 0, "Z", ALLOW_UNDO);
    CHECK(redo_depth() == 0);
}

static void
test_redo_empty_stack(void)
{
    init("abc\n");
    redo_pop(&g);
    CHECK(strstr(status_last, "Already at newest change") != NULL);
    CHECK(strcmp(dump(), "abc\n") == 0);
}

static void
test_undo_redo_round_trips(void)
{
    int i;

    init("abcdef\n");
    string_insert(&g, 1, "XY", ALLOW_UNDO);
    /* Undo and redo repeatedly: the text must come back each time. */
    for (i = 0; i < 4; i++) {
        undo_with_redo(&g);
        CHECK(strcmp(dump(), "abcdef\n") == 0);
        redo_pop(&g);
        CHECK(strcmp(dump(), "aXYbcdef\n") == 0);
    }
}

/* ---- flush_undo_data ----------------------------------------------------- */

static void
test_flush_undo_data(void)
{
    init("abc\n");
    string_insert(&g, 1, "X", ALLOW_UNDO);
    string_insert(&g, 1, "Y", ALLOW_UNDO);
    undo_with_redo(&g);
    CHECK(stack_depth() == 1);
    CHECK(redo_depth() == 1);

    flush_undo_data(&g);
    CHECK(stack_depth() == 0);
    CHECK(redo_depth() == 0);
    /* The text is not touched: only the history goes. */
    CHECK(strcmp(dump(), "aXbc\n") == 0);

    undo_pop(&g);
    CHECK(strstr(status_last, "Already at oldest change") != NULL);
}

/* ---- UNDO_SWAP ----------------------------------------------------------- */

static void
test_undo_swap(void)
{
    init("abcdef\n");
    /* A swap records the bytes it is about to overwrite, then the caller
     * overwrites them in place; length never changes. */
    undo_push(&g, 1, 3, UNDO_SWAP);
    text_set_bytes(&g, 1, "XYZ", 3);
    CHECK(strcmp(dump(), "aXYZef\n") == 0);
    CHECK(stack_depth() == 1);

    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    CHECK(text_len(&g) == 7);
}

static void
test_undo_swap_round_trip(void)
{
    init("abcdef\n");
    undo_push(&g, 1, 3, UNDO_SWAP);
    text_set_bytes(&g, 1, "XYZ", 3);
    undo_with_redo(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);
    redo_pop(&g);
    CHECK(strcmp(dump(), "aXYZef\n") == 0);
}

/* ---- the undofile sidecar ------------------------------------------------ */

static void
mkfile(char *path, size_t pathsz, const char *content)
{
    int fd;
    size_t n = strlen(content);

    snprintf(path, pathsz, "/tmp/vic_test_undo_XXXXXX");
    fd = mkstemp(path);
    if (fd < 0)
        abort();
    if (n && write(fd, content, n) != (ssize_t)n)
        abort();
    close(fd);
}

/* The sidecar path undo.c derives: "/dir/.basename.vundo". */
static void
sidecar(char *out, size_t outsz, const char *fn)
{
    const char *slash = strrchr(fn, '/');
    const char *base  = slash ? slash + 1 : fn;

    snprintf(out, outsz, "%.*s.%s.vundo", (int)(base - fn), fn, base);
}

static int
exists(const char *path)
{
    return access(path, F_OK) == 0;
}

static void
test_undofile_round_trip(void)
{
    char path[64];
    char side[128];

    mkfile(path, sizeof(path), "abcdef\n");
    init("abcdef\n");
    g.setops |= VI_UNDOFILE;
    sidecar(side, sizeof(side), path);

    /* Two edits, then persist. */
    string_insert(&g, 1, "XY", ALLOW_UNDO);
    text_hole_delete(&g, 0, 0, ALLOW_UNDO);
    CHECK(stack_depth() == 2);
    undo_save(&g, path);
    CHECK(exists(side));

    /* A new session over the same text loads the history back. */
    flush_undo_data(&g);
    CHECK(stack_depth() == 0);
    undo_load(&g, path);
    CHECK(stack_depth() == 2);

    /* And it replays in the right order. */
    CHECK(strcmp(dump(), "XYbcdef\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "aXYbcdef\n") == 0);
    undo_pop(&g);
    CHECK(strcmp(dump(), "abcdef\n") == 0);

    unlink(side);
    unlink(path);
}

static void
test_undofile_off_by_default(void)
{
    char path[64];
    char side[128];

    mkfile(path, sizeof(path), "abcdef\n");
    init("abcdef\n");
    sidecar(side, sizeof(side), path);

    /* Without VI_UNDOFILE nothing is written and nothing is read. */
    string_insert(&g, 1, "XY", ALLOW_UNDO);
    undo_save(&g, path);
    CHECK(!exists(side));

    flush_undo_data(&g);
    undo_load(&g, path);
    CHECK(stack_depth() == 0);

    unlink(path);
}

static void
test_undofile_empty_stack_removes_sidecar(void)
{
    char path[64];
    char side[128];

    mkfile(path, sizeof(path), "abcdef\n");
    init("abcdef\n");
    g.setops |= VI_UNDOFILE;
    sidecar(side, sizeof(side), path);

    string_insert(&g, 1, "XY", ALLOW_UNDO);
    undo_save(&g, path);
    CHECK(exists(side));

    /* Saving with no history clears the stale sidecar rather than
     * leaving history that no longer applies. */
    flush_undo_data(&g);
    undo_save(&g, path);
    CHECK(!exists(side));

    unlink(path);
}

static void
test_undofile_rejects_changed_buffer(void)
{
    char path[64];
    char side[128];

    mkfile(path, sizeof(path), "abcdef\n");
    init("abcdef\n");
    g.setops |= VI_UNDOFILE;
    sidecar(side, sizeof(side), path);

    string_insert(&g, 1, "XY", ALLOW_UNDO);
    undo_save(&g, path);
    flush_undo_data(&g);

    /* The buffer no longer matches the checksum in the header, so the
     * history is refused rather than applied to the wrong text. */
    string_insert(&g, 0, "Q", NO_UNDO);
    undo_load(&g, path);
    CHECK(stack_depth() == 0);

    unlink(side);
    unlink(path);
}

static void
test_undofile_rejects_changed_file_size(void)
{
    char path[64];
    char side[128];
    int fd;

    mkfile(path, sizeof(path), "abcdef\n");
    init("abcdef\n");
    g.setops |= VI_UNDOFILE;
    sidecar(side, sizeof(side), path);

    string_insert(&g, 1, "XY", ALLOW_UNDO);
    undo_save(&g, path);
    flush_undo_data(&g);

    /* The file on disk grew behind our back. */
    fd = open(path, O_WRONLY | O_APPEND);
    if (fd >= 0) {
        if (write(fd, "extra\n", 6) < 0)
            abort();
        close(fd);
    }
    undo_load(&g, path);
    CHECK(stack_depth() == 0);

    unlink(side);
    unlink(path);
}

static void
test_undofile_missing_and_null(void)
{
    init("abcdef\n");
    g.setops |= VI_UNDOFILE;

    /* No sidecar: silently nothing. */
    undo_load(&g, "/tmp/vic_test_undo_does_not_exist");
    CHECK(stack_depth() == 0);

    /* A NULL or empty name is the unnamed-buffer case. */
    string_insert(&g, 1, "XY", ALLOW_UNDO);
    undo_save(&g, NULL);
    undo_save(&g, "");
    undo_load(&g, NULL);
    CHECK(stack_depth() == 1);
}

static void
test_undofile_garbage_is_refused(void)
{
    char path[64];
    char side[128];
    int fd;

    mkfile(path, sizeof(path), "abcdef\n");
    init("abcdef\n");
    g.setops |= VI_UNDOFILE;
    sidecar(side, sizeof(side), path);

    /* A sidecar without the magic header must not be parsed. */
    fd = open(side, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        if (write(fd, "not a vundo file at all", 23) < 0)
            abort();
        close(fd);
    }
    undo_load(&g, path);
    CHECK(stack_depth() == 0);

    unlink(side);
    unlink(path);
}

int
main(void)
{
    test_undo_insert();
    test_undo_insert_at_end();

    test_undo_delete();
    test_undo_delete_single_byte();
    test_undo_delete_whole_buffer();

    test_queued_inserts_bundle();
    test_queued_deletes_bundle();
    test_queue_flushes_on_direction_change();
    test_queue_commit_when_empty();
    test_queued_delete_rejects_multibyte_length();

    test_chain_replace();
    test_chain_of_deletes();
    test_chain_of_deletes_redoes();
    test_chain_stops_at_unchained_entry();

    test_undo_is_lifo();
    test_undo_empty_stack();
    test_no_undo_records_nothing();

    test_undo_with_redo();
    test_undo_with_redo_delete();
    test_redo_of_a_chain();
    test_undo_pop_discards_redo();
    test_new_edit_clears_redo();
    test_redo_empty_stack();
    test_undo_redo_round_trips();

    test_flush_undo_data();

    test_undo_swap();
    test_undo_swap_round_trip();

    test_undofile_round_trip();
    test_undofile_off_by_default();
    test_undofile_empty_stack_removes_sidecar();
    test_undofile_rejects_changed_buffer();
    test_undofile_rejects_changed_file_size();
    test_undofile_missing_and_null();
    test_undofile_garbage_is_refused();

    SUMMARY();
}
