/*
 * context.c - jump-context mark management.
 *
 * Tracks the two context marks used by the '' (backtick-backtick) command:
 *   mark[MARK_CONTEXT] — current context (set before a jump)
 *   mark[MARK_PREV_CONTEXT] — previous context
 *
 * check_context saves the current dot into mark[MARK_CONTEXT] before any
 * command that makes a large motion (G, /, ?, n, N, H, L, M, {, }, …).
 * swap_context swaps dot and mark[MARK_PREV_CONTEXT] to implement ''.
 *
 * No hooks, no state beyond the marks already in struct editor.
 */
#include "context.h"

#include <string.h>

void
check_context(struct editor *g, char cmd)
{
	if (strchr(":%{}'GHLMz/?Nn", cmd) != NULL) {
		g->mark[MARK_PREV_CONTEXT] = g->mark[MARK_CONTEXT];
		g->mark[MARK_CONTEXT] = g->dot;
	}
}

char *
swap_context(struct editor *g, char *p)
{
	char *tmp;

	if (g->text <= g->mark[MARK_PREV_CONTEXT] &&
	    g->mark[MARK_PREV_CONTEXT] <= g->end - 1) {
		tmp = g->mark[MARK_PREV_CONTEXT];
		g->mark[MARK_PREV_CONTEXT] = p;
		g->mark[MARK_CONTEXT] = p = tmp;
	}

	return p;
}
