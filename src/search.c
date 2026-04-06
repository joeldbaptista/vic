/*
 * search.c - regex search and char-search commands.
 *
 * search_run_cmd()      — handles / ? n N: prompts for a pattern (or
 *                            reuses g->last_search_pattern), compiles it
 *                            with regcomp, and scans forward or backward
 *                            through the buffer for the next match.
 *
 * search_run_word_cmd() — implements * # g* g#: builds a pattern from
 *                            the word under the cursor and calls the search
 *                            machinery.
 *
 * char_search()         — character search within a line (f F t T);
 *                            returns the target pointer or NULL.
 *
 * get_input_line()      — prompts for user input on the status line
 *                            (used for :, /, ? prompts).
 *
 * Search wraps at buffer boundaries and reports "not found" via
 * indicate_error and status_line_bold.
 */
#define _GNU_SOURCE
#include "search.h"

#include "codepoint.h"
#include "input.h"
#include "line.h"
#include "screen.h"
#include "status.h"
#include "term.h"
#include "utf8.h"
#include <regex.h>

enum {
	FORWARD = 1,
	BACK = -1,
	FULL = 1,
	MAX_INPUT_LEN = 128,
};

static int
is_word_byte(unsigned char c)
{
	return c >= UTF8_MULTIBYTE_MIN || isalnum(c) || c == '_';
}

char *
get_input_line(struct editor *g, const char *prompt)
{
	char *buf = g->input_buf;
	int c;
	int i;

	strcpy(buf, prompt);
	g->last_status_cksum = 0;
	go_bottom_and_clear_to_eol(g);
	fputs(buf, stdout);

	i = strlen(buf);
	while (i < MAX_INPUT_LEN - 1) {
		c = get_one_char(g);
		if (c == '\n' || c == '\r' || c == ASCII_ESC)
			break;
		if (c == g->term_orig.c_cc[VERASE] || c == 8 || c == 127) {
			const char *p = stepbwd(buf + i, buf);
			i = (int)(p - buf);
			buf[i] = '\0';
			go_bottom_and_clear_to_eol(g);
			if (i <= 0) /* backed past start of prompt */
				break;
			fputs(buf, stdout);
		} else if (c > 0 && c < 256) {
			buf[i] = c;
			buf[++i] = '\0';
			putchar(c);
		}
	}
	refresh(g, FALSE);

	return buf;
}

char *
char_search(struct editor *g, char *p, const char *pat,
            int dir_and_range)
{
	struct re_pattern_buffer preg;
	const char *err;
	char *q;
	int i;
	int size;
	int range;
	int start;

	re_syntax_options = RE_SYNTAX_POSIX_BASIC & (~RE_DOT_NEWLINE);
	if (g->setops & VI_IGNORECASE)
		re_syntax_options |= RE_ICASE;

	memset(&preg, 0, sizeof(preg));
	err = re_compile_pattern(pat, strlen(pat), &preg);
	preg.not_bol = p != g->text;
	preg.not_eol = p != g->end - 1;
	if (err != NULL) {
		status_line_bold(g, "bad search pattern '%s': %s", pat, err);
		return p;
	}

	range = (dir_and_range & 1);
	q = g->end - 1;
	if (range == 0)
		q = next_line(g, p);
	if (dir_and_range < 0) {
		q = g->text;
		if (range == 0)
			q = prev_line(g, p);
	}

	range = q - p;
	if (range < 0) {
		size = -range;
		start = size;
	} else {
		size = range;
		start = 0;
	}
	q = p - start;
	if (q < g->text)
		q = g->text;

	i = re_search(&preg, q, size, start, range, NULL);
	regfree(&preg);

	return i < 0 ? NULL : q + i;
}

static char *
make_word_search_pattern(struct editor *g, char dir_prefix,
                         int whole_word)
{
	char *p = cp_start(g, g->dot);
	char *start;
	char *end;
	char *pattern;
	char *dst;
	size_t word_len;

	if (p >= g->end || *p == '\n' || !is_word_byte((unsigned char)*p))
		return NULL;

	start = p;
	while (start > g->text) {
		char *prev = cp_prev(g, start);
		if (!is_word_byte((unsigned char)*prev))
			break;
		start = prev;
	}

	end = cp_next(g, p);
	while (end < g->end && *end != '\n' && is_word_byte((unsigned char)*end))
		end = cp_next(g, end);

	word_len = (size_t)(end - start);
	/*
	 * \< and \> are POSIX BRE word-boundary anchors whose definition of
	 * "word character" is [[:alnum:]_] — ASCII-only in the C locale.
	 * For words containing multi-byte UTF-8 characters (Georgian, Russian,
	 * Greek, …) the anchors never match, so suppress them.
	 */
	if (whole_word && (unsigned char)*start >= UTF8_MULTIBYTE_MIN)
		whole_word = 0;
	pattern = xmalloc(word_len + (whole_word ? 6 : 2));
	dst = pattern;
	*dst++ = dir_prefix;
	if (whole_word) {
		*dst++ = '\\';
		*dst++ = '<';
	}
	memcpy(dst, start, word_len);
	dst += word_len;
	if (whole_word) {
		*dst++ = '\\';
		*dst++ = '>';
	}
	*dst = '\0';

	return pattern;
}

void
search_run_word_cmd(struct editor *g, int c, int whole_word)
{
	struct cmd_ctx nctx;
	char *pattern = make_word_search_pattern(g, c == '*' ? '/' : '?', whole_word);

	if (!pattern) {
		status_line_bold(g, "No word under cursor");
		indicate_error(g);
		return;
	}

	free(g->last_search_pattern);
	g->last_search_pattern = pattern;
	memset(&nctx, 0, sizeof(nctx));
	nctx.op = 'n';
	nctx.count = 1;
	nctx.rcount = 1;
	search_run_cmd(g, &nctx);
}

void
search_run_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	char *q;
	int dir;
	int c = (int)(unsigned char)ctx->op;

	if (c == '?' || c == '/') {
		if (ctx->str) {
			/* string was captured by the parser */
			if (ctx->str[0]) {
				{
					size_t slen = strlen(ctx->str);
					char *p = xmalloc(slen + 2);
					p[0] = (char)c;
					memcpy(p + 1, ctx->str, slen + 1);
					free(g->last_search_pattern);
					g->last_search_pattern = p;
				}
			} else if (g->last_search_pattern && g->last_search_pattern[0]) {
				/* empty pattern: update direction only */
				g->last_search_pattern[0] = (char)c;
			}
		} else {
			/* interactive input (visual mode, no parser) */
			char prompt[2] = {(char)c, '\0'};

			q = get_input_line(g, prompt);
			if (q == NULL || !q[0])
				return;
			if (!q[1]) {
				if (g->last_search_pattern[0])
					g->last_search_pattern[0] = (char)c;
			} else {
				free(g->last_search_pattern);
				g->last_search_pattern = xstrdup(q);
			}
		}
		c = 'n';
	}

	if (c == 'N') {
		dir = g->last_search_pattern[0] == '/' ? BACK : FORWARD;
	} else {
		dir = g->last_search_pattern[0] == '/' ? FORWARD : BACK;
	}

	if (g->last_search_pattern[1] == '\0') {
		status_line_bold(g, "No previous search");
		return;
	}

	do {
		char *search_from =
		    (dir == FORWARD) ? cp_next(g, g->dot) : cp_prev(g, g->dot);
		q = char_search(g, search_from, g->last_search_pattern + 1,
		                (dir * 2) | FULL);
		if (q != NULL) {
			g->dot = q;
		} else {
			const char *msg;
			q = char_search(g, dir == FORWARD ? g->text : g->end - 1,
			                g->last_search_pattern + 1, (dir * 2) | FULL);
			if (q != NULL) {
				g->dot = q;
				msg = "search hit %s, continuing at %s";
			} else {
				g->cmdcnt = 0;
				msg = "Pattern not found";
			}
			if (dir == FORWARD)
				status_line_bold(g, msg, "BOTTOM", "TOP");
			else
				status_line_bold(g, msg, "TOP", "BOTTOM");
		}
	} while (--g->cmdcnt > 0);
}
