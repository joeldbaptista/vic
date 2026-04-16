#define _GNU_SOURCE
#include "vic.h"
#include "buffer.h"
#include "codepoint.h"
#include "context.h"
#include "editcmd.h"
#include "ex.h"
#include "excore.h"
#include "input.h"
#include "line.h"
#include "motion.h"
#include "operator.h"
#include "parser.h"
#include "range.h"
#include "scan.h"
#include "screen.h"
#include "search.h"
#include "session.h"
#include "status.h"
#include "term.h"
#include "textobj.h"
#include "undo.h"
#include "utf8.h"
#include "visual.h"
#include "wordmotion.h"
#include <locale.h>
#include <regex.h>
#include <wchar.h>

/* 0x9b is Meta-ESC */
#define Isprint(c) ((unsigned char)(c) >= ' ' && (unsigned char)(c) < 0x7f)

static struct editor vi_g;

enum {
	MAX_INPUT_LEN = 128,
	MAX_SCR_COLS = VI_MAX_LINE,
	MAX_SCR_ROWS = VI_MAX_LINE,
	MAX_TABSTOP = 32,
};

/* VT102 / xterm control sequences */
#define ESC "\033"
#define ESC_BOLD_TEXT ESC "[7m"
#define ESC_NORM_TEXT ESC "[m"
#define ESC_BELL "\007"
#define ESC_CLEAR2EOL ESC "[K"
#define ESC_CLEAR2EOS ESC "[J"           /* default param = erase below */
#define ESC_SET_CURSOR_POS ESC "[%u;%uH" /* row;col, 1-based */
#define ESC_SET_CURSOR_TOPLEFT ESC "[H"

static const char modifying_cmds[] ALIGN1 = "aAcCdDiIJoOpPrRs"
                                            "xX<>~";

enum {
	YANKONLY = FALSE,
	YANKDEL = TRUE,
	FORWARD = 1,     /* code depends on "1"  for array index */
	BACK = -1,       /* code depends on "-1" for array index */
	LIMITED = 0,     /* char_search() current line only */
	FULL = 1,        /* char_search() entire text */
	S_BEFORE_WS = 1, /* skip_thing() modes for moving dot */
	S_TO_WS = 2,
	S_OVER_WS = 3,
	S_END_PUNCT = 4,
	S_END_ALNUM = 5,

};

void
show_help(void)
{
	/*
	 * == Print a brief feature summary to stdout ==
	 *
	 * Called when the user passes -h / --help.  Lists the most notable
	 * features available in this vi implementation.
	 */
	puts("These features are available:"
	     "\n\tPattern searches with / and ?"
	     "\n\tLast command repeat with ."
	     "\n\tLine marking with 'x"
	     "\n\tNamed buffers with \"x"
	     "\n\tSome colon mode commands with :"
	     "\n\tSettable options with \":set\""
	     "\n\tSignal catching- ^C"
	     "\n\tJob suspend and resume with ^Z"
	     "\n\tAdapt to window re-sizes");
}

void
sync_cursor(struct editor *g, char *d, int *row, int *col)
{
	/*
	 * == Scroll screenbegin so d is visible and compute its screen row/col ==
	 *
	 * If d is above the visible window, scrolls the screen up (or re-centres
	 * if d moved more than half a page).  If d is below, scrolls down by
	 * the minimum needed, again re-centring for large jumps.  Then computes
	 * d's logical column (accounting for tabs/UTF-8) and shifts g->offset
	 * left/right so the column falls within the visible horizontal window.
	 * Writes the resulting 0-based screen row into *row and screen column
	 * into *col.
	 */
	char *beg_cur;
	char *tp;
	int cnt, ro, co;
	int text_cols;

	beg_cur = begin_line(g, d);

	if (beg_cur < g->screenbegin) {
		cnt = count_lines(g, beg_cur, g->screenbegin);
	sc1:
		g->screenbegin = beg_cur;
		if (cnt > (int)((g->rows - 1) / 2)) {
			/* cursor moved too far — recentre */
			for (cnt = 0; cnt < (int)((g->rows - 1) / 2); cnt++)
				g->screenbegin = prev_line(g, g->screenbegin);
		}
	} else {
		char *end_scr = end_screen(g);
		if (beg_cur > end_scr) {
			cnt = count_lines(g, end_scr, beg_cur);
			if (cnt > (int)((g->rows - 1) / 2))
				goto sc1;
			for (ro = 0; ro < cnt - 1; ro++) {
				g->screenbegin = next_line(g, g->screenbegin);
				end_scr = next_line(g, end_scr);
				end_scr = end_line(g, end_scr);
			}
		}
	}
	/* find the row of d on screen */
	tp = g->screenbegin;
	for (ro = 0; ro < (int)g->rows - 1; ro++) {
		if (tp == beg_cur)
			break;
		tp = next_line(g, tp);
	}

	co = get_column(g, d);

	/*
	 * co is the column of dot.  The visible window is [offset, offset+cols).
	 *
	 * |-------------------------------------------------------------|
	 *               ^ ^                                ^
	 *        offset | |------- columns ----------------|
	 *
	 * If co is inside the window, subtract offset bias.
	 * If co is outside, shift the window to include it.
	 * If the first char of the line is a tab, force offset to 0.
	 */
	text_cols = screen_text_columns_on_screen(g);

	if (co < 0 + g->offset)
		g->offset = co;
	if (co >= text_cols + g->offset)
		g->offset = co - text_cols + 1;
	if (d == beg_cur && *d == '\t')
		g->offset = 0;
	co -= g->offset;

	*row = ro;
	*col = co;
}

static void
flash(int h)
{
	/*
	 * == Briefly invert the terminal display for visual error feedback ==
	 *
	 * Turns on reverse-video for h centiseconds then restores normal video.
	 * Used by indicate_error() when VI_ERR_METHOD is set to visual flash.
	 */
	fputs(ESC "[?5h", stdout); /* reverse video on */
	mysleep(h);
	fputs(ESC "[?5l", stdout); /* reverse video off */
}

void
indicate_error(struct editor *g)
{
	/*
	 * == Signal an error to the user (bell or visual flash) ==
	 *
	 * Sets g->cmd_error so callers can detect the error after the fact.
	 * Emits a terminal bell unless VI_ERR_METHOD is set to visual, in which
	 * case flash() is called instead.
	 */
	g->cmd_error = TRUE;
	if (!IS_ERR_METHOD(g)) {
		fputs(ESC_BELL, stdout);
	} else {
		flash(10);
	}
}

int
get_motion_char(struct editor *g)
{
	/*
	 * == Read the next command character, absorbing an optional digit count ==
	 *
	 * Reads one character from input.  If it is a non-zero digit, keeps
	 * reading digits and accumulates the number into g->cmdcnt (multiplied
	 * with any existing cmdcnt).  Returns the first non-digit character.
	 * Used by motion commands that may be preceded by a repeat count.
	 */

	int c, cnt;

	c = get_one_char(g);
	if (isdigit(c)) {
		if (c != '0') {
			for (cnt = 0; isdigit(c); c = get_one_char(g))
				cnt = cnt * 10 + (c - '0');
			g->cmdcnt = (g->cmdcnt ?: 1) * cnt;
		} else {
			g->cmdcnt = 0; /* standalone '0' is a motion, not a count */
		}
	}

	return c;
}

void
reset_ydreg(struct editor *g)
{
	/*
	 * == Reset the yank/delete register to the default unnamed register ==
	 *
	 * Sets g->ydreg to 26 (the unnamed register) and clears g->adding2q.
	 * Called after any command that consumes the register so the next
	 * yank/delete goes to the right place.
	 */

	g->ydreg = 26;
	g->adding2q = 0;
}

char *
find_pair(struct editor *g, char *p, const char c)
{
	/*
	 * == Find the matching bracket for the bracket character at p ==
	 *
	 * Handles the pairs ()  []  {}  <>.  Searches forward for open brackets
	 * and backward for close brackets, tracking nesting level.
	 * Returns a pointer to the matching bracket, or NULL if not found or if
	 * c is not a recognised bracket character.
	 */
	(void)g;

	const char *braces = "()[]{}<>";
	const char *hit;
	char match;
	int dir, level;

	hit = strchr(braces, c);
	if (hit == NULL)
		return NULL;

	dir = hit - braces;
	dir ^= 1;
	match = braces[dir];
	dir = ((dir & 1) << 1) - 1; /* +1 for open brackets, -1 for close */

	level = 1;
	for (;;) {
		p += dir;
		if (p < g->text || p >= g->end)
			return NULL;
		if (*p == c)
			level++;
		if (*p == match) {
			level--;
			if (level == 0)
				return p;
		}
	}
}

void
showmatching(struct editor *g, char *p)
{
	/*
	 * == Briefly move the cursor to the matching bracket and bounce back ==
	 *
	 * Calls find_pair() on the character at p.  If a match is found, moves
	 * dot there, redraws, pauses 40 ms, then restores dot and redraws again
	 * — giving a brief visual highlight.  Rings the error bell if no match.
	 */
	char *q, *save_dot;

	q = find_pair(g, p, *p);
	if (q == NULL) {
		indicate_error(g);
	} else {
		save_dot = g->dot;
		g->dot = q;
		refresh(g, FALSE);
		mysleep(40);
		g->dot = save_dot;
		refresh(g, FALSE);
	}
}

/*
 * Word chars:      0-9 _ A-Z a-z
 * Stoppers:        !"#$%&'()*+,-./:;<=>?@[\]^`{|}~
 * Whitespace:      TAB VT FF CR SPACE  (newline is NOT whitespace here)
 */
static void
winch_handler(int sig UNUSED_PARAM)
{
	/*
	 * == SIGWINCH handler — mark that a terminal resize is pending ==
	 *
	 * Sets vi_g.need_winch = 1.  The actual resize work (re-query dimensions,
	 * allocate new screen, redraw) is deferred to process_pending_signals()
	 * so it runs outside the signal handler.
	 */
	(void)sig;
	vi_g.need_winch = 1;
}

static void
tstp_handler(int sig UNUSED_PARAM)
{
	/*
	 * == SIGTSTP handler — mark that a job-suspend is pending ==
	 *
	 * Sets vi_g.need_tstp = 1.  The actual suspend sequence (cookmode,
	 * raise(SIGSTOP), rawmode, redraw) is deferred to process_pending_signals().
	 */
	(void)sig;
	vi_g.need_tstp = 1;
}

static void
int_handler(int sig)
{
	/*
	 * == SIGINT handler — mark that an interrupt is pending ==
	 *
	 * Sets vi_g.need_int = 1.  process_pending_signals() will siglongjmp
	 * to g->restart, aborting any in-progress command and returning to the
	 * main edit loop.
	 */
	(void)sig;
	vi_g.need_int = 1;
}

void
process_pending_signals(struct editor *g)
{
	/*
	 * == Handle deferred SIGINT / SIGWINCH / SIGTSTP actions ==
	 *
	 * Called from the main edit loop at safe points (not inside a signal
	 * handler).  Processes at most one pending signal per call in priority
	 * order: INT first (longjmps), then WINCH (resize + redraw), then TSTP
	 * (suspend: cookmode → SIGSTOP → rawmode → redraw).
	 */
	if (g->need_int) {
		g->need_int = 0;
		siglongjmp(g->restart, SIGINT);
	}

	if (g->need_winch) {
		g->need_winch = 0;
		query_screen_dimensions(g);
		new_screen(g, g->rows, g->columns);
		redraw(g, TRUE);
	}

	if (g->need_tstp) {
		g->need_tstp = 0;

		/* ioctl inside cookmode() may generate SIGTTOU; ignore it */
		signal(SIGTTOU, SIG_IGN);

		go_bottom_and_clear_to_eol(g);
		cookmode(g);
		raise(SIGSTOP);
		rawmode(g);
		term_cursor_shape_update_for_mode(g->cmd_mode);
		g->last_status_cksum = 0;
		redraw(g, TRUE);
	}
}

typedef void (*cmd_fn)(struct editor *g);
typedef void (*cmd_ctx_fn)(struct editor *g, const struct cmd_ctx *ctx);

struct cmd_entry {
	int key;
	cmd_fn fn;         /* non-NULL: simple command, no ctx needed */
	cmd_ctx_fn ctx_fn; /* non-NULL: command uses ctx */
};

static void
run_status_mark_dirty_cmd(struct editor *g)
{
	/*
	 * == ^G — force the status line to redisplay on the next refresh ==
	 *
	 * Clears g->last_status_cksum so show_status_line() considers the
	 * status stale and redraws it.
	 */
	g->last_status_cksum = 0;
}

static void
run_redraw_cmd(struct editor *g)
{
	/*
	 * == ^L — force a full screen redraw ==
	 *
	 * Calls redraw(g, TRUE) to repaint every line unconditionally.
	 * Useful after terminal corruption or after returning from a shell command.
	 */
	redraw(g, TRUE);
}

static void
run_escape_cmd(struct editor *g)
{
	/*
	 * == ESC — return to Normal mode and commit any queued undo entry ==
	 *
	 * If already in Normal mode (cmd_mode == 0), signals an error.
	 * Otherwise clears cmd_mode, commits any open undo queue entry, resets
	 * the register, and invalidates the status checksum so it redraws.
	 */
	if (g->cmd_mode == 0)
		indicate_error(g);
	g->cmd_mode = 0;
	undo_queue_commit(g);
	reset_ydreg(g);
	g->last_status_cksum = 0;
}

static void
run_match_pair_cmd(struct editor *g)
{
	/*
	 * == % — jump to the bracket that matches the bracket at or after dot ==
	 *
	 * Scans right from dot to find the first bracket character on the current
	 * line, then calls find_pair() to locate its match.  Moves dot there, or
	 * signals an error if no bracket is found or there is no matching bracket.
	 */
	char *p;
	char *q;

	for (q = g->dot; q < g->end && *q != '\n'; q++) {
		if (strchr("()[]{}", *q) != NULL) {
			p = find_pair(g, q, *q);
			if (p == NULL) {
				indicate_error(g);
			} else {
				g->dot = p;
			}
			return;
		}
	}

	indicate_error(g);
}

static void
run_undo_last_cmd(struct editor *g)
{
	/*
	 * == u — undo the last modifying command ==
	 *
	 * Delegates to undo_with_redo() which pops the undo stack and pushes
	 * the current state onto the redo stack.
	 */
	undo_with_redo(g);
}

static void
run_redo_last_cmd(struct editor *g)
{
	/*
	 * == ^R — redo the last undone command ==
	 *
	 * Delegates to redo_pop() which pops the redo stack and restores that
	 * buffer state.
	 */
	redo_pop(g);
}

static void
run_repeat_last_modifying_cmd(struct editor *g)
{
	/*
	 * == . — repeat the last buffer-modifying command ==
	 *
	 * Reconstructs the last command's context (operator, count, register)
	 * from g->last_cmd_ctx and replays it via do_cmd().  For insert-entering
	 * commands (a/i/c/…), the captured insert text is queued into ioq_start
	 * so it replays character by character through the main loop.  The dot-
	 * repeat count is overridden by any explicit g->cmdcnt set by the user.
	 */
	struct cmd_ctx ctx;
	int c;
	int pre_modified;
	int replay_lmc_len;

	if (!g->has_last_cmd_ctx)
		return;

	ctx = g->last_cmd_ctx;
	g->cmdcnt = g->cmdcnt ? g->cmdcnt : ctx.count;

	if (ctx.reg) {
		if (ctx.reg == '+') {
			g->ydreg = SHARED_REG;
		} else {
			int ri = (ctx.reg | 0x20) - 'a';
			if ((unsigned)ri <= 25)
				g->ydreg = (unsigned)ri;
		}
	}

	g->adding2q = in_set(ctx.op, "aAcCiIoORs") ? 1 : 0;
	replay_lmc_len = g->lmc_len; /* save before resetting for fresh capture */
	if (g->adding2q)
		g->lmc_len = 0;

	c = ctx.op ? (int)(unsigned char)ctx.op : ctx.raw_key;
	pre_modified = g->modified_count;
	do_cmd(g, c, &ctx);

	if (g->modified_count != pre_modified) {
		g->last_cmd_ctx = ctx;
		g->has_last_cmd_ctx = 1;
		if (!g->adding2q)
			g->lmc_len = 0;
	}

	/* queue insert text for replay through the main loop */
	if (replay_lmc_len > 0) {
		char *buf = xmalloc(replay_lmc_len + 1);
		memcpy(buf, g->last_modifying_cmd, replay_lmc_len);
		buf[replay_lmc_len] = '\0';
		free(g->ioq_start);
		g->ioq = g->ioq_start = buf;
	}
}

static void
run_undo_line_cmd(struct editor *g)
{
	/*
	 * == U — restore the current line to its state when dot first entered it ==
	 *
	 * Replaces the entire current line with the snapshot stored in g->reg[ureg],
	 * which is updated each time dot moves to a new line.  Leaves dot at the
	 * first non-blank character.  Does nothing if ureg is empty.
	 */
	char *p;
	char *q;

	if (g->reg[ureg] == NULL)
		return;

	p = begin_line(g, g->dot);
	q = end_line(g, g->dot);
	p = text_hole_delete(g, p, q, ALLOW_UNDO);
	p += string_insert(g, p, g->reg[ureg], ALLOW_UNDO_CHAIN);
	g->dot = p;
	dot_skip_over_ws(g);
	yank_status(g, "Undo", g->reg[ureg], 1);
}

static void
run_colon_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == : — enter and execute a colon (Ex) command ==
	 *
	 * If ctx carries a pre-built command string (from a :map binding), runs
	 * it directly.  In visual mode, leaves visual mode to set the '< '> marks,
	 * then prompts with ":'<,'>" so s/// acts on the selection.  In normal
	 * mode, prompts with ":".  Redraws after execution.
	 */
	char *p;

	if (ctx && ctx->str) {
		p = (char *)ctx->str;
		colon(g, p);
		refresh(g, FALSE);
	} else if (g->visual_mode) {
		/* Leave visual mode (sets '< and '> marks), then pre-fill
		 * the address range so :s acts on the selection. */
		visual_leave(g);
		p = get_input_line(g, ":'<,'>");
		colon(g, p);
	} else {
		p = get_input_line(g, ":");
		colon(g, p);
	}
}

static void
run_digit_cmd(struct editor *g, int d)
{
	/*
	 * == Accumulate a digit into the command count, or run '0' as a motion ==
	 *
	 * If d is 0 and no count is being built (cmdcnt < 1), treats '0' as the
	 * "go to beginning of line" motion.  Otherwise appends the digit to
	 * g->cmdcnt for use as a repeat count by the following command.
	 */
	if (d == 0 && g->cmdcnt < 1) {
		dot_begin(g);
	} else {
		g->cmdcnt = g->cmdcnt * 10 + d;
	}
}

static void
run_set_mark_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == m{a-z} — set a named mark at the current cursor position ==
	 *
	 * Reads the mark letter from ctx->anchor (if available) or from the
	 * input stream.  Stores g->dot into g->mark[letter - 'a'].  Signals
	 * an error for any character outside a-z.
	 */
	int c1;

	c1 = (ctx && ctx->anchor) ? (int)(unsigned char)ctx->anchor : get_one_char(g);
	c1 = (c1 | 0x20) - 'a';
	if ((unsigned)c1 <= 25) {
		g->mark[c1] = g->dot;
	} else {
		indicate_error(g);
	}
}

static void
run_jump_mark_cmd(struct editor *g, char **orig_dot, const struct cmd_ctx *ctx)
{
	/*
	 * == '{a-z} — jump to a named mark ==
	 *
	 * For letters a-z: moves dot to the stored mark, goes to beginning of line,
	 * then skips whitespace.  For '' (double-quote): swaps the current position
	 * with the saved context mark (the position before the last jump) and also
	 * updates *orig_dot.  Signals an error for unknown mark letters or if the
	 * mark points outside the buffer.
	 */
	char *q;
	int c1;

	c1 = (ctx && ctx->anchor) ? (int)(unsigned char)ctx->anchor : get_one_char(g);
	c1 = (c1 | 0x20);
	if ((unsigned)(c1 - 'a') <= 25) {
		c1 = (c1 - 'a');
		q = g->mark[c1];
		if (g->text <= q && q < g->end) {
			g->dot = q;
			dot_begin(g);
			dot_skip_over_ws(g);
		} else {
			indicate_error(g);
		}
	} else if (c1 == '\'') {
		g->dot = swap_context(g, g->dot);
		dot_begin(g);
		dot_skip_over_ws(g);
		*orig_dot = g->dot;
	} else {
		indicate_error(g);
	}
}

static void
run_delete_key_cmd(struct editor *g)
{
	/*
	 * == DEL key — delete the character under the cursor ==
	 *
	 * Deletes the UTF-8 codepoint at dot if dot is not at or past the last
	 * newline.  Uses yank_delete_current so the deleted text goes into the
	 * register.
	 */
	if (g->dot < g->end - 1 && *g->dot != '\n') {
		char *start = cp_start(g, g->dot);
		char *stop = cp_end(g, start) - 1;
		g->dot = yank_delete_current(g, start, stop, PARTIAL, YANKDEL, ALLOW_UNDO);
	}
}

static void
run_g_prefix_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == g{x} — dispatch 'g' two-character commands ==
	 *
	 * Reads the second character from ctx->op2 or the input stream:
	 *   gg  — go to first line (sets cmdcnt to 1 if not set)
	 *   g*  — search for word under cursor without word boundaries
	 *   g#  — search backward for word under cursor without word boundaries
	 * Any other second character signals an error and reports not_implemented.
	 */
	char buf[3];
	int c1;

	c1 = (ctx && ctx->op2) ? (int)(unsigned char)ctx->op2 : get_one_char(g);
	if (c1 == 'g') {
		if (g->cmdcnt == 0)
			g->cmdcnt = 1;
		motion_run_goto_line_cmd(g);
		return;
	}
	if (c1 == '*') {
		search_run_word_cmd(g, '*', 0);
		return;
	}
	if (c1 == '#') {
		search_run_word_cmd(g, '#', 0);
		return;
	}
	{
		buf[0] = 'g';
		buf[1] = (c1 >= 0 ? c1 : '*');
		buf[2] = '\0';
		not_implemented(g, buf);
		g->cmd_error = TRUE;
	}
}

static void
run_visual_char_mode_cmd(struct editor *g)
{
	/*
	 * == v — toggle characterwise visual mode ==
	 *
	 * If already in characterwise visual mode (visual_mode == 1), leaves it.
	 * Otherwise enters characterwise visual mode.
	 */
	if (g->visual_mode == 1)
		visual_leave(g);
	else
		visual_enter(g, 1);
}

static void
run_visual_line_mode_cmd(struct editor *g)
{
	/*
	 * == V — toggle linewise visual mode ==
	 *
	 * If already in linewise visual mode (visual_mode == 2), leaves it.
	 * Otherwise enters linewise visual mode.
	 */
	if (g->visual_mode == 2)
		visual_leave(g);
	else
		visual_enter(g, 2);
}

static void
run_visual_block_mode_cmd(struct editor *g)
{
	/*
	 * == Ctrl-V — toggle block visual mode ==
	 *
	 * If already in block visual mode (visual_mode == 3), leaves it.
	 * Otherwise enters block visual mode.
	 */
	if (g->visual_mode == 3)
		visual_leave(g);
	else
		visual_enter(g, 3);
}

static void
run_zz_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == ZZ / ZQ — save and quit, or quit without saving ==
	 *
	 * Reads the second character from ctx->op2 or input:
	 *   ZZ — if modified, writes the file; on success sets editing=0 to exit.
	 *         If there are more files on the command line, reports the count
	 *         and stays editing.
	 *   ZQ — quits without saving by advancing optind past remaining files.
	 * Any other character signals an error.
	 */
	int c1;
	int cnt;
	int j;

	c1 = (ctx && ctx->op2) ? (int)(unsigned char)ctx->op2 : get_one_char(g);
	if (c1 == 'Q') {
		g->editing = 0;
		optind = g->cmdline_filecnt;
		return;
	}
	if (c1 != 'Z') {
		indicate_error(g);
		return;
	}
	if (g->modified_count) {
		if (g->readonly_mode && g->current_filename) {
			status_line_bold(g, "'%s' is read only", g->current_filename);
			return;
		}
		cnt = file_write(g, g->current_filename, g->text, g->end - 1);
		if (cnt < 0) {
			if (cnt == -1)
				status_line_bold(g, "Write error: %s", strerror(errno));
		} else if (cnt == (g->end - 1 - g->text + 1)) {
			g->editing = 0;
		}
	} else {
		g->editing = 0;
	}
	j = g->cmdline_filecnt - optind - 1;
	if (g->editing == 0 && j > 0) {
		g->editing = 1;
		g->modified_count = 0;
		g->last_modified_count = -1;
		status_line_bold(g, "%u more file(s) to edit", j);
	}
}

static void
do_put(struct editor *g, int before)
{
	/*
	 * == Core paste implementation shared by P and p ==
	 *
	 * Reads the active yank register (syncing from the shared clipboard if
	 * needed).  For WHOLE (linewise) registers: positions dot at the line
	 * before (P) or after (p) the current line.  For PARTIAL (characterwise)
	 * registers: inserts at dot (P) or one character right (p).  Repeats
	 * g->cmdcnt times, chaining undo entries.  Moves dot to the end of the
	 * pasted text and resets the register.
	 */
	char *p;
	int allow_undo;
	int cnt;
	int i;

	if (g->ydreg == SHARED_REG)
		shared_yank_in(g);
	else
		yank_sync_in(g);
	p = g->reg[g->ydreg];
	if (p == NULL) {
		status_line_bold(g, "Nothing in register %c", what_reg(g));
		return;
	}
	allow_undo = ALLOW_UNDO;
	cnt = 0;
	i = g->cmdcnt ?: 1;
	if (g->regtype[g->ydreg] == WHOLE) {
		if (before) {
			dot_begin(g);
		} else {
			if (end_line(g, g->dot) == (g->end - 1)) {
				g->dot = g->end;
			} else {
				dot_next(g);
			}
		}
	} else {
		if (!before)
			dot_right(g);
		if (strchr(p, '\n') == NULL)
			cnt = i * strlen(p) - 1;
	}
	do {
		string_insert(g, g->dot, p, allow_undo);
		allow_undo = ALLOW_UNDO_CHAIN;
	} while (--g->cmdcnt > 0);
	g->dot += cnt;
	dot_skip_over_ws(g);
	yank_status(g, "Put", p, i);
	reset_ydreg(g);
}

static void
run_put_before_cmd(struct editor *g)
{
	/*
	 * == P — paste the register contents before the cursor ==
	 */
	do_put(g, 1);
}

static void
run_put_after_cmd(struct editor *g)
{
	/*
	 * == p — paste the register contents after the cursor ==
	 */
	do_put(g, 0);
}

static void
run_paragraph_fwd_cmd(struct editor *g)
{
	/*
	 * == } — move forward to the next paragraph boundary ==
	 *
	 * Delegates to motion_run_paragraph_cmd; resets the register on success.
	 */
	if (motion_run_paragraph_cmd(g, '}'))
		reset_ydreg(g);
}

static void
run_paragraph_bck_cmd(struct editor *g)
{
	/*
	 * == { — move backward to the previous paragraph boundary ==
	 *
	 * Delegates to motion_run_paragraph_cmd; resets the register on success.
	 */
	if (motion_run_paragraph_cmd(g, '{'))
		reset_ydreg(g);
}

static void
do_shift(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == < / > — indent or de-indent lines covered by a motion or range ==
	 *
	 * Resolves the target range with range_find(), then iterates over each
	 * line: '>' inserts a tab at the start (unless the line is empty), '<'
	 * removes one leading tab or up to tabstop leading spaces.  All edits
	 * are chained into one undo entry.  Leaves dot at the first non-blank
	 * of the first affected line.
	 */
	char *p;
	char *q;
	int allow_undo;
	int cnt;
	int i;
	int j;

	cnt = count_lines(g, g->text, g->dot);
	if (range_find(g, &p, &q, ctx) == -1) {
		reset_ydreg(g);
		return;
	}
	allow_undo = ALLOW_UNDO;
	i = count_lines(g, p, q);
	for (p = begin_line(g, p); i > 0; i--, p = next_line(g, p)) {
		if (ctx->op == '<') {
			if (*p == '\t') {
				text_hole_delete(g, p, p, allow_undo);
			} else if (*p == ' ') {
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
	g->dot = find_line(g, cnt);
	dot_skip_over_ws(g);
	reset_ydreg(g);
}

static void
scroll_up_page(struct editor *g)
{
	/* == ^B / PgUp — scroll up one full page == */
	dot_scroll(g, g->rows - 2, -1);
}
static void
scroll_dn_half(struct editor *g)
{
	/* == ^D — scroll down half a page == */
	dot_scroll(g, (g->rows - 2) / 2, 1);
}
static void
scroll_dn_line(struct editor *g)
{
	/* == ^E — scroll the view down one line without moving dot == */
	dot_scroll(g, 1, 1);
}
static void
scroll_dn_page(struct editor *g)
{
	/* == ^F / PgDn — scroll down one full page == */
	dot_scroll(g, g->rows - 2, 1);
}
static void
scroll_up_half(struct editor *g)
{
	/* == ^U — scroll up half a page == */
	dot_scroll(g, (g->rows - 2) / 2, -1);
}
static void
scroll_up_line(struct editor *g)
{
	/* == ^Y — scroll the view up one line without moving dot == */
	dot_scroll(g, 1, -1);
}

static void
run_search_star_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == * — search forward for the word under the cursor (whole-word) ==
	 */
	(void)ctx;
	search_run_word_cmd(g, '*', 1);
}

static void
run_search_hash_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	/*
	 * == # — search backward for the word under the cursor (whole-word) ==
	 */
	(void)ctx;
	search_run_word_cmd(g, '#', 1);
}

static int
dispatch_cmd(struct editor *g, int c, const struct cmd_ctx *ctx)
{
	/*
	 * == Look up and execute a Normal-mode command by key code ==
	 *
	 * Handles digit keys (0-9) specially: routes them through run_digit_cmd.
	 * For all other keys, searches the static cmd_entry table for a matching
	 * key and calls either fn (simple) or ctx_fn (context-aware) handler.
	 * Returns 1 if a handler was found and executed, 0 otherwise.
	 * A synthetic cmd_ctx is constructed when ctx is NULL.
	 */
	struct cmd_ctx local_ctx;
	size_t i;

	if (!ctx) {
		memset(&local_ctx, 0, sizeof(local_ctx));
		local_ctx.op = (char)c;
		local_ctx.count = 1;
		local_ctx.rcount = 1;
		ctx = &local_ctx;
	}

	if (c >= '0' && c <= '9') {
		run_digit_cmd(g, c - '0');
		return 1;
	}

	static const struct cmd_entry table[] = {
	    {ASCII_CTRL_B, scroll_up_page, NULL},
	    {KEYCODE_PAGEUP, scroll_up_page, NULL},
	    {ASCII_CTRL_D, scroll_dn_half, NULL},
	    {ASCII_CTRL_E, scroll_dn_line, NULL},
	    {ASCII_CTRL_F, scroll_dn_page, NULL},
	    {KEYCODE_PAGEDOWN, scroll_dn_page, NULL},
	    {ASCII_CTRL_U, scroll_up_half, NULL},
	    {ASCII_CTRL_Y, scroll_up_line, NULL},
	    {'f', NULL, motion_run_find_char_cmd},
	    {'F', NULL, motion_run_find_char_cmd},
	    {'t', NULL, motion_run_find_char_cmd},
	    {'T', NULL, motion_run_find_char_cmd},
	    {'*', NULL, run_search_star_cmd},
	    {'#', NULL, run_search_hash_cmd},
	    {'n', NULL, search_run_cmd},
	    {'N', NULL, search_run_cmd},
	    {'?', NULL, search_run_cmd},
	    {'/', NULL, search_run_cmd},
	    {'O', NULL, edit_run_open_line_cmd},
	    {'o', NULL, edit_run_open_line_cmd},
	    {'X', NULL, operator_run_delete_or_substitute_cmd},
	    {'x', NULL, operator_run_delete_or_substitute_cmd},
	    {'s', NULL, operator_run_delete_or_substitute_cmd},
	    {'C', NULL, operator_run_change_or_delete_eol_cmd},
	    {'D', NULL, operator_run_change_or_delete_eol_cmd},
	    {'c', NULL, operator_run_change_delete_yank_cmd},
	    {'d', NULL, operator_run_change_delete_yank_cmd},
	    {'y', NULL, operator_run_change_delete_yank_cmd},
	    {'Y', NULL, operator_run_change_delete_yank_cmd},
	    {'<', NULL, do_shift},
	    {'>', NULL, do_shift},
	    {':', NULL, run_colon_cmd},
	    {'m', NULL, run_set_mark_cmd},
	    {'Z', NULL, run_zz_cmd},
	    {'g', NULL, run_g_prefix_cmd},
	    {'z', NULL, motion_run_scroll_to_screenpos_cmd},
	    {'r', NULL, operator_run_replace_char_cmd},
	    {'h', motion_run_left_cmd, NULL},
	    {KEYCODE_LEFT, motion_run_left_cmd, NULL},
	    {ASCII_CTRL_H, motion_run_left_cmd, NULL},
	    {ASCII_DEL, motion_run_left_cmd, NULL},
	    {' ', motion_run_right_cmd, NULL},
	    {'l', motion_run_right_cmd, NULL},
	    {KEYCODE_RIGHT, motion_run_right_cmd, NULL},
	    {ASCII_CTRL_G, run_status_mark_dirty_cmd, NULL},
	    {ASCII_CTRL_L, run_redraw_cmd, NULL},
	    {ASCII_CTRL_R, run_redo_last_cmd, NULL},
	    {'(', motion_run_prev_empty_line_cmd, NULL},
	    {')', motion_run_next_empty_line_cmd, NULL},
	    {ASCII_ESC, run_escape_cmd, NULL},
	    {ASCII_CTRL_J, motion_run_next_line_keep_col_cmd, NULL},
	    {'j', motion_run_next_line_keep_col_cmd, NULL},
	    {KEYCODE_DOWN, motion_run_next_line_keep_col_cmd, NULL},
	    {ASCII_CR, motion_run_next_line_skip_ws_cmd, NULL},
	    {'k', motion_run_prev_line_keep_col_cmd, NULL},
	    {KEYCODE_UP, motion_run_prev_line_keep_col_cmd, NULL},
	    {'-', motion_run_prev_line_skip_ws_cmd, NULL},
	    {';', motion_run_repeat_search_same_cmd, NULL},
	    {',', motion_run_repeat_search_reverse_cmd, NULL},
	    {'$', motion_run_line_end_cmd, NULL},
	    {KEYCODE_END, motion_run_line_end_cmd, NULL},
	    {'%', run_match_pair_cmd, NULL},
	    {'u', run_undo_last_cmd, NULL},
	    {'.', run_repeat_last_modifying_cmd, NULL},
	    {'U', run_undo_line_cmd, NULL},
	    {'^', motion_run_first_nonblank_cmd, NULL},
	    {'H', motion_run_screen_top_cmd, NULL},
	    {'L', motion_run_screen_bottom_cmd, NULL},
	    {'M', motion_run_screen_middle_cmd, NULL},
	    {'|', motion_run_goto_column_cmd, NULL},
	    {KEYCODE_HOME, dot_begin, NULL},
	    {KEYCODE_DELETE, run_delete_key_cmd, NULL},
	    {'w', run_word_forward_cmd, NULL},
	    {'b', run_word_backward_cmd, NULL},
	    {'e', run_word_end_cmd, NULL},
	    {'B', run_blank_word_backward_cmd, NULL},
	    {'E', run_blank_word_end_cmd, NULL},
	    {'W', run_blank_word_forward_cmd, NULL},
	    {'G', motion_run_goto_line_cmd, NULL},
	    {'a', edit_run_append_after_cmd, NULL},
	    {'A', edit_run_append_eol_cmd, NULL},
	    {'i', edit_run_insert_before_cmd, NULL},
	    {'I', edit_run_insert_before_first_nonblank_cmd, NULL},
	    {KEYCODE_INSERT, edit_run_insert_before_cmd, NULL},
	    {'R', edit_run_start_replace_cmd, NULL},
	    {'J', edit_run_join_lines_cmd, NULL},
	    {'~', operator_run_flip_case_cmd, NULL},
	    {'v', run_visual_char_mode_cmd, NULL},
	    {'V', run_visual_line_mode_cmd, NULL},
	    {ASCII_CTRL_V, run_visual_block_mode_cmd, NULL},
	    {'P', run_put_before_cmd, NULL},
	    {'p', run_put_after_cmd, NULL},
	    {'{', run_paragraph_bck_cmd, NULL},
	    {'}', run_paragraph_fwd_cmd, NULL},
	};

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		if (table[i].key == c) {
			if (table[i].ctx_fn)
				table[i].ctx_fn(g, ctx);
			else
				table[i].fn(g);
			return 1;
		}
	}

	return 0;
}

//----- Execute a Vi Command -----------------------------------
void
do_cmd(struct editor *g, int c, const struct cmd_ctx *ctx)
{
	/*
	 * == Execute one vi command character in the current editing mode ==
	 *
	 * The top-level command dispatcher.  Behaviour depends on cmd_mode:
	 *   2 (replace) — printable bytes overwrite the character at dot
	 *   1 (insert)  — printable bytes are inserted before dot
	 *   0 (normal)  — arrow/page keys go directly to dispatch; visual-mode
	 *                 commands and operator keys are handled inline; all
	 *                 other keys are routed through dispatch_cmd().
	 *
	 * After every command:
	 *   - Re-inserts the sentinel newline if the buffer is empty.
	 *   - Clamps dot to valid range.
	 *   - Calls check_context() if dot moved (updates jump context marks).
	 *   - Clears cmdcnt unless the key was a digit.
	 *   - Nudges dot off the trailing newline in Normal mode.
	 */

	char buf[12];
	char *orig_dot = g->dot;
	int cnt;

	memset(buf, '\0', sizeof(buf));
	g->keep_index = FALSE;
	g->cmd_error = FALSE;

	show_status_line(g);

	switch (c) {
	case KEYCODE_UP:
	case KEYCODE_DOWN:
	case KEYCODE_LEFT:
	case KEYCODE_RIGHT:
	case KEYCODE_HOME:
	case KEYCODE_END:
	case KEYCODE_PAGEUP:
	case KEYCODE_PAGEDOWN:
	case KEYCODE_DELETE:
		goto key_cmd_mode;
	}

	if (g->cmd_mode == 2) {
		if (c == KEYCODE_INSERT) {
			edit_run_start_insert_cmd(g);
			goto dc1;
		}
		if (*g->dot == '\n') {
			g->cmd_mode = 1; /* past EOL: fall into insert */
			undo_queue_commit(g);
		} else {
			if (1 <= c || Isprint(c)) {
				if (c != 27 && !(c == g->term_orig.c_cc[VERASE] || c == 8 || c == 127)) {
					char *start = cp_start(g, g->dot);
					char *stop = cp_end(g, start) - 1;
					g->dot = yank_delete_current(g, start, stop, PARTIAL, YANKDEL, ALLOW_UNDO);
				}
				g->dot = char_insert(g, g->dot, c, ALLOW_UNDO_CHAIN);
			}
			goto dc1;
		}
	}
	if (g->cmd_mode == 1) {
		if (c == KEYCODE_INSERT) { /* Insert key again = enter replace mode */
			edit_run_start_replace_cmd(g);
			goto dc1;
		}
		if (1 <= c || Isprint(c)) {
			g->dot = char_insert(g, g->dot, c, ALLOW_UNDO_QUEUED);
		}
		goto dc1;
	}

	if (g->visual_mode) {
		if (c == ASCII_ESC) {
			visual_leave(g);
			reset_ydreg(g);
			goto dc1;
		}
		if (c == 'v') {
			if (g->visual_mode == 1)
				visual_leave(g);
			else
				g->visual_mode = 1;
			g->last_status_cksum = 0;
			goto dc1;
		}
		if (c == 'V') {
			if (g->visual_mode == 2)
				visual_leave(g);
			else {
				g->visual_mode = 2;
				g->visual_anchor =
				    begin_line(g, g->visual_anchor ? g->visual_anchor : g->dot);
				g->dot = begin_line(g, g->dot);
				g->last_status_cksum = 0;
			}
			goto dc1;
		}
		if (c == ASCII_CTRL_V) {
			if (g->visual_mode == 3)
				visual_leave(g);
			else
				g->visual_mode = 3;
			g->last_status_cksum = 0;
			goto dc1;
		}
		if (in_set(c, "oO")) {
			char *tmp = g->dot;
			g->dot = g->visual_anchor;
			g->visual_anchor = tmp;
			if (g->visual_mode == 2) {
				g->dot = begin_line(g, g->dot);
				g->visual_anchor = begin_line(g, g->visual_anchor);
			}
			goto dc1;
		}
		if (in_set(c, "dcyxpUuC<>")) {
			visual_apply_operator(g, c);
			goto dc1;
		}
		if ((unsigned)c < ASCII_DEL && strchr(modifying_cmds, c)) {
			indicate_error(g);
			goto dc1;
		}
		if (dispatch_cmd(g, c, ctx))
			goto dc1;
		if (c == '\'') {
			run_jump_mark_cmd(g, &orig_dot, ctx);
			goto dc1;
		}
		buf[0] = c;
		buf[1] = '\0';
		not_implemented(g, buf);
		goto dc1;
	}

	/*
	 * NORMAL MODE PARSING
	 */
key_cmd_mode:
	if (dispatch_cmd(g, c, ctx))
		goto dc1;
	if (c == '\'') {
		run_jump_mark_cmd(g, &orig_dot, ctx);
		goto dc1;
	}
	if (c != 0x00) {
		buf[0] = c;
		buf[1] = '\0';
		not_implemented(g, buf);
		reset_ydreg(g);
	}

dc1:
	if (g->end == g->text) {
		/* buffer became empty — restore the sentinel newline */
		char_insert(g, g->text, '\n', NO_UNDO);
		g->dot = g->text;
	}
	/* dot may equal end (one past last char); otherwise clamp it */
	if (g->dot != g->end)
		g->dot = bound_dot(g, g->dot);
	if (g->dot != orig_dot)
		check_context(g, c);

	if (!isdigit(c))
		g->cmdcnt = 0;
	cnt = g->dot - begin_line(g, g->dot);
	/* keep dot off the newline in normal mode */
	if (*g->dot == '\n' && cnt > 0 && g->cmd_mode == 0)
		g->dot = cp_prev(g, g->dot);
}

/*
 * cmd_ctx_from_parser — populate a cmd_ctx from a completed parser state.
 *
 * count and rcount default to 1 (the conventional "no explicit count" value
 * that handlers expect) when the parser accumulated no digits (m==0 / n==0).
 * raw_key carries KEYCODE_* and control chars when op is '\0'.
 */
static void
cmd_ctx_from_parser(struct cmd_ctx *ctx, const struct parser *s)
{
	/*
	 * == Populate a cmd_ctx from a completed parser state ==
	 *
	 * Extracts register, counts, operator, second operator, range count,
	 * range character, anchor, inline string, and raw key from the parser
	 * struct.  count and rcount default to 1 when the parser accumulated
	 * no digits, matching the "no explicit count" convention expected by
	 * command handlers.
	 */
	ctx->reg = s->reg;
	ctx->count = s->m ? s->m : 1;
	ctx->op = s->op;
	ctx->op2 = s->op2;
	ctx->rcount = s->n ? s->n : 1;
	ctx->rg = s->rg;
	ctx->anchor = s->a;
	ctx->str = s->b;
	ctx->raw_key = s->raw_key;
}

void
edit_file(struct editor *g, char *fn)
{
	/*
	 * == Open fn and run the interactive edit loop until :q / ZZ ==
	 *
	 * Initialises raw mode, queries terminal dimensions, allocates the screen
	 * shadow buffer, and loads fn into the text buffer.  Installs signal
	 * handlers (SIGWINCH, SIGTSTP, SIGINT) and sets the sigsetjmp restart
	 * point so ^C can abort any in-progress command.
	 *
	 * Main loop (while editing > 0):
	 *   1. Process any deferred signals.
	 *   2. Read one character (from ioq_start replay queue or terminal).
	 *   3. Snapshot the current line for 'U' undo if dot moved to a new line.
	 *   4. Handle visual-mode prefix sequences ('"', '+', 'a'/'i') inline.
	 *   5. In Normal mode, feed the character through the MONRAS parser.
	 *      When complete: extract cmd_ctx and call do_cmd().
	 *   6. In Insert/Replace mode, bypass the parser and call do_cmd() directly.
	 *   7. Refresh the screen and status line (skipped if input is queued).
	 *
	 * On exit, moves the cursor to the bottom of the screen and restores
	 * cooked terminal mode.
	 */
	int c;
	int sig;

	g->editing = 1;
	rawmode(g);
	term_cursor_shape_init_and_set();
	g->rows = 24;
	g->columns = 80;
	g->get_rowcol_error = query_screen_dimensions(g);
	if (g->get_rowcol_error /* TODO? && no input on stdin */) {
		uint64_t k;
		fputs(ESC "[999;999H" ESC "[6n", stdout);
		fflush(NULL);
		k = safe_read_key(STDIN_FILENO, g->readbuffer, /*timeout_ms:*/ 100);
		if ((int32_t)k == KEYCODE_CURSOR_POS) {
			uint32_t rc = (k >> 32);
			g->columns = (rc & CSI_COORD_MASK);
			if (g->columns > MAX_SCR_COLS)
				g->columns = MAX_SCR_COLS;
			g->rows = ((rc >> 16) & CSI_COORD_MASK);
			if (g->rows > MAX_SCR_ROWS)
				g->rows = MAX_SCR_ROWS;
		}
	}
	new_screen(g, g->rows, g->columns);
	init_text_buffer(g, fn);

	g->ydreg = 26;
	g->mark[MARK_CONTEXT] = g->mark[MARK_PREV_CONTEXT] = g->text;
	g->crow = 0;
	g->ccol = 0;

	signal(SIGWINCH, winch_handler);
	signal(SIGTSTP, tstp_handler);
	sig = sigsetjmp(g->restart, 1);
	if (sig != 0) {
		g->screenbegin = g->dot = g->text;
	}
	/* int_handler() longjmps to restart; install after restart is ready */
	signal(SIGINT, int_handler);

	g->cmd_mode = 0;
	g->visual_mode = 0;
	g->visual_anchor = NULL;
	g->cmdcnt = 0;
	g->offset = 0;
	c = '\0';
	free(g->ioq_start);
	g->ioq_start = NULL;
	g->adding2q = 0;
	initparser(&g->cmd_parser);

	run_initial_cmds(g);
	redraw(g, FALSE);
	while (g->editing > 0) {
		process_pending_signals(g);
		c = get_one_char(g);
		process_pending_signals(g);
		/* save a copy of the current line for the 'U' command */
		if (begin_line(g, g->dot) != g->dot_line) {
			g->dot_line = begin_line(g, g->dot);
			text_yank(g, begin_line(g, g->dot), end_line(g, g->dot), ureg,
			          PARTIAL);
		}

		/*
		 * Visual-mode two-char prefix/text-object accumulation.
		 *
		 * Collect the second character of '"<reg>', '+<op>', and 'a'/'i'
		 * text-object sequences here (input phase) so that do_cmd never
		 * needs to call get_one_char mid-execution in visual mode.
		 *
		 * vis_ai_pending: set to 'a' or 'i' on first char; the next char
		 *   is the text-object anchor — call textobj_find_range directly.
		 * vis_reg_pending: set when '"' is seen; the next char is the
		 *   register name.  After resolving the register, loop back so
		 *   the following char is processed as the operator.
		 * '+' sets SHARED_REG then loops back for the operator char.
		 */
		if (g->cmd_mode == 0 && g->visual_mode) {
			if (g->vis_ai_pending) {
				char *start, *stop;
				int buftype;
				if (textobj_find_range(g, g->vis_ai_pending, c,
				                       &start, &stop, &buftype) == 0) {
					g->visual_mode = 1;
					g->visual_anchor = cp_start(g, start);
					g->dot = cp_start(g, stop);
					g->last_status_cksum = 0;
				}
				g->vis_ai_pending = 0;
				goto vis_done;
			}
			if (g->vis_reg_pending) {
				if (c == '+') {
					g->ydreg = SHARED_REG;
				} else {
					int ri = (c | 0x20) - 'a';
					if ((unsigned)ri <= 25)
						g->ydreg = (unsigned)ri;
					else
						indicate_error(g);
				}
				g->vis_reg_pending = 0;
				continue;
			}
			if (c == '"') {
				g->vis_reg_pending = 1;
				continue;
			}
			if (c == '+') {
				g->ydreg = SHARED_REG;
				continue;
			}
			if (in_set(c, "ai")) {
				g->vis_ai_pending = c;
				continue;
			}
		}

		/*
		 * Normal-mode parser shim (MONRAS model).
		 *
		 * Feed each incoming char to the stateful parser.  While it
		 * returns 0 (needs more input), loop back for the next char.
		 * When it returns 1 (done):
		 *   ok==1: extract register/count/op into cmd_ctx and call do_cmd.
		 *   ok==0: parser rejected the sequence — ring bell and restart.
		 *
		 * Insert, replace, and visual modes bypass the parser entirely.
		 */
		struct cmd_ctx ctx;
		struct cmd_ctx *ctx_p = NULL;

		if (g->cmd_mode == 0 && !g->visual_mode) {
			int pc = c;
			/* STG_STRING terminates on '\0'; remap Enter/ESC so the
			 * real TTY stream (which never emits '\0') can terminate
			 * search and colon commands.
			 * Also handle backspace and show live feedback. */
			if (g->cmd_parser.stg == STG_STRING) {
				if (in_set(c, "\r\n\x1b")) { /* CR, LF, ESC */
					pc = '\0';
				} else if (c == g->term_orig.c_cc[VERASE] ||
				           c == ASCII_CTRL_H || c == ASCII_DEL) {
					/* erase previous codepoint from parser buffer */
					if (g->cmd_parser.k > 0) {
						const char *p = stepbwd(
						    g->cmd_parser.b + g->cmd_parser.k,
						    g->cmd_parser.b);
						g->cmd_parser.k = (int)(p - g->cmd_parser.b);
						g->cmd_parser.b[g->cmd_parser.k] = '\0';
					}
					go_bottom_and_clear_to_eol(g);
					putchar(g->cmd_parser.op);
					fputs(g->cmd_parser.b, stdout);
					fflush(NULL);
					continue;
				}
			}
			if (!parse(&g->cmd_parser, pc)) {
				/* show live prompt feedback for colon/search commands */
				if (g->cmd_parser.stg == STG_STRING) {
					go_bottom_and_clear_to_eol(g);
					putchar(g->cmd_parser.op);
					fputs(g->cmd_parser.b, stdout);
					fflush(NULL);
				}
				continue; /* need more input */
			}

			if (g->cmd_parser.ok) {
				cmd_ctx_from_parser(&ctx, &g->cmd_parser);
				/* resolve register */
				if (ctx.reg) {
					if (ctx.reg == '+') {
						g->ydreg = SHARED_REG;
					} else {
						int ri = (ctx.reg | 0x20) - 'a';
						if ((unsigned)ri <= 25)
							g->ydreg = (unsigned)ri;
					}
				}
				/* resolve count */
				if (g->cmd_parser.m)
					g->cmdcnt = g->cmd_parser.m;
				/* synthesise the key do_cmd expects */
				c = ctx.op ? (int)(unsigned char)ctx.op : ctx.raw_key;
				ctx_p = &ctx;
			} else {
				/* parser rejected: unknown command — bell and restart */
				indicate_error(g);
				initparser(&g->cmd_parser);
				continue;
			}
		}

		/* before do_cmd: arm insert-text capture for insert-entering ops */
		if (ctx_p != NULL) {
			g->adding2q = in_set(ctx.op, "aAcCiIoORs") ? 1 : 0;
			if (g->adding2q)
				g->lmc_len = 0; /* fresh capture for insert-entering command */
		}
		{
			int pre_modified = g->modified_count;
			do_cmd(g, c, ctx_p);
			/* snapshot for '.' repeat when the buffer was changed.
			 * Skip for '.' itself — run_repeat_last_modifying_cmd
			 * handles its own snapshot to preserve the original ctx. */
			if (ctx_p != NULL && g->modified_count != pre_modified &&
			    ctx.op != '.') {
				g->last_cmd_ctx = ctx;
				g->has_last_cmd_ctx = 1;
				if (!g->adding2q)
					g->lmc_len = 0; /* pure modify: no insert text to replay */
			}
		}
		if (ctx_p != NULL)
			initparser(&g->cmd_parser);
	vis_done:
		/* skip redraw if input is already queued */
		if (!g->readbuffer[0] && mysleep(0) == 0) {
			refresh(g, FALSE);
			show_status_line(g);
			term_cursor_shape_update_for_mode(g->cmd_mode);
		}
	}
	go_bottom_and_clear_to_eol(g);
	cookmode(g);
}

int
main(int argc, char **argv)
{
	/*
	 * == Program entry point: parse CLI options and start the editor session ==
	 *
	 * Initialises the global editor state, parses command-line options
	 * (including -c, -R, -s flags and initial file list), then hands off
	 * to run_editor_session() which calls edit_file() for each file.
	 * Returns 0 on normal exit, 1 if option parsing fails.
	 */
	struct editor *g = &vi_g;
	struct cli_options opts;
	int arg_index;
	init_globals(g);

	arg_index = parse_cli_options(g, argc, argv, &opts);
	if (arg_index < 0) {
		show_usage();
		return 1;
	}
	if (apply_cli_options(g, &opts) < 0)
		return 1;

	argv += arg_index;
	g->cmdline_filecnt = argc - arg_index;
	run_editor_session(g, argv);
	return 0;
}
