/*
 * visual.c - visual mode state and operator application.
 *
 * visual_mode values: 0=off, 1=charwise, 2=linewise, 3=block.
 * g->visual_anchor is the fixed end of the selection.
 *
 * visual_enter() / visual_leave() — switch mode on/off
 * visual_get_range()              — compute start/stop/buftype for char/line
 *                                   modes; returns 0 for block mode
 * block_visual_cols()             — column window and row span for block mode
 * block_selection_ranges()        — per-row [p,q) array for block mode;
 *                                   caller frees the result
 * visual_apply_operator()         — execute an operator (d, c, y, <, >,
 *                                   ~, !) over the visual selection, then
 *                                   leave visual mode
 *
 * "o" in visual mode swaps dot and anchor (implemented in vic.c, not here).
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
visual_block_insert_replay(struct editor *g)
{
	/*
	 * == Replay a block-visual I insertion onto every row below the first ==
	 *
	 * Called immediately after ESC exits the interactive insert that was
	 * triggered by 'I' in Ctrl-V block-visual mode.  At this point the text
	 * typed by the user has already been inserted on the first row of the
	 * block (at vis_block_insert_col).  This function inserts the same text
	 * at vis_block_insert_col on every subsequent row in the block that is
	 * long enough to reach that column.
	 *
	 * The net-inserted string is read directly from the buffer
	 * ([vis_block_insert_start_off .. g->dot] inclusive) so that backspaces
	 * and other editing during the insert session are accounted for.
	 *
	 * Rows are processed bottom-to-top so that insertions at lower rows do
	 * not shift the buffer positions of rows still to be processed.
	 * Because text_hole_make may realloc g->text the realloc bias is
	 * propagated to the remaining row-pointer array after each insertion.
	 */
	char *insert_start;
	int insert_len;
	char *insert_text;
	char *row_top;
	char *row_bot;
	int col;
	int nrows;
	char **rows;
	int i;

	insert_start = g->text + g->vis_block_insert_start_off;
	insert_len = (int)(g->dot - insert_start) + 1;
	if (insert_len <= 0)
		return;

	insert_text = xstrndup(insert_start, (size_t)insert_len);

	row_top = g->text + g->vis_block_row_top_off;
	/*
	 * vis_block_row_bot_off was recorded before the user typed anything.
	 * All insert_len bytes were inserted on row_top (at insert_start_off),
	 * which comes before row_bot in the buffer.  Each insertion shifted
	 * row_bot one byte forward, so the current begin_line of the bottom
	 * row is at the saved offset + insert_len.
	 */
	row_bot = g->text + g->vis_block_row_bot_off + insert_len;
	col = g->vis_block_insert_col;

	/* Count rows below row_top in the block. */
	nrows = count_lines(g, row_top, row_bot) - 1;
	if (nrows <= 0) {
		free(insert_text);
		return;
	}

	/* Collect begin_line pointers for rows row_top+1 .. row_bot. */
	rows = xmalloc((size_t)nrows * sizeof(*rows));
	{
		char *r = next_line(g, row_top);
		for (i = 0; i < nrows; i++, r = next_line(g, r))
			rows[i] = r;
	}

	/*
	 * Process bottom-to-top.  After each insertion the bias from any
	 * realloc is applied to rows not yet processed (they come before the
	 * insertion point in the buffer and are therefore not memmove'd, but
	 * they do need the realloc bias applied).
	 */
	for (i = nrows - 1; i >= 0; i--) {
		char *p = move_to_col(g, rows[i], col);
		if (get_column(g, p) == col) {
			uintptr_t bias = string_insert(g, p, insert_text,
			                               ALLOW_UNDO_CHAIN);
			if (bias) {
				int j;
				for (j = 0; j < i; j++)
					rows[j] += bias;
			}
		}
	}

	free(rows);
	free(insert_text);
}

void
visual_leave(struct editor *g)
{
	/*
	 * == Exit visual mode and archive the selection as `< and `> marks ==
	 *
	 * Saves the smaller of (anchor, dot) in MARK_LT and the larger in
	 * MARK_GT, expanding to line boundaries for line-visual mode.  Clears
	 * all visual state so the editor returns to Normal mode.
	 */
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
visual_enter(struct editor *g, int mode)
{
	/*
	 * == Enter visual mode, anchoring the selection at dot ==
	 *
	 * mode=1 → charwise visual (v)
	 * mode=2 → linewise visual (V): snaps anchor and dot to line starts
	 * mode=3 → block visual (Ctrl-V)
	 *
	 * Parameters:
	 *   g    — editor state
	 *   mode — 1, 2, or 3
	 */
	g->visual_mode = mode;
	g->visual_anchor = (mode == 2) ? begin_line(g, g->dot) : g->dot;
	if (mode == 2)
		g->dot = begin_line(g, g->dot);
	g->last_status_cksum = 0;
}

int
visual_get_range(struct editor *g, char **start, char **stop,
                 int *buftype)
{
	/*
	 * == Compute the current visual selection as start/stop/buftype ==
	 *
	 * Handles charwise (mode=1) and linewise (mode=2) only.  Block mode
	 * (mode=3) returns 0 — use block_selection_ranges() instead.
	 * Normalises anchor and dot so start <= stop.  In line mode expands to
	 * full-line boundaries and sets buftype=WHOLE.  In character mode sets
	 * buftype=MULTI if the selection spans multiple lines, PARTIAL otherwise.
	 * Returns 1 if visual mode is active and range is set, 0 otherwise.
	 */
	char *a;
	char *b;
	char *s;
	char *e;

	if (!g->visual_mode || g->visual_mode == 3 || g->visual_anchor == NULL)
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
block_visual_cols(struct editor *g, int *col_left, int *col_right,
                  char **row_top, char **row_bot)
{
	/*
	 * == Compute the column window and row span for block visual mode ==
	 *
	 * Returns the inclusive column range [col_left, col_right] and the
	 * begin_line pointers for the top and bottom rows of the block.
	 * Call only when visual_mode == 3 and visual_anchor != NULL.
	 *
	 * Parameters:
	 *   g         — editor state
	 *   col_left  — out: leftmost column of block (inclusive, 0-based)
	 *   col_right — out: rightmost column of block (inclusive)
	 *   row_top   — out: begin_line of the topmost selected row
	 *   row_bot   — out: begin_line of the bottommost selected row
	 */
	char *a = g->visual_anchor;
	char *b = g->dot;
	int ca = get_column(g, a);
	int cb = get_column(g, b);

	*col_left  = ca < cb ? ca : cb;
	*col_right = ca < cb ? cb : ca;
	*row_top   = begin_line(g, a < b ? a : b);
	*row_bot   = begin_line(g, a < b ? b : a);
}

struct block_range *
block_selection_ranges(struct editor *g, int *count)
{
	/*
	 * == Compute per-row byte ranges for the current block visual selection ==
	 *
	 * Each returned range [p, q) satisfies p < q and covers the bytes whose
	 * visual column falls within [col_left, col_right] on one buffer row.
	 * Both p and q are valid codepoint-start boundaries.
	 *
	 * Lines that do not reach col_left (including lines where a tab wholly
	 * spans the window) are omitted.  Lines that reach col_left but not
	 * col_right are clamped at end-of-line.
	 *
	 * Ranges are ordered by ascending buffer address (first selected row
	 * first).  The returned array must be freed by the caller.
	 *
	 * Parameters:
	 *   g     — editor state; visual_mode must be 3 and visual_anchor set
	 *   count — out: number of ranges returned
	 *
	 * Returns: heap-allocated array, or NULL when the selection is empty.
	 */
	struct block_range *ranges;
	int col_left, col_right;
	char *row_top, *row_bot;
	int nlines, n, i;
	char *line;

	*count = 0;
	if (g->visual_mode != 3 || g->visual_anchor == NULL)
		return NULL;

	block_visual_cols(g, &col_left, &col_right, &row_top, &row_bot);

	nlines = count_lines(g, row_top, row_bot);
	ranges = xmalloc((size_t)nlines * sizeof(*ranges));
	n = 0;

	line = row_top;
	for (i = 0; i < nlines; i++, line = next_line(g, line)) {
		char *ptr = begin_line(g, line);
		char *p = NULL, *q = NULL;
		int co = 0;

		while (ptr < g->end && *ptr != '\n') {
			char *nxt = cp_next(g, ptr);

			if (p == NULL) {
				if (co >= col_left) {
					if (co > col_right)
						break; /* tab swallows window; skip line */
					p = ptr;
				}
			} else {
				if (co > col_right) {
					q = ptr;
					break;
				}
			}

			co = next_column(g, ptr, co);
			ptr = nxt;
		}

		if (p != NULL) {
			if (q == NULL)
				q = ptr; /* clamp to eol or end-of-buffer */
			ranges[n].p = p;
			ranges[n].q = q;
			n++;
		}
	}

	*count = n;
	if (n == 0) {
		free(ranges);
		return NULL;
	}
	return ranges;
}

void
visual_apply_operator(struct editor *g, int op)
{
	/*
	 * == Apply an operator to the current visual selection ==
	 *
	 * For block mode (visual_mode == 3): resolves per-row ranges via
	 * block_selection_ranges, dispatches on op, processes ranges
	 * bottom-to-top so earlier pointer values remain valid through deletions.
	 *
	 * For charwise/linewise: resolves the selection via visual_get_range,
	 * then dispatches on op:
	 *   y  — yank the selection into the register
	 *   p  — replace selection with register contents
	 *   U/u — uppercase/lowercase in-place (UNDO_SWAP entry for atomic undo)
	 *   >/< — indent/de-indent each line in the selection
	 *   x/d — delete (x is remapped to d)
	 *   c  — delete and enter INSERT mode
	 *   C  — cut (delete into register, no INSERT mode); +C targets SHARED_REG
	 *
	 * Calls visual_leave() before mutating the buffer so that the `< and
	 * `> marks are set correctly before any pointer adjustment.
	 */
	char *start;
	char *stop;
	char *saved_reg = g->reg[g->ydreg];
	char *put_copy = NULL;
	int buftype;
	int allow_undo;

	/* ── Block visual mode (visual_mode == 3) ───────────────────────────── */
	if (g->visual_mode == 3) {
		int count = 0;
		struct block_range *ranges;
		int i;

		if (op == 'p') {
			/* Block-mode paste not yet implemented. */
			visual_leave(g);
			indicate_error(g);
			return;
		}

		ranges = block_selection_ranges(g, &count);
		visual_leave(g);

		if (ranges == NULL || count == 0) {
			free(ranges);
			indicate_error(g);
			return;
		}

		if (op == 'y') {
			size_t total = 0;
			char *buf, *dst;

			for (i = 0; i < count; i++)
				total += (size_t)(ranges[i].q - ranges[i].p) + 1;
			buf = xmalloc(total + 1);
			dst = buf;
			for (i = 0; i < count; i++) {
				size_t len = (size_t)(ranges[i].q - ranges[i].p);
				memcpy(dst, ranges[i].p, len);
				dst += len;
				*dst++ = '\n';
			}
			*dst = '\0';
			free(g->reg[g->ydreg]);
			g->reg[g->ydreg] = buf;
			g->regtype[g->ydreg] = BLOCK;
			yank_sync_out(g);
			yank_status(g, "Yank", buf, 1);
			reset_ydreg(g);
			free(ranges);
			return;
		}

		if (op == 'U' || op == 'u') {
			/* Case-change: in-place, uniform byte count.  Save the full
			 * contiguous span as one UNDO_SWAP entry so a single 'u'
			 * restores everything.  Bytes between ranges are over-saved
			 * but restored to their unchanged values, which is correct. */
			char *span_start = ranges[0].p;
			char *span_end = ranges[count - 1].q - 1;
			char *p;

			undo_push(g, span_start,
			          (unsigned)(span_end - span_start + 1), UNDO_SWAP);
			for (i = 0; i < count; i++) {
				for (p = ranges[i].p; p < ranges[i].q; p++) {
					unsigned char ch = (unsigned char)*p;
					if (ch < UTF8_MULTIBYTE_MIN && isalpha(ch))
						*p = (char)(op == 'U' ? toupper(ch) : tolower(ch));
				}
			}
			g->dot = ranges[0].p;
			reset_ydreg(g);
			free(ranges);
			return;
		}

		if (op == '>' || op == '<') {
			/* Indent/de-indent: operate on whole lines covering the block. */
			char *row_top = begin_line(g, ranges[0].p);
			char *row_bot_line = begin_line(g, ranges[count - 1].p);
			int nlines = count_lines(g, row_top, row_bot_line);
			char *p;

			allow_undo = ALLOW_UNDO;
			g->dot = row_top;
			for (p = row_top; nlines > 0; nlines--, p = next_line(g, p)) {
				if (op == '<') {
					if (*p == '\t') {
						text_hole_delete(g, p, p, allow_undo);
					} else if (*p == ' ') {
						int j;
						for (j = 0; *p == ' ' && j < g->tabstop; j++) {
							text_hole_delete(g, p, p, allow_undo);
							allow_undo = ALLOW_UNDO_CHAIN;
						}
					}
				} else if (p != end_line(g, p)) {
					char_insert(g, p, '\t', allow_undo);
				}
				allow_undo = ALLOW_UNDO_CHAIN;
			}
			dot_skip_over_ws(g);
			reset_ydreg(g);
			free(ranges);
			return;
		}

		/* d, x, C, c: delete ranges bottom-to-top, optionally enter insert. */
		if (op == 'x' || op == 'C')
			op = 'd';

		allow_undo = ALLOW_UNDO;
		for (i = count - 1; i >= 0; i--) {
			text_hole_delete(g, ranges[i].p, ranges[i].q - 1, allow_undo);
			allow_undo = ALLOW_UNDO_CHAIN;
		}
		g->dot = ranges[0].p;
		if (op == 'c')
			edit_run_start_insert_cmd(g);

		reset_ydreg(g);
		free(ranges);
		return;
	}

	/* ── Charwise / linewise (visual_mode 1 or 2) ───────────────────────── */

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

	if (op == '>' || op == '<') {
		char *p;
		int allow_undo2 = ALLOW_UNDO;
		int nlines = count_lines(g, start, stop);

		g->dot = start;
		for (p = begin_line(g, start); nlines > 0; nlines--, p = next_line(g, p)) {
			if (op == '<') {
				if (*p == '\t') {
					text_hole_delete(g, p, p, allow_undo2);
				} else if (*p == ' ') {
					int j;
					for (j = 0; *p == ' ' && j < g->tabstop; j++) {
						text_hole_delete(g, p, p, allow_undo2);
						allow_undo2 = ALLOW_UNDO_CHAIN;
					}
				}
			} else if (p != end_line(g, p)) {
				char_insert(g, p, '\t', allow_undo2);
			}
			allow_undo2 = ALLOW_UNDO_CHAIN;
		}
		dot_skip_over_ws(g);
		reset_ydreg(g);
		return;
	}

	if (op == 'x' || op == 'C')
		op = 'd';

	g->dot = yank_delete_current(g, start, stop, buftype, 1, ALLOW_UNDO);
	if (op == 'c')
		edit_run_start_insert_cmd(g);
	else if (buftype == WHOLE)
		dot_skip_over_ws(g);

	reset_ydreg(g);
}
