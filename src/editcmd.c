/*
 * editcmd.c - insert/replace mode entry commands.
 *
 * Implements the command-mode keypresses that switch the editor into
 * insert or replace mode:
 *   i/I  — insert before cursor / before first non-blank
 *   a/A  — append after cursor / after end of line
 *   o/O  — open new line below / above
 *   R    — enter replace mode
 *   J    — join lines
 *
 * Each function positions the cursor correctly and calls
 * edit_run_start_insert_cmd or edit_run_start_replace_cmd to set
 * g->cmd_mode and commit the undo queue.
 */
#include "editcmd.h"

#include "buffer.h"
#include "codepoint.h"
#include "line.h"
#include "motion.h"
#include "undo.h"

void
edit_run_start_insert_cmd(struct editor *g)
{
	/*
	 * == Enter INSERT mode ==
	 *
	 * Sets g->cmd_mode = 1 and commits any pending undo queue so the
	 * preceding Normal-mode command is recorded as one undo unit.
	 * g->newindent = -1 means "inherit the indent of the previous line"
	 * when autoindent fires on the first newline.
	 */
	g->newindent = -1;
	g->cmd_mode = 1;
	undo_queue_commit(g);
}

void
edit_run_start_replace_cmd(struct editor *g)
{
	/*
	 * == Enter REPLACE mode (R) ==
	 *
	 * Sets g->cmd_mode = 2.  g->rstart records the entry position so that
	 * backspace during replace can undo character-by-character back to
	 * the start of the replacement session.
	 */
	g->cmd_mode = 2;
	undo_queue_commit(g);
	g->rstart = g->dot;
}

void
edit_run_insert_before_first_nonblank_cmd(struct editor *g)
{
	/*
	 * == Insert before the first non-blank on the current line (I) ==
	 */
	dot_begin(g);
	dot_skip_over_ws(g);
	edit_run_start_insert_cmd(g);
}

void
edit_run_insert_before_cmd(struct editor *g)
{
	/*
	 * == Insert before the cursor (i) ==
	 */
	edit_run_start_insert_cmd(g);
}

void
edit_run_append_after_cmd(struct editor *g)
{
	/*
	 * == Append after the cursor (a) ==
	 *
	 * Steps dot one codepoint right (unless already on a newline) so that
	 * subsequent inserts land after the current character.
	 */
	if (*g->dot != '\n')
		g->dot = cp_next(g, g->dot);
	edit_run_start_insert_cmd(g);
}

void
edit_run_append_eol_cmd(struct editor *g)
{
	/*
	 * == Append after the end of the current line (A) ==
	 */
	dot_end(g);
	edit_run_append_after_cmd(g);
}

void
edit_run_join_lines_cmd(struct editor *g)
{
	/*
	 * == Join the current line with the next (J) ==
	 *
	 * Replaces the trailing newline of the current line with a space, then
	 * strips any leading blanks on the joined line so adjacent words are
	 * separated by exactly one space.  Repeats g->cmdcnt times.
	 */
	do {
		dot_end(g);
		if (g->dot < g->end - 1) {
			undo_push(g, g->dot, 1, UNDO_DEL);
			*g->dot++ = ' ';
			undo_push(g, g->dot - 1, 1, UNDO_INS_CHAIN);
			while (isblank((unsigned char)*g->dot))
				text_hole_delete(g, g->dot, g->dot, ALLOW_UNDO_CHAIN);
		}
	} while (--g->cmdcnt > 0);
	reset_ydreg(g);
}

void
edit_run_open_line_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == Open a new line below (o) or above (O) the current line ==
	 *
	 * o: moves to end_line and inserts '\n', entering INSERT mode after.
	 * O: moves to begin_line, captures the current indent for autoindent,
	 *    inserts '\n', then backs up so the new line is above.
	 */
	int above = ctx && ctx->op == 'O';
	if (above) {
		dot_begin(g);
		g->newindent = get_column(g, g->dot + indent_len(g, g->dot));
	} else {
		dot_end(g);
	}

	g->cmd_mode = 1;
	g->dot = char_insert(g, g->dot, '\n', ALLOW_UNDO);
	if (above && !IS_AUTOINDENT(g))
		dot_prev(g);
	edit_run_start_insert_cmd(g);
}
