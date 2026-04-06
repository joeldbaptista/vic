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
	g->newindent = -1;
	g->cmd_mode = 1;
	undo_queue_commit(g);
}

void
edit_run_start_replace_cmd(struct editor *g)
{
	g->cmd_mode = 2;
	undo_queue_commit(g);
	g->rstart = g->dot;
}

void
edit_run_insert_before_first_nonblank_cmd(struct editor *g)
{
	dot_begin(g);
	dot_skip_over_ws(g);
	edit_run_start_insert_cmd(g);
}

void
edit_run_insert_before_cmd(struct editor *g)
{
	edit_run_start_insert_cmd(g);
}

void
edit_run_append_after_cmd(struct editor *g)
{
	if (*g->dot != '\n')
		g->dot = cp_next(g, g->dot);
	edit_run_start_insert_cmd(g);
}

void
edit_run_append_eol_cmd(struct editor *g)
{
	dot_end(g);
	edit_run_append_after_cmd(g);
}

void
edit_run_join_lines_cmd(struct editor *g)
{
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
