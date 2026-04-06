/*
 * motion.c - cursor motion primitives.
 *
 * Implements all commands that move g->dot without editing text:
 *
 *   Character:  dot_left/right, dot_begin/end (0 ^ $ _ |)
 *   Line:       dot_next/prev  (j k)
 *   Screen:     dot_first_screen_line / dot_last_screen_line /
 *               dot_middle_screen_line  (H L M)
 *   Absolute:   motion_goto_line (G nG)
 *   Column:     move_to_col, dot_skip_over_ws
 *   Char-find:  motion_run_find_cmd (f F t T ; ,)
 *
 * Each motion calls undo_queue_commit before moving so that a preceding
 * insert sequence is committed as one undo unit.
 *
 * Upward calls go through motion_hooks.
 */
#include "motion.h"

#include "codepoint.h"
#include "input.h"
#include "line.h"
#include "undo.h"

void
dot_left(struct editor *g)
{
	undo_queue_commit(g);
	g->dot = cp_start(g, g->dot);
	if (g->dot > g->text && g->dot[-1] != '\n')
		g->dot = cp_prev(g, g->dot);
}

void
dot_right(struct editor *g)
{
	undo_queue_commit(g);
	g->dot = cp_start(g, g->dot);
	if (g->dot < g->end - 1 && *g->dot != '\n')
		g->dot = cp_next(g, g->dot);
}

void
dot_begin(struct editor *g)
{
	undo_queue_commit(g);
	g->dot = begin_line(g, g->dot);
}

void
dot_end(struct editor *g)
{
	undo_queue_commit(g);
	g->dot = end_line(g, g->dot);
}

char *
move_to_col(struct editor *g, char *p, int l)
{
	int co;

	p = begin_line(g, p);
	co = 0;
	while (p < g->end) {
		int nco;
		char *next;

		if (*p == '\n')
			break;
		nco = next_column(g, p, co);
		if (nco > l)
			break;
		co = nco;
		next = cp_next(g, p);
		if (next <= p)
			break;
		p = next;
	}
	return p;
}

void
dot_next(struct editor *g)
{
	undo_queue_commit(g);
	g->dot = next_line(g, g->dot);
}

void
dot_prev(struct editor *g)
{
	undo_queue_commit(g);
	g->dot = prev_line(g, g->dot);
}

char *
next_empty_line(struct editor *g, char *p)
{
	p = next_line(g, p);
	while (p < g->end && *p != '\n') {
		char *n = next_line(g, p);
		if (n == p)
			return NULL;
		p = n;
	}
	if (p < g->end && *p == '\n')
		return p;
	return NULL;
}

char *
prev_empty_line(struct editor *g, char *p)
{
	p = prev_line(g, p);
	while (p >= g->text && *p != '\n') {
		char *n = prev_line(g, p);
		if (n == p)
			return NULL;
		p = n;
	}
	if (p >= g->text && *p == '\n')
		return p;
	return NULL;
}

void
dot_skip_over_ws(struct editor *g)
{
	while (g->dot < g->end - 1) {
		unsigned char c = (unsigned char)*g->dot;

		if (!(c < UTF8_MULTIBYTE_MIN && isspace(c)) || c == '\n')
			break;
		g->dot = cp_next(g, g->dot);
	}
}

void
dot_to_char(struct editor *g, int cmd)
{
	char *q = cp_start(g, g->dot);
	int dir = islower(cmd) ? 1 : -1;

	if (g->last_search_char == 0)
		return;

	do {
		do {
			if (dir > 0) {
				q = cp_next(g, q);
				if (q > g->end - 1 || *q == '\n') {
					indicate_error(g);
					return;
				}
			} else {
				if (q <= g->text) {
					indicate_error(g);
					return;
				}
				q = cp_prev(g, q);
				if (*q == '\n') {
					indicate_error(g);
					return;
				}
			}
			if ((unsigned char)*q == (unsigned char)g->last_search_char)
				break;
			if (q == g->text && dir < 0) {
				indicate_error(g);
				return;
			}
		} while (1);
	} while (--g->cmdcnt > 0);

	g->dot = q;

	if (cmd == 't')
		dot_left(g);
	else if (cmd == 'T')
		dot_right(g);
}

void
motion_run_repeat_search_same_cmd(struct editor *g)
{
	dot_to_char(g, g->last_search_cmd);
}

void
motion_run_repeat_search_reverse_cmd(struct editor *g)
{
	dot_to_char(g, g->last_search_cmd ^ 0x20);
}

void
motion_run_find_char_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	if (ctx && ctx->anchor)
		g->last_search_char = (unsigned char)ctx->anchor;
	else
		g->last_search_char = get_one_char(g);
	g->last_search_cmd = ctx ? (int)(unsigned char)ctx->op : 0;
	dot_to_char(g, g->last_search_cmd);
}

void
motion_run_first_nonblank_cmd(struct editor *g)
{
	dot_begin(g);
	dot_skip_over_ws(g);
}

void
motion_run_screen_top_cmd(struct editor *g)
{
	g->dot = g->screenbegin;
	if (g->cmdcnt > (int)(g->rows - 1))
		g->cmdcnt = (int)(g->rows - 1);
	while (--g->cmdcnt > 0)
		dot_next(g);
	dot_begin(g);
	dot_skip_over_ws(g);
}

void
motion_run_screen_bottom_cmd(struct editor *g)
{
	g->dot = end_screen(g);
	if (g->cmdcnt > (int)(g->rows - 1))
		g->cmdcnt = (int)(g->rows - 1);
	while (--g->cmdcnt > 0)
		dot_prev(g);
	dot_begin(g);
	dot_skip_over_ws(g);
}

void
motion_run_screen_middle_cmd(struct editor *g)
{
	int cnt;

	g->dot = g->screenbegin;
	for (cnt = 0; cnt < (int)((g->rows - 1) / 2); cnt++)
		g->dot = next_line(g, g->dot);
	dot_skip_over_ws(g);
}

void
motion_run_goto_column_cmd(struct editor *g)
{
	g->dot = move_to_col(g, g->dot, g->cmdcnt - 1);
}

void
motion_run_goto_line_cmd(struct editor *g)
{
	g->dot = g->end - 1;
	if (g->cmdcnt > 0)
		g->dot = find_line(g, g->cmdcnt);
	dot_begin(g);
	dot_skip_over_ws(g);
}

void
motion_run_left_cmd(struct editor *g)
{
	do {
		dot_left(g);
	} while (--g->cmdcnt > 0);
}

void
motion_run_right_cmd(struct editor *g)
{
	do {
		dot_right(g);
	} while (--g->cmdcnt > 0);
}

void
motion_run_prev_empty_line_cmd(struct editor *g)
{
	char *p;
	char *q = g->dot;

	do {
		p = prev_empty_line(g, q);
		if (p == NULL) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = q;
}

void
motion_run_next_empty_line_cmd(struct editor *g)
{
	char *p;
	char *q = g->dot;

	do {
		p = next_empty_line(g, q);
		if (p == NULL) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = q;
}

void
motion_run_next_line_keep_col_cmd(struct editor *g)
{
	char *p;
	char *q = g->dot;

	do {
		p = next_line(g, q);
		if (p == end_line(g, q)) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = q;
	g->dot = g->cindex < 0 ? end_line(g, g->dot)
	                       : move_to_col(g, g->dot, g->cindex);
	g->keep_index = TRUE;
}

void
motion_run_next_line_skip_ws_cmd(struct editor *g)
{
	char *p;
	char *q = g->dot;

	do {
		p = next_line(g, q);
		if (p == end_line(g, q)) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = q;
	dot_skip_over_ws(g);
}

void
motion_run_prev_line_skip_ws_cmd(struct editor *g)
{
	char *p;
	char *q = g->dot;

	do {
		p = prev_line(g, q);
		if (p == begin_line(g, q)) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = q;
	dot_skip_over_ws(g);
}

void
motion_run_line_end_cmd(struct editor *g)
{
	for (;;) {
		g->dot = end_line(g, g->dot);
		if (--g->cmdcnt <= 0)
			break;
		dot_next(g);
	}
	g->cindex = -1;
	g->keep_index = TRUE;
}

int
motion_run_paragraph_cmd(struct editor *g, int c)
{
	int dir;

	dir = c == '}' ? 1 : -1;
	do {
		int skip = TRUE;
		while (dir == 1 ? g->dot < g->end - 1 : g->dot > g->text) {
			char *adj =
			    (dir == 1) ? cp_next(g, g->dot) : cp_prev(g, g->dot);
			if (*g->dot == '\n' && *adj == '\n') {
				if (!skip) {
					if (dir == 1)
						g->dot = adj;
					goto found_break;
				}
			} else {
				skip = FALSE;
			}
			g->dot = adj;
		}
		return 1;
	found_break:
		continue;
	} while (--g->cmdcnt > 0);

	return 0;
}

void
motion_run_scroll_to_screenpos_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	int c1;
	int cnt = 0;

	c1 = (ctx && ctx->op2) ? (int)(unsigned char)ctx->op2 : get_one_char(g);
	if (c1 == '.')
		cnt = (g->rows - 2) / 2;
	if (c1 == '-')
		cnt = g->rows - 2;
	g->screenbegin = begin_line(g, g->dot);
	dot_scroll(g, cnt, -1);
}

void
dot_scroll(struct editor *g, int cnt, int dir)
{
	char *q;

	undo_queue_commit(g);
	for (; cnt > 0; cnt--) {
		if (dir < 0)
			g->screenbegin = prev_line(g, g->screenbegin);
		else
			g->screenbegin = next_line(g, g->screenbegin);
	}
	if (g->dot < g->screenbegin)
		g->dot = g->screenbegin;
	q = end_screen(g);
	if (g->dot > q)
		g->dot = begin_line(g, q);
	dot_skip_over_ws(g);
}

char *
bound_dot(struct editor *g, char *p)
{
	if (p >= g->end && g->end > g->text) {
		p = g->end - 1;
		indicate_error(g);
	}
	if (p < g->text) {
		p = g->text;
		indicate_error(g);
	}
	if (p > g->text && p < g->end)
		p = cp_start(g, p);
	return p;
}
