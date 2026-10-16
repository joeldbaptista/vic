/*
 * test_context.c - unit tests for context.c
 *
 * context.c owns the two-slot jump-context ring, mark[MARK_CONTEXT] and
 * mark[MARK_PREV_CONTEXT], which is what the '' command jumps through.
 * It touches nothing but those two marks, g->dot and text_len, so this
 * test links the real piece tree and needs no stubs.
 */
#include "testutil.h"
#include "context.h"
#include "pt.h"
#include "text.h"
#include <stdlib.h>
#include <string.h>

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
    memset(&g, 0, sizeof(g));
    g.pt                       = ptnew(copy, n);
    g.mark[MARK_CONTEXT]       = -1;
    g.mark[MARK_PREV_CONTEXT]  = -1;
}

/* ---- check_context ------------------------------------------------------- */

static void
test_check_context_jump_commands(void)
{
    /* The complete set of characters context.c treats as a far motion. */
    static const char jumps[] = ":%{}'GHLMz/?Nn";
    size_t i;

    for (i = 0; i < sizeof(jumps) - 1; i++) {
        init("abc\ndef\n");
        g.dot = 5;
        check_context(&g, jumps[i]);
        CHECK(g.mark[MARK_CONTEXT] == 5);
        CHECK(g.mark[MARK_PREV_CONTEXT] == -1);
    }
}

static void
test_check_context_other_commands(void)
{
    /* A command that is not a far motion leaves both marks alone. */
    static const char others[] = "wbeiaxdyjklhpPuU0$";
    size_t i;

    for (i = 0; i < sizeof(others) - 1; i++) {
        init("abc\ndef\n");
        g.dot                = 5;
        g.mark[MARK_CONTEXT] = 2;
        check_context(&g, others[i]);
        CHECK(g.mark[MARK_CONTEXT] == 2);
        CHECK(g.mark[MARK_PREV_CONTEXT] == -1);
    }
}

static void
test_check_context_shifts_the_ring(void)
{
    init("abc\ndef\n");
    /* Each far motion pushes the old current context into the previous
     * slot, so the ring holds the last two jump origins. */
    g.dot = 1;
    check_context(&g, 'G');
    CHECK(g.mark[MARK_CONTEXT] == 1);
    CHECK(g.mark[MARK_PREV_CONTEXT] == -1);

    g.dot = 5;
    check_context(&g, 'G');
    CHECK(g.mark[MARK_CONTEXT] == 5);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 1);

    g.dot = 7;
    check_context(&g, '/');
    CHECK(g.mark[MARK_CONTEXT] == 7);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 5);
    /* Only two slots, so the first origin has fallen out of the ring. */
}

static void
test_check_context_dot_zero(void)
{
    init("abc\n");
    g.dot = 0;
    check_context(&g, 'G');
    /* 0 is a valid position, not "unset". */
    CHECK(g.mark[MARK_CONTEXT] == 0);
}

/* ---- swap_context -------------------------------------------------------- */

static void
test_swap_context(void)
{
    init("abc\ndef\n"); /* len 8, so valid positions are 0..7 */
    g.mark[MARK_PREV_CONTEXT] = 2;
    /* '' jumps to the previous context and leaves the departure point
     * behind, so a second '' comes back. */
    CHECK(swap_context(&g, 6) == 2);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 6);
    /* The current-context slot also moves, which is what makes the jump
     * itself recordable. */
    CHECK(g.mark[MARK_CONTEXT] == 2);

    CHECK(swap_context(&g, 2) == 6);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 2);
}

static void
test_swap_context_unset(void)
{
    init("abc\ndef\n");
    /* No previous context: p comes back unchanged and nothing is set. */
    g.mark[MARK_PREV_CONTEXT] = -1;
    CHECK(swap_context(&g, 6) == 6);
    CHECK(g.mark[MARK_PREV_CONTEXT] == -1);
    CHECK(g.mark[MARK_CONTEXT] == -1);
}

static void
test_swap_context_out_of_bounds(void)
{
    init("abc\ndef\n"); /* len 8 */
    /* A mark past the end of the buffer is refused rather than followed:
     * the buffer may have shrunk since the mark was taken. */
    g.mark[MARK_PREV_CONTEXT] = 8;
    CHECK(swap_context(&g, 6) == 6);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 8);

    g.mark[MARK_PREV_CONTEXT] = 99;
    CHECK(swap_context(&g, 6) == 6);

    /* The last valid byte is accepted. */
    g.mark[MARK_PREV_CONTEXT] = 7;
    CHECK(swap_context(&g, 6) == 7);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 6);
}

static void
test_swap_context_zero_mark(void)
{
    init("abc\n");
    /* Offset 0 is in bounds, so the swap must happen. */
    g.mark[MARK_PREV_CONTEXT] = 0;
    CHECK(swap_context(&g, 2) == 0);
    CHECK(g.mark[MARK_PREV_CONTEXT] == 2);
    CHECK(g.mark[MARK_CONTEXT] == 0);
}

static void
test_swap_context_empty_buffer(void)
{
    init("");
    /* text_len - 1 is -1, so no mark can be in bounds. */
    g.mark[MARK_PREV_CONTEXT] = 0;
    CHECK(swap_context(&g, 0) == 0);
    CHECK(g.mark[MARK_CONTEXT] == -1);
}

static void
test_check_then_swap(void)
{
    init("abcdefgh\n");
    /* The two functions compose: a far motion records the origin, and ''
     * returns to it. */
    g.dot = 1;
    check_context(&g, 'G');
    g.dot = 8;
    check_context(&g, 'G'); /* now PREV = 1, CONTEXT = 8 */
    CHECK(swap_context(&g, 8) == 1);
}

int
main(void)
{
    test_check_context_jump_commands();
    test_check_context_other_commands();
    test_check_context_shifts_the_ring();
    test_check_context_dot_zero();

    test_swap_context();
    test_swap_context_unset();
    test_swap_context_out_of_bounds();
    test_swap_context_zero_mark();
    test_swap_context_empty_buffer();

    test_check_then_swap();

    SUMMARY();
}
