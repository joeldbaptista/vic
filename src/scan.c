/*
 * scan.c - low-level token/whitespace scanner.
 *
 * Provides skip_thing(), which moves a pointer forward or backward by
 * linecnt lines according to a scan type:
 *
 *   S_BEFORE_WS  — stop before whitespace
 *   S_TO_WS      — stop at first whitespace
 *   S_OVER_WS    — skip whitespace
 *   S_END_PUNCT  — stop at end of punctuation run
 *   S_END_ALNUM  — stop at end of alnum/underscore run
 *
 * Used exclusively by wordmotion.c to implement the word-boundary logic
 * for w/b/e/W/B/E.
 *
 * No hooks: cp_next and cp_prev are called directly from codepoint.h.
 */
#include "scan.h"

#include "codepoint.h"

enum {
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

static int
st_test(struct editor *g, char *p, int type, int dir, char *tested)
{
	char c;
	unsigned char c0;
	unsigned char ci;
	int test;
	int inc;
	char *pi;

	inc = dir;
	c0 = (unsigned char)*p;
	if (inc >= 0) {
		pi = cp_next(g, p);
		ci = (pi < g->end) ? (unsigned char)*pi : '\n';
	} else {
		pi = (p > g->text) ? cp_prev(g, p) : g->text;
		ci = (pi >= g->text) ? (unsigned char)*pi : '\n';
	}
	c = (char)c0;
	test = 0;

	if (type == S_BEFORE_WS) {
		c = (char)ci;
		test = (!is_ascii_space(ci) || ci == '\n');
	}
	if (type == S_TO_WS) {
		c = (char)c0;
		test = (!is_ascii_space(c0) || c0 == '\n');
	}
	if (type == S_OVER_WS) {
		c = (char)c0;
		test = is_ascii_space(c0);
	}
	if (type == S_END_PUNCT) {
		c = (char)ci;
		test = is_ascii_punct(ci);
	}
	if (type == S_END_ALNUM) {
		c = (char)ci;
		test = is_word_byte(ci);
	}
	*tested = c;
	return test;
}

char *
skip_thing(struct editor *g, char *p, int linecnt, int dir, int type)
{
	char c;

	while (st_test(g, p, type, dir, &c)) {
		if (c == '\n' && --linecnt < 1)
			break;
		if (dir >= 0 && p >= g->end - 1)
			break;
		if (dir < 0 && p <= g->text)
			break;
		p = (dir >= 0) ? cp_next(g, p) : cp_prev(g, p);
	}

	return p;
}
