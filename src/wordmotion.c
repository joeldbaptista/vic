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
 *
 * No hooks: cp_next/cp_prev from codepoint.h and skip_thing from scan.h
 * are called directly.
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
	run_word_dir_cmd(g, BACK, 0);
}

void
run_word_end_cmd(struct editor *g)
{
	run_word_dir_cmd(g, FORWARD, 1);
}

static void
run_blank_word_cmd(struct editor *g, int c)
{
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
	run_blank_word_cmd(g, 'B');
}

void
run_blank_word_end_cmd(struct editor *g)
{
	run_blank_word_cmd(g, 'E');
}

void
run_blank_word_forward_cmd(struct editor *g)
{
	run_blank_word_cmd(g, 'W');
}
