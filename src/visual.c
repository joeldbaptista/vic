/*
 * visual.c - visual mode state and operator application.
 *
 * Visual mode is represented by g->visual_mode (0 = off, 1 = char, 2 = line)
 * and g->visual_anchor (the fixed end of the selection).
 *
 * visual_enter() / visual_leave() — switch mode on/off
 * visual_get_range()              — compute start/stop/buftype from
 *                                   visual_anchor and dot; called by
 *                                   screen.c for highlighting and by
 *                                   visual_apply_operator
 * visual_apply_operator()         — execute an operator (d, c, y, <, >,
 *                                   ~, !) over the visual selection, then
 *                                   leave visual mode
 *
 * "o" in visual mode swaps dot and anchor (implemented in vi.c, not here).
 */
#include "visual.h"
#include "undo.h"

#include "buffer.h"
#include "codepoint.h"
#include "editcmd.h"
#include "line.h"
#include "motion.h"
#include "operator.h"
#include "status.h"

void
visual_leave(struct editor *g)
{
	if (g->visual_anchor != NULL) {
		char *a = cp_start(g, g->visual_anchor);
		char *b = cp_start(g, g->dot);
		if (g->visual_mode == 2) {
			g->mark[MARK_LT] = begin_line(g, a < b ? a : b);
			g->mark[MARK_GT] = end_line(g, a < b ? b : a);
		} else {
			g->mark[MARK_LT] = a < b ? a : b;
			g->mark[MARK_GT] = a < b ? b : a;
		}
	}
	g->visual_mode = 0;
	g->visual_anchor = NULL;
	g->last_status_cksum = 0;
	g->refresh_last_screenbegin = NULL;
	g->vis_ai_pending = 0;
	g->vis_reg_pending = 0;
}

void
visual_enter(struct editor *g, int linewise)
{
	g->visual_mode = linewise ? 2 : 1;
	g->visual_anchor = linewise ? begin_line(g, g->dot) : g->dot;
	if (linewise)
		g->dot = begin_line(g, g->dot);
	g->last_status_cksum = 0;
}

int
visual_get_range(struct editor *g, char **start, char **stop,
                 int *buftype)
{
	char *a;
	char *b;
	char *s;
	char *e;

	if (!g->visual_mode || g->visual_anchor == NULL)
		return 0;

	a = cp_start(g, g->visual_anchor);
	b = cp_start(g, g->dot);

	if (g->visual_mode == 2) {
		s = a < b ? a : b;
		e = a < b ? b : a;
		*start = begin_line(g, s);
		*stop = end_line(g, e);
		*buftype = WHOLE;
		return 1;
	}

	s = a < b ? a : b;
	e = a < b ? b : a;
	e = cp_end(g, e) - 1;
	if (e < s)
		e = s;

	*start = s;
	*stop = e;
	*buftype =
	    memchr(s, '\n', (size_t)(e - s + 1)) ? MULTI : PARTIAL;
	return 1;
}

void
visual_apply_operator(struct editor *g, int op)
{
	char *start;
	char *stop;
	char *saved_reg = g->reg[g->ydreg];
	char *put_copy = NULL;
	int buftype;

	if (!visual_get_range(g, &start, &stop, &buftype)) {
		indicate_error(g);
		return;
	}

	if (op == 'p') {
		if (g->ydreg == SHARED_REG)
			shared_yank_in(g);
		else
			yank_sync_in(g);
		if (g->reg[g->ydreg] == NULL) {
			status_line_bold(g, "Nothing in register %c", what_reg(g));
			return;
		}
		put_copy = xstrdup(g->reg[g->ydreg]);
	}

	visual_leave(g);

	if (op == 'y') {
		text_yank(g, start, stop, g->ydreg, buftype);
		if (g->reg[g->ydreg] != saved_reg)
			yank_status(g, "Yank", g->reg[g->ydreg], 1);
		return;
	}

	if (op == 'p') {
		g->dot = yank_delete_current(g, start, stop, buftype, 1, ALLOW_UNDO);
		g->dot += string_insert(g, g->dot, put_copy, ALLOW_UNDO_CHAIN);
		free(put_copy);
		reset_ydreg(g);
		return;
	}

	if (op == 'U' || op == 'u') {
		char *p;

		/* Save the exact original bytes as a single swap entry.
		 * Undo restores them with one memcpy — no memmove, no realloc. */
		undo_push(g, start, (unsigned)(stop - start + 1), UNDO_SWAP);

		for (p = start; p <= stop; p++) {
			unsigned char ch = (unsigned char)*p;
			if (ch < UTF8_MULTIBYTE_MIN && isalpha(ch))
				*p = (char)(op == 'U' ? toupper(ch) : tolower(ch));
		}

		g->dot = start;
		reset_ydreg(g);
		return;
	}

	if (op == 'x')
		op = 'd';

	g->dot = yank_delete_current(g, start, stop, buftype, 1, ALLOW_UNDO);
	if (op == 'c')
		edit_run_start_insert_cmd(g);
	else if (buftype == WHOLE)
		dot_skip_over_ws(g);

	reset_ydreg(g);
}
