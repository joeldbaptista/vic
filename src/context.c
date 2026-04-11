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
	/*
	 * == Save dot as a context mark before a large jump ==
	 *
	 * When cmd is one of the "far motion" characters (G, /, ?, n, N, H,
	 * L, M, {, }, …), the current dot is pushed into the two-slot context
	 * ring so that '' can return to it.
	 *
	 * - Called before every Normal-mode dispatch; the strchr filter keeps
	 *   it cheap for non-jumping commands.
	 */
	if (strchr(":%{}'GHLMz/?Nn", cmd) != NULL) {
		g->mark[MARK_PREV_CONTEXT] = g->mark[MARK_CONTEXT];
		g->mark[MARK_CONTEXT] = g->dot;
	}
}

char *
swap_context(struct editor *g, char *p)
{
	/*
	 * == Swap dot with the previous context mark ('' command) ==
	 *
	 * Exchanges p with mark[MARK_PREV_CONTEXT] and simultaneously updates
	 * mark[MARK_CONTEXT], so a second '' jumps back to the original spot.
	 *
	 * - Returns p unchanged if mark[MARK_PREV_CONTEXT] is not set or lies
	 *   outside the current buffer bounds.
	 */
	char *tmp;

	if (g->text <= g->mark[MARK_PREV_CONTEXT] &&
	    g->mark[MARK_PREV_CONTEXT] <= g->end - 1) {
		tmp = g->mark[MARK_PREV_CONTEXT];
		g->mark[MARK_PREV_CONTEXT] = p;
		g->mark[MARK_CONTEXT] = p = tmp;
	}

	return p;
}
