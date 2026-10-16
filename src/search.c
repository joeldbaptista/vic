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
 *                            returns the target offset or -1.
 *
 * get_input_line()      — prompts for user input on the status line
 *                            (used for :, /, ? prompts).
 *
 * Search wraps at buffer boundaries and reports "not found" via
 * indicate_error and status_line_bold.
 */
#include "search.h"

#include "codepoint.h"
#include "input.h"
#include "line.h"
#include "regex.h"
#include "screen.h"
#include "status.h"
#include "term.h"
#include "text.h"
#include "utf8.h"

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
	/*
	 * == Read a line of text from the user on the status line ==
	 *
	 * Displays prompt at the bottom of the screen and accumulates
	 * characters into g->input_buf.  Backspace removes the last codepoint
	 * (but not past the prompt start).  Enter, ESC, or MAX_INPUT_LEN
	 * terminates input.  Returns g->input_buf (always valid).
	 *
	 * Used as the input backend for /, ?, and : commands.
	 */
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

int
char_search(struct editor *g, int p, const char *pat,
            int dir_and_range)
{
	/*
	 * == Regex search for pat starting at p, forward or backward ==
	 *
	 * dir_and_range encodes direction and scope:
	 *   positive → forward from p to end-of-buffer (or end-of-line if
	 *              the FULL bit is clear)
	 *   negative → backward; finds the last match before p
	 *
	 * For backward searches, scans line by line and keeps re-matching
	 * within each segment to find the rightmost occurrence.
	 * Each line segment is copied out of the text, matched, and the match
	 * offsets are translated back to text offsets.
	 * Returns the offset of the match start in the buffer, or -1 if no
	 * match is found.  The pattern is compiled with REG_EXTENDED |
	 * REG_NEWLINE, plus REG_ICASE when ignorecase is set.
	 */
	regex_t preg;
	regmatch_t m;
	int flags;
	int full;
	int region_start;
	int region_end;
	int line;
	int eol;
	int seg_end;
	int result = -1;
	int cur;
	char *buf;

	flags = REG_EXTENDED | REG_NEWLINE;
	if (g->setops & VI_IGNORECASE)
		flags |= REG_ICASE;
	if (regcomp(&preg, pat, flags) != 0) {
		status_line_bold(g, "bad search pattern '%s'", pat);
		return p;
	}

	full = dir_and_range & 1;

	if (dir_and_range > 0) {
		/* Forward: find first match at or after p. */
		region_end = full ? text_len(g) - 1 : next_line(g, p);
		for (line = p; line < region_end; line = next_line(g, eol)) {
			int eflags = 0;

			eol = end_line(g, line);
			if (eol > region_end)
				eol = region_end;
			buf = text_copy(g, line, eol - line);
			if (line != 0 && text_char(g, line - 1) != '\n')
				eflags = REG_NOTBOL;
			if (regexec(&preg, buf, 1, &m, eflags) == 0) {
				result = line + m.rm_so;
				free(buf);
				break;
			}
			free(buf);
		}
	} else {
		/* Backward: find last match before p. */
		region_start = full ? 0 : prev_line(g, p);
		for (line = region_start; line < p; line = next_line(g, eol)) {
			eol = end_line(g, line);
			seg_end = eol < p ? eol : p;
			buf = text_copy(g, line, seg_end - line);
			if (regexec(&preg, buf, 1, &m, 0) == 0) {
				/* Walk forward within segment; keep last match. */
				cur = line;
				do {
					result = cur + m.rm_so;
					cur = result +
					    (m.rm_eo > m.rm_so ? m.rm_eo - m.rm_so : 1);
				} while (cur < seg_end &&
				         regexec(&preg, buf + (cur - line), 1, &m,
				                 REG_NOTBOL) == 0);
			}
			free(buf);
		}
	}

	regfree(&preg);
	return result;
}

static char *
make_word_search_pattern(struct editor *g, char dir_prefix,
                         int whole_word)
{
	/*
	 * == Build a search pattern string for the word under the cursor ==
	 *
	 * Finds the extent of the word at g->dot (alnum + underscore), then
	 * builds a heap-allocated string of the form:
	 *   dir_prefix + [\<] + word + [\>]
	 * where dir_prefix is '/' or '?' and the word-boundary anchors are
	 * included only when whole_word is set and the word is ASCII-only.
	 *
	 * Returns NULL if dot is not on a word character.  The caller is
	 * responsible for freeing the returned string.
	 */
	int p = cp_start(g, g->dot);
	int start;
	int end;
	char *pattern;
	char *dst;
	size_t word_len;

	if (p >= text_len(g) || text_char(g, p) == '\n' ||
	    !is_word_byte((unsigned char)text_char(g, p)))
		return NULL;

	start = p;
	while (start > 0) {
		int prev = cp_prev(g, start);
		if (!is_word_byte((unsigned char)text_char(g, prev)))
			break;
		start = prev;
	}

	end = cp_next(g, p);
	while (end < text_len(g) && text_char(g, end) != '\n' &&
	       is_word_byte((unsigned char)text_char(g, end)))
		end = cp_next(g, end);

	word_len = (size_t)(end - start);
	/*
	 * \< and \> are POSIX BRE word-boundary anchors whose definition of
	 * "word character" is [[:alnum:]_] — ASCII-only in the C locale.
	 * For words containing multi-byte UTF-8 characters (Georgian, Russian,
	 * Greek, …) the anchors never match, so suppress them.
	 */
	if (whole_word && (unsigned char)text_char(g, start) >= UTF8_MULTIBYTE_MIN)
		whole_word = 0;
	pattern = xmalloc(word_len + (whole_word ? 6 : 2));
	dst = pattern;
	*dst++ = dir_prefix;
	if (whole_word) {
		*dst++ = '\\';
		*dst++ = '<';
	}
	text_read(g, start, (int)word_len, dst);
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
	/*
	 * == Implement *, #, g*, g# — search for the word under the cursor ==
	 *
	 * Builds a pattern for the word at dot, stores it in
	 * g->last_search_pattern, then delegates to search_run_cmd.
	 * c == '*' or 'g*' → forward search ('/'); '#' or 'g#' → backward ('?').
	 * whole_word=1 adds \< \> word-boundary anchors; whole_word=0 omits them
	 * (g* and g# variants).
	 */
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
	/*
	 * == Execute a search command: /, ?, n, N ==
	 *
	 * For / and ?: prompts interactively (or uses the pre-parsed string
	 * from the parser), updates g->last_search_pattern, then normalises
	 * to 'n'.  For n/N: determines direction from the stored pattern
	 * prefix ('/' = forward, '?' = backward; N reverses).
	 *
	 * Searches forward by stepping one codepoint past dot.  On failure
	 * wraps around from the opposite buffer end.  Reports wrap-around and
	 * "Pattern not found" via status_line_bold.  Repeats g->cmdcnt times.
	 */
	int q;
	char *input;
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

			input = get_input_line(g, prompt);
			if (input == NULL || !input[0])
				return;
			if (!input[1]) {
				if (g->last_search_pattern[0])
					g->last_search_pattern[0] = (char)c;
			} else {
				free(g->last_search_pattern);
				g->last_search_pattern = xstrdup(input);
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
		int search_from =
		    (dir == FORWARD) ? cp_next(g, g->dot) : cp_prev(g, g->dot);
		q = char_search(g, search_from, g->last_search_pattern + 1,
		                (dir * 2) | FULL);
		if (q != -1) {
			g->dot = q;
		} else {
			const char *msg;
			q = char_search(g, dir == FORWARD ? 0 : text_len(g) - 1,
			                g->last_search_pattern + 1, (dir * 2) | FULL);
			if (q != -1) {
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
