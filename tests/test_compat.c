/*
 * test_compat.c - unit tests for compat.c
 *
 * compat.c is the portability and utility layer: allocation wrappers that
 * abort on OOM, the capacity-growth helper, POSIX I/O helpers, the
 * memrchr/strchrnul shims, and three small string helpers.  It depends on
 * nothing in the editor, so this test links compat.c alone.
 *
 * Not covered here, and why:
 *   die, show_usage                  — they call exit().
 *   get_terminal_width_height,
 *   set_termios_to_raw, safe_poll    — they need a terminal or a poll set
 *                                      that a unit test cannot supply
 *                                      meaningfully.
 *   xmalloc, xzalloc, xrealloc       — the OOM path is an exit(); the
 *                                      success path is tested indirectly
 *                                      through every other function here,
 *                                      and xzalloc's zeroing is checked.
 */
#include "testutil.h"
#include "vic.h"
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- grow_cap ------------------------------------------------------------ */

static void
test_grow_cap(void)
{
    /* Already large enough: unchanged. */
    CHECK(grow_cap(16, 10, 8) == 16);
    CHECK(grow_cap(16, 16, 8) == 16);

    /* Doubling until it fits. */
    CHECK(grow_cap(16, 17, 8) == 32);
    CHECK(grow_cap(16, 33, 8) == 64);
    CHECK(grow_cap(16, 100, 8) == 128);
}

static void
test_grow_cap_from_zero(void)
{
    /* A zero cap starts from min. */
    CHECK(grow_cap(0, 1, 8) == 8);
    CHECK(grow_cap(0, 9, 8) == 16);

    /*
     * A zero cap and a zero min would leave the doubling loop spinning on
     * 0 forever, so cap falls back to 1.
     */
    CHECK(grow_cap(0, 1, 0) == 1);
    CHECK(grow_cap(0, 5, 0) == 8);
    CHECK(grow_cap(0, 0, 0) == 1);
}

static void
test_grow_cap_overflow(void)
{
    /*
     * Doubling past SIZE_MAX/2 wraps to 0 and would spin, so the function
     * returns `need` instead of a wrapped capacity.
     */
    CHECK(grow_cap(SIZE_MAX / 2 + 1, SIZE_MAX, 8) == SIZE_MAX);
    CHECK(grow_cap(SIZE_MAX - 1, SIZE_MAX, 8) == SIZE_MAX);
    /* The result always satisfies the request. */
    CHECK(grow_cap(1, SIZE_MAX, 1) == SIZE_MAX);
}

static void
test_grow_cap_result_covers_need(void)
{
    /* The contract is "returns a capacity >= need", for any input. */
    size_t caps[] = {0, 1, 2, 7, 8, 1024};
    size_t needs[] = {0, 1, 2, 9, 100, 4096};
    size_t ci, ni;

    for (ci = 0; ci < sizeof(caps) / sizeof(caps[0]); ci++)
        for (ni = 0; ni < sizeof(needs) / sizeof(needs[0]); ni++)
            CHECK(grow_cap(caps[ci], needs[ni], 8) >= needs[ni]);
}

/* ---- xzalloc / xstrdup / xstrndup ---------------------------------------- */

static void
test_xzalloc_is_zeroed(void)
{
    unsigned char *p = xzalloc(64);
    int i;
    int all_zero = 1;

    CHECK(p != NULL);
    for (i = 0; i < 64; i++)
        if (p[i] != 0)
            all_zero = 0;
    CHECK(all_zero);
    free(p);
}

static void
test_xstrdup(void)
{
    char *p = xstrdup("hello");

    CHECK(p != NULL);
    CHECK(strcmp(p, "hello") == 0);
    free(p);

    p = xstrdup("");
    CHECK(p != NULL && p[0] == '\0');
    free(p);

    /* NULL in, NULL out: several call sites rely on this rather than
     * guarding every call. */
    CHECK(xstrdup(NULL) == NULL);
}

static void
test_xstrndup(void)
{
    char *p = xstrndup("hello world", 5);

    CHECK(p != NULL);
    CHECK(strcmp(p, "hello") == 0); /* always NUL-terminated */
    free(p);

    /* n past the end of the string copies only the string. */
    p = xstrndup("hi", 10);
    CHECK(strcmp(p, "hi") == 0);
    free(p);

    p = xstrndup("hi", 0);
    CHECK(p != NULL && p[0] == '\0');
    free(p);

    CHECK(xstrndup(NULL, 5) == NULL);
}

/* ---- memrchr ------------------------------------------------------------- */

static void
test_memrchr(void)
{
    const char *s = "abcabc";

    /* The last occurrence, not the first. */
    CHECK(memrchr(s, 'a', 6) == s + 3);
    CHECK(memrchr(s, 'c', 6) == s + 5);
    /* The search is bounded by n, so a shorter n finds the earlier one. */
    CHECK(memrchr(s, 'a', 3) == s + 0);
    CHECK(memrchr(s, 'c', 3) == s + 2);
}

static void
test_memrchr_absent(void)
{
    const char *s = "abcabc";

    CHECK(memrchr(s, 'z', 6) == NULL);
    /* A zero length searches nothing at all. */
    CHECK(memrchr(s, 'a', 0) == NULL);
}

static void
test_memrchr_binary(void)
{
    /* It must not stop at an embedded NUL, unlike the str* functions. */
    const char buf[] = {'a', '\0', 'b', '\0', 'a'};

    CHECK(memrchr(buf, 'a', 5) == buf + 4);
    CHECK(memrchr(buf, '\0', 5) == buf + 3);
    CHECK(memrchr(buf, 'b', 5) == buf + 2);
}

static void
test_memrchr_high_byte(void)
{
    /* The byte is compared as unsigned, so 0xff is findable. */
    const unsigned char buf[] = {0x41, 0xff, 0x42};

    CHECK(memrchr(buf, 0xff, 3) == buf + 1);
}

/* ---- strchrnul ----------------------------------------------------------- */

static void
test_strchrnul(void)
{
    const char *s = "abc";

    CHECK(strchrnul(s, 'a') == s);
    CHECK(strchrnul(s, 'c') == s + 2);
    /* Absent: the terminator, never NULL.  That is the whole point of the
     * function. */
    CHECK(strchrnul(s, 'z') == s + 3);
    CHECK(*strchrnul(s, 'z') == '\0');

    CHECK(strchrnul("", 'a') != NULL);
    CHECK(*strchrnul("", 'a') == '\0');
}

static void
test_strchrnul_for_nul(void)
{
    const char *s = "abc";

    /* Searching for the terminator finds it. */
    CHECK(strchrnul(s, '\0') == s + 3);
}

/* ---- skip_whitespace / skip_non_whitespace ------------------------------- */

static void
test_skip_whitespace(void)
{
    char buf[] = "  \t x y";

    CHECK(skip_whitespace(buf) == buf + 4);
    /* Nothing to skip: unchanged. */
    CHECK(skip_whitespace(buf + 4) == buf + 4);
}

static void
test_skip_whitespace_edges(void)
{
    char empty[] = "";
    char all[]   = "   ";

    CHECK(skip_whitespace(empty) == empty);
    /* All whitespace: it stops on the terminator rather than running off. */
    CHECK(skip_whitespace(all) == all + 3);
    CHECK(*skip_whitespace(all) == '\0');
}

static void
test_skip_non_whitespace(void)
{
    char buf[] = "word rest";

    CHECK(skip_non_whitespace(buf) == buf + 4);
    CHECK(skip_non_whitespace(buf + 4) == buf + 4);
}

static void
test_skip_non_whitespace_edges(void)
{
    char empty[] = "";
    char all[]   = "word";

    CHECK(skip_non_whitespace(empty) == empty);
    CHECK(skip_non_whitespace(all) == all + 4);
    CHECK(*skip_non_whitespace(all) == '\0');
}

static void
test_skip_pair_walks_a_line(void)
{
    /* The two together are how ex.c splits an argument list. */
    char buf[] = "  one two  three";
    char *p    = buf;

    p = skip_whitespace(p);
    CHECK(strncmp(p, "one", 3) == 0);
    p = skip_non_whitespace(p);
    p = skip_whitespace(p);
    CHECK(strncmp(p, "two", 3) == 0);
    p = skip_non_whitespace(p);
    p = skip_whitespace(p);
    CHECK(strcmp(p, "three") == 0);
    p = skip_non_whitespace(p);
    CHECK(*p == '\0');
}

/* ---- index_in_strings ---------------------------------------------------- */

#define TABLE "alpha\0beta\0gamma\0"

static void
test_index_in_strings(void)
{
    CHECK(index_in_strings(TABLE, "alpha") == 0);
    CHECK(index_in_strings(TABLE, "beta") == 1);
    CHECK(index_in_strings(TABLE, "gamma") == 2);
}

static void
test_index_in_strings_absent(void)
{
    CHECK(index_in_strings(TABLE, "delta") == -1);
    /* The match is exact: no prefix and no case folding. */
    CHECK(index_in_strings(TABLE, "alph") == -1);
    CHECK(index_in_strings(TABLE, "alphas") == -1);
    CHECK(index_in_strings(TABLE, "ALPHA") == -1);
    CHECK(index_in_strings(TABLE, "") == -1);
}

static void
test_index_in_strings_null(void)
{
    CHECK(index_in_strings(NULL, "alpha") == -1);
    CHECK(index_in_strings(TABLE, NULL) == -1);
    CHECK(index_in_strings(NULL, NULL) == -1);
}

static void
test_index_in_strings_empty_table(void)
{
    CHECK(index_in_strings("\0", "alpha") == -1);
}

static void
test_index_in_strings_on_the_real_table(void)
{
    /* The option table is what this function exists for, so check a few
     * entries of it.  The index is the position in OPTS_STR, which
     * excore.c halves to get the flag bit. */
    CHECK(index_in_strings(OPTS_STR, "ai") == 0);
    CHECK(index_in_strings(OPTS_STR, "autoindent") == 1);
    CHECK(index_in_strings(OPTS_STR, "et") == 2);
    CHECK(index_in_strings(OPTS_STR, "nosuchoption") == -1);
}

/* ---- full_read / full_write ---------------------------------------------- */

static void
mkfile(char *path, size_t pathsz, const char *content)
{
    int fd;
    size_t n = strlen(content);

    snprintf(path, pathsz, "/tmp/vic_test_compat_XXXXXX");
    fd = mkstemp(path);
    if (fd < 0)
        abort();
    if (n && write(fd, content, n) != (ssize_t)n)
        abort();
    close(fd);
}

static void
test_full_read(void)
{
    char path[64];
    char buf[32];
    int fd;

    mkfile(path, sizeof(path), "hello world");
    fd = open(path, O_RDONLY);
    CHECK(fd >= 0);
    memset(buf, '?', sizeof(buf));
    CHECK(full_read(fd, buf, 11) == 11);
    CHECK(memcmp(buf, "hello world", 11) == 0);
    close(fd);

    /* Asking for more than the file holds returns what there was. */
    fd = open(path, O_RDONLY);
    CHECK(full_read(fd, buf, 32) == 11);
    close(fd);

    /* At EOF it returns 0 rather than blocking or erroring. */
    fd = open(path, O_RDONLY);
    full_read(fd, buf, 11);
    CHECK(full_read(fd, buf, 4) == 0);
    close(fd);

    unlink(path);
}

static void
test_full_read_bad_fd(void)
{
    char buf[8];

    CHECK(full_read(-1, buf, 4) < 0);
}

static void
test_full_write(void)
{
    char path[64];
    char buf[32];
    int fd;

    mkfile(path, sizeof(path), "");
    fd = open(path, O_WRONLY);
    CHECK(fd >= 0);
    CHECK(full_write(fd, "abcdef", 6) == 6);
    close(fd);

    fd = open(path, O_RDONLY);
    CHECK(full_read(fd, buf, 6) == 6);
    CHECK(memcmp(buf, "abcdef", 6) == 0);
    close(fd);

    unlink(path);
}

static void
test_full_write_bad_fd(void)
{
    CHECK(full_write(-1, "abc", 3) < 0);
}

/* ---- drain_fd ------------------------------------------------------------ */

static void
test_drain_fd(void)
{
    char path[64];
    size_t len = 999;
    char *buf;
    int fd;

    mkfile(path, sizeof(path), "one\ntwo\n");
    fd  = open(path, O_RDONLY);
    buf = drain_fd(fd, &len);
    close(fd);
    CHECK(buf != NULL);
    CHECK(len == 8);
    CHECK(strcmp(buf, "one\ntwo\n") == 0); /* NUL-terminated */
    free(buf);
    unlink(path);
}

static void
test_drain_fd_empty(void)
{
    char path[64];
    size_t len = 999;
    char *buf;
    int fd;

    mkfile(path, sizeof(path), "");
    fd  = open(path, O_RDONLY);
    buf = drain_fd(fd, &len);
    close(fd);
    CHECK(buf != NULL);
    CHECK(len == 0);
    CHECK(buf[0] == '\0');
    free(buf);
    unlink(path);
}

static void
test_drain_fd_large(void)
{
    /* Larger than the 4096-byte starting capacity, so the growth path in
     * drain_fd runs. */
    char path[64];
    char big[20000];
    size_t len = 0;
    char *buf;
    int fd;

    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    mkfile(path, sizeof(path), big);
    fd  = open(path, O_RDONLY);
    buf = drain_fd(fd, &len);
    close(fd);
    CHECK(buf != NULL);
    CHECK(len == sizeof(big) - 1);
    CHECK(strlen(buf) == sizeof(big) - 1);
    free(buf);
    unlink(path);
}

static void
test_drain_fd_bad_fd(void)
{
    size_t len = 999;

    /* A read error gives NULL, never a partial capture: callers use the
     * result to replace existing text. */
    CHECK(drain_fd(-1, &len) == NULL);
}

/* ---- xmalloc_open_read_close --------------------------------------------- */

static void
test_open_read_close(void)
{
    char path[64];
    size_t size = 999;
    char *buf;

    mkfile(path, sizeof(path), "file content\n");
    buf = xmalloc_open_read_close(path, &size);
    CHECK(buf != NULL);
    CHECK(size == 13);
    CHECK(strcmp(buf, "file content\n") == 0);
    free(buf);

    /* sizep is optional. */
    buf = xmalloc_open_read_close(path, NULL);
    CHECK(buf != NULL);
    CHECK(strcmp(buf, "file content\n") == 0);
    free(buf);

    unlink(path);
}

static void
test_open_read_close_empty(void)
{
    char path[64];
    size_t size = 999;
    char *buf;

    mkfile(path, sizeof(path), "");
    buf = xmalloc_open_read_close(path, &size);
    CHECK(buf != NULL);
    CHECK(size == 0);
    CHECK(buf[0] == '\0');
    free(buf);
    unlink(path);
}

static void
test_open_read_close_missing(void)
{
    size_t size = 999;

    CHECK(xmalloc_open_read_close("/no/such/file", &size) == NULL);
    /* size is left alone on failure. */
    CHECK(size == 999);
}

static void
test_open_read_close_large(void)
{
    /* The size comes from fstat, so the read loop normally fills the
     * buffer exactly; this checks a file bigger than one read. */
    char path[64];
    char big[20000];
    size_t size = 0;
    char *buf;

    memset(big, 'y', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    mkfile(path, sizeof(path), big);
    buf = xmalloc_open_read_close(path, &size);
    CHECK(buf != NULL);
    CHECK(size == sizeof(big) - 1);
    CHECK(strlen(buf) == sizeof(big) - 1);
    free(buf);
    unlink(path);
}

/* ---- safe_read ----------------------------------------------------------- */

static void
test_safe_read(void)
{
    char path[64];
    char buf[32];
    int fd;

    mkfile(path, sizeof(path), "abc");
    fd = open(path, O_RDONLY);
    CHECK(safe_read(fd, buf, sizeof(buf)) == 3);
    /* At EOF, 0. */
    CHECK(safe_read(fd, buf, sizeof(buf)) == 0);
    close(fd);
    unlink(path);

    CHECK(safe_read(-1, buf, 4) < 0);
}

int
main(void)
{
    test_grow_cap();
    test_grow_cap_from_zero();
    test_grow_cap_overflow();
    test_grow_cap_result_covers_need();

    test_xzalloc_is_zeroed();
    test_xstrdup();
    test_xstrndup();

    test_memrchr();
    test_memrchr_absent();
    test_memrchr_binary();
    test_memrchr_high_byte();

    test_strchrnul();
    test_strchrnul_for_nul();

    test_skip_whitespace();
    test_skip_whitespace_edges();
    test_skip_non_whitespace();
    test_skip_non_whitespace_edges();
    test_skip_pair_walks_a_line();

    test_index_in_strings();
    test_index_in_strings_absent();
    test_index_in_strings_null();
    test_index_in_strings_empty_table();
    test_index_in_strings_on_the_real_table();

    test_full_read();
    test_full_read_bad_fd();
    test_full_write();
    test_full_write_bad_fd();

    test_drain_fd();
    test_drain_fd_empty();
    test_drain_fd_large();
    test_drain_fd_bad_fd();

    test_open_read_close();
    test_open_read_close_empty();
    test_open_read_close_missing();
    test_open_read_close_large();

    test_safe_read();

    SUMMARY();
}
