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
	/*
	 * == Move cursor left one codepoint ==
	 *
	 * Commits any pending undo queue, then retreats dot by one codepoint.
	 * Does not cross the preceding newline (stays on the current line).
	 */
	undo_queue_commit(g);
	g->dot = cp_start(g, g->dot);
	if (g->dot > g->text && g->dot[-1] != '\n')
		g->dot = cp_prev(g, g->dot);
}

void
dot_right(struct editor *g)
{
	/*
	 * == Move cursor right one codepoint ==
	 *
	 * Commits any pending undo queue, then advances dot by one codepoint.
	 * Does not cross the trailing newline of the current line.
	 */
	undo_queue_commit(g);
	g->dot = cp_start(g, g->dot);
	if (g->dot < g->end - 1 && *g->dot != '\n')
		g->dot = cp_next(g, g->dot);
}

void
dot_begin(struct editor *g)
{
	/*
	 * == Move cursor to the first byte of the current line ==
	 */
	undo_queue_commit(g);
	g->dot = begin_line(g, g->dot);
}

void
dot_end(struct editor *g)
{
	/*
	 * == Move cursor to the newline terminating the current line ==
	 */
	undo_queue_commit(g);
	g->dot = end_line(g, g->dot);
}

char *
move_to_col(struct editor *g, char *p, int l)
{
	/*
	 * == Walk p to the target column l on its line ==
	 *
	 * Starting from begin_line, advances codepoint by codepoint and stops
	 * at the last position whose column does not exceed l.  Handles tabs
	 * and wide codepoints correctly via next_column.
	 *
	 * - Never overshoots into the trailing newline.
	 * - Returns the closest reachable position when l is past end-of-line.
	 */
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
	/*
	 * == Move cursor to the start of the next line ==
	 */
	undo_queue_commit(g);
	g->dot = next_line(g, g->dot);
}

void
dot_prev(struct editor *g)
{
	/*
	 * == Move cursor to the start of the previous line ==
	 */
	undo_queue_commit(g);
	g->dot = prev_line(g, g->dot);
}

char *
next_empty_line(struct editor *g, char *p)
{
	/*
	 * == Pointer to the next empty line after p ==
	 *
	 * An empty line is one whose only byte is '\n'.  Used by the }
	 * paragraph-forward motion.  Returns NULL when no empty line exists
	 * after p.
	 */
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
	/*
	 * == Pointer to the previous empty line before p ==
	 *
	 * An empty line is one whose only byte is '\n'.  Used by the {
	 * paragraph-backward motion.  Returns NULL when no empty line exists
	 * before p.
	 */
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
	/*
	 * == Advance dot past any leading ASCII whitespace ==
	 *
	 * Steps dot forward over space/tab characters, stopping before
	 * newlines and non-whitespace.  Called after line-motion commands to
	 * land the cursor on the first non-blank column.
	 */
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
	/*
	 * == Move dot to the next/previous occurrence of g->last_search_char ==
	 *
	 * Implements f, F, t, T (and their ; / , repeats within a line).
	 * dir = 1 for f/t (forward), -1 for F/T (backward).  Does not cross
	 * newlines.  After finding the character:
	 * - f/F: lands on the matching byte.
	 * - t:   steps back one codepoint (stops just before the char).
	 * - T:   steps forward one codepoint (stops just after the char).
	 *
	 * - Calls indicate_error when the character is not found on the line.
	 */
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
	/*
	 * == Repeat last f/F/t/T search in the same direction (;) ==
	 */
	dot_to_char(g, g->last_search_cmd);
}

void
motion_run_repeat_search_reverse_cmd(struct editor *g)
{
	/*
	 * == Repeat last f/F/t/T search in the opposite direction (,) ==
	 *
	 * XOR 0x20 flips the case bit to swap f<->F and t<->T.
	 */
	dot_to_char(g, g->last_search_cmd ^ 0x20);
}

void
motion_run_find_char_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == Execute an f/F/t/T command ==
	 *
	 * Reads the target character from ctx->anchor (when available from the
	 * parser) or from the terminal interactively.  Saves it in
	 * g->last_search_char for ; / , repeats, then delegates to dot_to_char.
	 */
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
	/*
	 * == Move to the first non-blank character on the current line (^) ==
	 */
	dot_begin(g);
	dot_skip_over_ws(g);
}

void
motion_run_screen_top_cmd(struct editor *g)
{
	/*
	 * == Move to the top of the visible screen window (H) ==
	 *
	 * Supports a count: 3H moves to the 3rd line from the top.
	 */
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
	/*
	 * == Move to the bottom of the visible screen window (L) ==
	 *
	 * Supports a count: 3L moves to the 3rd line from the bottom.
	 */
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
	/*
	 * == Move to the middle line of the visible screen window (M) ==
	 */
	int cnt;

	g->dot = g->screenbegin;
	for (cnt = 0; cnt < (int)((g->rows - 1) / 2); cnt++)
		g->dot = next_line(g, g->dot);
	dot_skip_over_ws(g);
}

void
motion_run_goto_column_cmd(struct editor *g)
{
	/*
	 * == Move to column g->cmdcnt on the current line (|) ==
	 */
	g->dot = move_to_col(g, g->dot, g->cmdcnt - 1);
}

void
motion_run_goto_line_cmd(struct editor *g)
{
	/*
	 * == Go to line g->cmdcnt, or last line when count is zero (G) ==
	 *
	 * With no count: jumps to the last line of the buffer.
	 * With a count n: jumps to line n (1-based), landing on the first
	 * non-blank character.
	 */
	g->dot = g->end - 1;
	if (g->cmdcnt > 0)
		g->dot = find_line(g, g->cmdcnt);
	dot_begin(g);
	dot_skip_over_ws(g);
}

void
motion_run_left_cmd(struct editor *g)
{
	/*
	 * == Move cursor left g->cmdcnt codepoints (h) ==
	 */
	do {
		dot_left(g);
	} while (--g->cmdcnt > 0);
}

void
motion_run_right_cmd(struct editor *g)
{
	/*
	 * == Move cursor right g->cmdcnt codepoints (l / Space) ==
	 */
	do {
		dot_right(g);
	} while (--g->cmdcnt > 0);
}

void
motion_run_prev_empty_line_cmd(struct editor *g)
{
	/*
	 * == Move to the previous paragraph boundary ({) ==
	 *
	 * Jumps g->cmdcnt empty lines backward.  Calls indicate_error and
	 * returns early when no more empty lines exist.
	 */
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
	/*
	 * == Move to the next paragraph boundary (}) ==
	 *
	 * Jumps g->cmdcnt empty lines forward.  Calls indicate_error and
	 * returns early when no more empty lines exist.
	 */
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
	/*
	 * == Move to the next line, preserving the column (j / Down / Ctrl-J) ==
	 *
	 * When the previous command was also a vertical motion (prev_keep_index
	 * is TRUE), g->cindex holds the intended "sticky" column set by that
	 * prior j/k — use it directly so the column is preserved even when the
	 * cursor was forced to a shorter line in between.  Otherwise compute
	 * the column from the actual cursor position via get_column, which is
	 * always accurate regardless of whether refresh() ran.
	 *
	 * g->cindex == -1 is an explicit "end-of-line" signal set by $; in
	 * that case we pass a very large column to move_to_col so it walks to
	 * the last character on the destination line.
	 */
	char *p;
	char *q = g->dot;
	int col;

	if (g->cindex < 0)
		col = INT_MAX;
	else if (g->prev_keep_index)
		col = g->cindex;
	else
		col = get_column(g, g->dot);

	do {
		p = next_line(g, q);
		if (p == end_line(g, q)) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = move_to_col(g, q, col);
	g->cindex = (col == INT_MAX) ? -1 : col;
	g->keep_index = TRUE;
}

void
motion_run_prev_line_keep_col_cmd(struct editor *g)
{
	/*
	 * == Move to the previous line, preserving the column (k / Up) ==
	 *
	 * Mirror of motion_run_next_line_keep_col_cmd; see its comment for the
	 * rationale behind prev_keep_index and the sticky column logic.
	 */
	char *p;
	char *q = g->dot;
	int col;

	if (g->cindex < 0)
		col = INT_MAX;
	else if (g->prev_keep_index)
		col = g->cindex;
	else
		col = get_column(g, g->dot);

	do {
		p = prev_line(g, q);
		if (p == begin_line(g, q)) {
			indicate_error(g);
			return;
		}
		q = p;
	} while (--g->cmdcnt > 0);

	g->dot = move_to_col(g, q, col);
	g->cindex = (col == INT_MAX) ? -1 : col;
	g->keep_index = TRUE;
}

void
motion_run_next_line_skip_ws_cmd(struct editor *g)
{
	/*
	 * == Move to the next line, landing on the first non-blank (CR / +) ==
	 */
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
	/*
	 * == Move to the previous line, landing on the first non-blank (- / k-) ==
	 */
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
	/*
	 * == Move to the end of the current line ($) ==
	 *
	 * Supports a count: 3$ moves to the end of the 3rd line below.
	 * Sets g->cindex = -1 so subsequent j/k motions stay at end-of-line.
	 */
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
	/*
	 * == Move to the next/previous paragraph boundary (} / {) ==
	 *
	 * A paragraph boundary is a pair of consecutive newlines ('\n\n').
	 * The scan skips adjacent blank lines so the cursor lands just past
	 * (forward) or just before (backward) the blank-line separator.
	 *
	 * - c == '}': forward; any other value: backward.
	 * - Returns 1 when no boundary was found (caller sets error), 0 on
	 *   success.
	 */
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
	/*
	 * == Redraw so the current line is at a screen position (z) ==
	 *
	 * Second key selects the position: 't' or CR = top, '.' = middle,
	 * '-' or 'b' = bottom.  Reads the second key from ctx->op2 (parser
	 * path) or interactively from the terminal.
	 */
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
	/*
	 * == Scroll the view cnt lines in direction dir ==
	 *
	 * Moves g->screenbegin forward (dir > 0) or backward (dir < 0) by cnt
	 * lines, then clamps g->dot to stay within the visible window.
	 * Used by Ctrl-E/Y and the z scroll commands.
	 */
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
	/*
	 * == Clamp p to a valid buffer position ==
	 *
	 * Ensures p is within [g->text, g->end - 1] and snapped to a codepoint
	 * start.  Calls indicate_error when clamping was necessary.
	 * Used at operator-range boundaries to prevent out-of-bounds access.
	 */
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
