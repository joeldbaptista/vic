/*
 * wordmotion.c - word and WORD motion commands.
 *
 * Implements the six word motion commands:
 *   w / W  — forward to start of next word/WORD
 *   b / B  — backward to start of current/previous word/WORD
 *   e / E  — forward to end of current/next word/WORD
 *
 * Word boundaries use skip_thing() from scan.c with the appropriate
 * scan type (S_BEFORE_WS, S_TO_WS, S_OVER_WS, S_END_ALNUM, S_END_PUNCT).
 * WORD variants skip punctuation by treating any non-whitespace run as
 * one token (S_BEFORE_WS / S_OVER_WS only).
 */
#include "wordmotion.h"

#include "codepoint.h"
#include "scan.h"

enum {
	FORWARD = 1,
	BACK = -1,
	S_BEFORE_WS = 1,
	S_TO_WS = 2,
	S_OVER_WS = 3,
	S_END_PUNCT = 4,
	S_END_ALNUM = 5,
};

static int
is_ascii_space(unsigned char c)
{
	return c < UTF8_MULTIBYTE_MIN && isspace(c);
}

static int
is_word_byte(unsigned char c)
{
	return c >= UTF8_MULTIBYTE_MIN || isalnum(c) || c == '_';
}

static int
is_ascii_punct(unsigned char c)
{
	return c < UTF8_MULTIBYTE_MIN && ispunct(c);
}

void
run_word_forward_cmd(struct editor *g)
{
	/*
	 * == Move to the start of the next word (w) ==
	 *
	 * Skip to the end of the current alnum/punct run, step one codepoint
	 * past it, then skip any whitespace to land on the first character of
	 * the next word.  Handles the distinct case where dot starts on
	 * punctuation vs. an alphanumeric/underscore run.
	 *
	 * - Kept separate from run_word_dir_cmd because the step-then-skip
	 *   order differs from b and e.
	 */
	do {
		if (is_word_byte((unsigned char)*g->dot)) {
			g->dot = skip_thing(g, g->dot, 1, FORWARD, S_END_ALNUM);
		} else if (is_ascii_punct((unsigned char)*g->dot)) {
			g->dot = skip_thing(g, g->dot, 1, FORWARD, S_END_PUNCT);
		}
		if (g->dot < g->end - 1)
			g->dot = cp_next(g, g->dot);
		if (is_ascii_space((unsigned char)*g->dot)) {
			g->dot = skip_thing(g, g->dot, 2, FORWARD, S_OVER_WS);
		}
	} while (--g->cmdcnt > 0);
}

static void
run_word_dir_cmd(struct editor *g, int dir, int end_mode)
{
	/*
	 * == Shared implementation for b (backward) and e (end) ==
	 *
	 * Steps one codepoint in dir, skips any whitespace, then skips to the
	 * end of the current alnum/punct run.  end_mode controls where the
	 * cursor lands:
	 *
	 * - end_mode = 1 (e): skip-count 2 over whitespace so the cursor
	 *   lands on the last char of the following word.
	 * - end_mode = 0 (b): skip-count 1 so the cursor stops at the first
	 *   non-space char after retreating.
	 */
	do {
		char *adj =
		    (dir == FORWARD) ? cp_next(g, g->dot) : cp_prev(g, g->dot);
		if (adj < g->text || adj > g->end - 1)
			break;
		g->dot = adj;
		if (is_ascii_space((unsigned char)*g->dot)) {
			g->dot = skip_thing(g, g->dot, end_mode ? 2 : 1, dir, S_OVER_WS);
		}
		if (is_word_byte((unsigned char)*g->dot)) {
			g->dot = skip_thing(g, g->dot, 1, dir, S_END_ALNUM);
		} else if (is_ascii_punct((unsigned char)*g->dot)) {
			g->dot = skip_thing(g, g->dot, 1, dir, S_END_PUNCT);
		}
	} while (--g->cmdcnt > 0);
}

void
run_word_backward_cmd(struct editor *g)
{
	/*
	 * == Move to the start of the current or previous word (b) ==
	 */
	run_word_dir_cmd(g, BACK, 0);
}

void
run_word_end_cmd(struct editor *g)
{
	/*
	 * == Move to the end of the current or next word (e) ==
	 */
	run_word_dir_cmd(g, FORWARD, 1);
}

static void
run_blank_word_cmd(struct editor *g, int c)
{
	/*
	 * == Implements upper-case word motions ==
	 *
	 * Upper-case word motions consider punctuations as letters.
	 *
	 * - W moves cursor to the beginning of next word
	 * - B moves cursor to the beginning of previous word
	 * - E mvoes cursor to the end of current or next word
	 */
	int dir = FORWARD;

	if (c == 'B')
		dir = BACK;
	do {
		char *adj =
		    (dir == FORWARD) ? cp_next(g, g->dot) : cp_prev(g, g->dot);
		unsigned char adjc =
		    (adj >= g->text && adj < g->end) ? (unsigned char)*adj : '\n';
		if (c == 'W' || is_ascii_space(adjc)) {
			g->dot = skip_thing(g, g->dot, 1, dir, S_TO_WS);
			g->dot = skip_thing(g, g->dot, 2, dir, S_OVER_WS);
		}
		if (c != 'W')
			g->dot = skip_thing(g, g->dot, 1, dir, S_BEFORE_WS);
	} while (--g->cmdcnt > 0);
}

void
run_blank_word_backward_cmd(struct editor *g)
{
	/*
	 * == Move to the start of the current or previous WORD (B) ==
	 */
	run_blank_word_cmd(g, 'B');
}

void
run_blank_word_end_cmd(struct editor *g)
{
	/*
	 * == Move to the end of the current or next WORD (E) ==
	 */
	run_blank_word_cmd(g, 'E');
}

void
run_blank_word_forward_cmd(struct editor *g)
{
	/*
	 * == Move to the start of the next WORD (W) ==
	 */
	run_blank_word_cmd(g, 'W');
}
