/*
 * run.c - `:run` command dispatch table and built-in commands.
 *
 * run_dispatch() tokenises the argument string and calls the matching
 * run_fn from run_table[].  Add new commands by appending entries there.
 *
 * Every command receives its range as text offsets [rs, re] (inclusive).
 * Commands that read the range copy it out of the text with text_copy and
 * work on the copy; positions written back stay offsets.
 *
 * Built-in commands
 * -----------------
 *   echo [args…]             — print args to the status bar
 *   wc                       — count lines, words, bytes in range
 *   convert c2s|s2c|s2uc     — identifier case conversion
 *   upper / lower            — uppercase / lowercase all text in range
 *   trim                     — strip trailing whitespace from each line
 *   trimsel [0|1|2]          — strip whitespace from selection: 0=both(default),1=begin,2=end
 *   uniq                     — remove consecutive duplicate lines
 *   sort [-r]                — sort lines (reverse with -r)
 *   wrap N                   — hard-wrap lines at column N
 *   number                   — prepend line numbers
 *   deindent [N]             — remove N indent levels (default 1)
 *   align CHAR               — align lines to first occurrence of CHAR
 *   urlencode / urldecode    — percent-encode / decode range
 *   base64enc / base64dec    — base-64 encode / decode range
 *   jsonesc / jsonunesc      — JSON string escape / unescape range
 *   freq                     — top-5 word frequencies in range
 *   col                      — report max line width in range
 *   hash                     — FNV-1a hash of entire range
 *   hash mod N               — per-identifier hash%N; report collisions
 *   hash replace [mod N]     — replace identifiers with hash literals
 *   pipe <shell command>     — filter range through a shell command
 *   highlight [pattern]      — set or clear the viewport highlight pattern
 *   color-escape [on|off]    — render SGR escapes in the buffer as colour
 */
#include "run.h"
#include "buffer.h"
#include "ex.h"
#include "screen.h"
#include "status.h"
#include "text.h"
#include "undo.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- shared helpers ---------------------------------------------------- */

static int
is_ident_start(unsigned char c)
{
	/*
	 * == True if c can start a C-style identifier ==
	 */
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident(unsigned char c)
{
	/*
	 * == True if c can continue a C-style identifier ==
	 */
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* ---- upper / lower ---------------------------------------------------- */

static void
run_upper(struct editor *g, int argc, char *argv[],
          int rs, int re)
{
	/*
	 * == :run upper — uppercase all ASCII letters in the range ==
	 */
	int len = (int)(re - rs + 1);
	char *buf;
	int p;

	(void)argc;
	(void)argv;
	undo_push(g, rs, (unsigned)len, UNDO_SWAP);
	/* Change the bytes in one copy and write them back in one call: a
	 * text_set_char() per byte costs a tree descent and a piece split
	 * each, which is minutes of work over a whole large file. */
	buf = text_copy(g, rs, len);
	for (p = 0; p < len; p++)
		buf[p] = (char)toupper((unsigned char)buf[p]);
	text_set_bytes(g, rs, buf, len);
	free(buf);
}

static void
run_lower(struct editor *g, int argc, char *argv[],
          int rs, int re)
{
	/*
	 * == :run lower — lowercase all ASCII letters in the range ==
	 */
	int len = (int)(re - rs + 1);
	char *buf;
	int p;

	(void)argc;
	(void)argv;
	undo_push(g, rs, (unsigned)len, UNDO_SWAP);
	/* Change the bytes in one copy and write them back in one call: a
	 * text_set_char() per byte costs a tree descent and a piece split
	 * each, which is minutes of work over a whole large file. */
	buf = text_copy(g, rs, len);
	for (p = 0; p < len; p++)
		buf[p] = (char)tolower((unsigned char)buf[p]);
	text_set_bytes(g, rs, buf, len);
	free(buf);
}

/* ---- trim -------------------------------------------------------------- */

static void
run_trim(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run trim — strip trailing whitespace from each line in the range ==
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;

	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		char *line_start = out;
		char *last_non_ws = out - 1;

		while (p <= rbe && *p != '\n') {
			*out = *p++;
			if (*out != ' ' && *out != '\t')
				last_non_ws = out;
			out++;
		}
		/* truncate trailing whitespace */
		out = last_non_ws + 1;
		if (p <= rbe) {
			*out++ = '\n';
			p++;
		}
		(void)line_start;
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- trimsel ----------------------------------------------------------- */

static void
run_trimsel(struct editor *g, int argc, char *argv[],
            int rs, int re)
{
	/*
	 * == :run trimsel [0|1|2] — strip whitespace from the visual selection ==
	 *
	 * The ex range [rs,re] is always line-aligned, so we read MARK_LT and
	 * MARK_GT directly to get the exact visual byte boundaries.  Falls back
	 * to [rs,re] when no visual marks are set.
	 *
	 * The optional parameter controls which end(s) to strip:
	 *   0 (default) — strip both leading and trailing whitespace
	 *   1           — strip leading whitespace only
	 *   2           — strip trailing whitespace only
	 */
	int mode = 0;
	int start, end;

	if (argc >= 2)
		mode = (int)strtol(argv[1], NULL, 10);

	/* Use exact visual marks when available. */
	start = g->mark[MARK_LT] >= 0 ? g->mark[MARK_LT] : rs;
	end   = g->mark[MARK_GT] >= 0 ? g->mark[MARK_GT] : re;
	if (start > end) {
		int tmp = start;
		start = end;
		end   = tmp;
	}

	if (mode == 0 || mode == 1) {
		while (start <= end && (text_char(g, start) == ' ' ||
		                        text_char(g, start) == '\t' ||
		                        text_char(g, start) == '\n' ||
		                        text_char(g, start) == '\r'))
			start++;
	}
	if (mode == 0 || mode == 2) {
		while (end >= start && (text_char(g, end) == ' ' ||
		                        text_char(g, end) == '\t' ||
		                        text_char(g, end) == '\n' ||
		                        text_char(g, end) == '\r'))
			end--;
	}

	if (start > end) {
		/*
		 * The selection is all blanks: delete it.  Fall back to the
		 * range like the code above does; passing an unset mark here
		 * made undo_push() copy from NULL in the pointer version.
		 */
		buffer_replace_range(g,
		                     g->mark[MARK_LT] >= 0 ? g->mark[MARK_LT] : rs,
		                     g->mark[MARK_GT] >= 0 ? g->mark[MARK_GT] : re,
		                     "", 0);
		return;
	}

	{
		int sel_start = g->mark[MARK_LT] >= 0 ? g->mark[MARK_LT] : rs;
		int sel_end   = g->mark[MARK_GT] >= 0 ? g->mark[MARK_GT] : re;
		if (sel_start > sel_end) {
			int tmp = sel_start; sel_start = sel_end; sel_end = tmp;
		}
		if (start > sel_start || end < sel_end) {
			int new_len = (int)(end - start + 1);
			char *tmp = malloc((size_t)new_len);
			if (!tmp)
				return;
			text_read(g, start, new_len, tmp);
			buffer_replace_range(g, sel_start, sel_end, tmp,
			                     new_len);
			free(tmp);
		}
	}
}

/* ---- uniq -------------------------------------------------------------- */

static void
run_uniq(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run uniq — remove consecutive duplicate lines in the range ==
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	char *prev_line = NULL;
	int prev_len = 0;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;

	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		char *line = p;
		int len = 0;

		while (p <= rbe && *p != '\n') {
			p++;
			len++;
		}
		if (prev_line == NULL || len != prev_len ||
		    memcmp(line, prev_line, (size_t)len) != 0) {
			memcpy(out, line, (size_t)len);
			out += len;
			if (p <= rbe)
				*out++ = '\n';
			prev_line = line;
			prev_len = len;
		}
		if (p <= rbe)
			p++;
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- sort -------------------------------------------------------------- */

struct line_ref {
	char *start;
	int len;
};

static int
line_cmp_asc(const void *a, const void *b)
{
	/*
	 * == Ascending comparator for struct line_ref, used by run_sort ==
	 */
	const struct line_ref *la = a, *lb = b;
	int min_len = la->len < lb->len ? la->len : lb->len;
	int cmp = memcmp(la->start, lb->start, (size_t)min_len);
	return cmp != 0 ? cmp : la->len - lb->len;
}

static int
line_cmp_desc(const void *a, const void *b)
{
	/*
	 * == Descending comparator for struct line_ref (reverses line_cmp_asc) ==
	 */
	return line_cmp_asc(b, a);
}

static void
run_sort(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run sort [-r] — sort lines in the range lexicographically ==
	 *
	 * -r reverses the order.  Splits the range into line_ref structs,
	 * sorts with qsort(), then rebuilds it with buffer_replace_range.
	 */
	int reverse = (argc >= 2 && strcmp(argv[1], "-r") == 0);
	int range_len = (int)(re - rs + 1);
	int max_lines = range_len / 2 + 2;
	struct line_ref *lines = malloc((size_t)max_lines * sizeof(*lines));
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	int n = 0, i, new_len;

	if (!lines || !new_buf) {
		free(lines);
		free(new_buf);
		return;
	}

	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	p = rb;
	while (p <= rbe && n < max_lines) {
		lines[n].start = p;
		lines[n].len = 0;
		while (p <= rbe && *p != '\n') {
			p++;
			lines[n].len++;
		}
		n++;
		if (p <= rbe)
			p++;
	}

	qsort(lines, (size_t)n, sizeof(*lines),
	      reverse ? line_cmp_desc : line_cmp_asc);

	out = new_buf;
	for (i = 0; i < n; i++) {
		memcpy(out, lines[i].start, (size_t)lines[i].len);
		out += lines[i].len;
		if (i < n - 1)
			*out++ = '\n';
	}
	/* preserve trailing newline if original range ended with one */
	if (re >= rs && text_char(g, re) == '\n')
		*out++ = '\n';

	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(lines);
	free(new_buf);
}

/* ---- wrap -------------------------------------------------------------- */

static void
run_wrap(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run wrap N — hard-wrap each line in the range at column N ==
	 *
	 * Finds the last space at or before column N and breaks there; if no
	 * space exists, breaks at column N exactly.  Requires N >= 4.
	 */
	int col;
	int range_len = (int)(re - rs + 1);
	char *new_buf;
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	if (argc < 2 || (col = atoi(argv[1])) < 4) {
		status_line(g, "wrap: usage: wrap N  (N >= 4)");
		return;
	}

	/* worst case: every char becomes a line of 1 + newline */
	new_buf = malloc((size_t)range_len * 2 + 1);
	if (!new_buf)
		return;

	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		char *line = p;
		int len = 0;

		while (p <= rbe && *p != '\n') {
			p++;
			len++;
		}
		if (p <= rbe)
			p++; /* skip newline */

		/* wrap this line */
		while (len > col) {
			int brk = col;
			/* find last space at or before col */
			while (brk > 0 && line[brk] != ' ' && line[brk] != '\t')
				brk--;
			if (brk == 0)
				brk = col; /* no space: hard break */
			memcpy(out, line, (size_t)brk);
			out += brk;
			*out++ = '\n';
			/* skip the space we broke at */
			if (line[brk] == ' ' || line[brk] == '\t')
				brk++;
			line += brk;
			len -= brk;
		}
		memcpy(out, line, (size_t)len);
		out += len;
		*out++ = '\n';
	}
	free(rb);
	new_len = (int)(out - new_buf);
	/* strip extra trailing newline if original didn't end with one */
	if (new_len > 0 && new_buf[new_len - 1] == '\n' &&
	    (re < rs || text_char(g, re) != '\n'))
		new_len--;
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- number ------------------------------------------------------------ */

static void
run_number(struct editor *g, int argc, char *argv[],
           int rs, int re)
{
	/*
	 * == :run number — prepend sequential line numbers to each line ==
	 *
	 * Numbers are right-justified in a field wide enough for the last line
	 * number, followed by a tab.
	 */
	int range_len = (int)(re - rs + 1);
	int nlines = 1;
	char *scan, *new_buf, *p, *out;
	char *rb, *rbe;
	int lineno = 1, new_len, max_plen;

	(void)argc;
	(void)argv;

	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	for (scan = rb; scan <= rbe; scan++)
		if (*scan == '\n')
			nlines++;
	if (text_char(g, re) == '\n')
		nlines--;
	/* exact worst-case prefix width for the largest line number used */
	max_plen = snprintf(NULL, 0, "%d. ", nlines);
	new_buf = malloc((size_t)range_len + (size_t)nlines * (size_t)max_plen);
	if (!new_buf) {
		free(rb);
		return;
	}

	out = new_buf;
	p = rb;
	while (p <= rbe) {
		char prefix[16];
		int plen = snprintf(prefix, sizeof(prefix), "%d. ", lineno++);
		memcpy(out, prefix, (size_t)plen);
		out += plen;
		while (p <= rbe && *p != '\n')
			*out++ = *p++;
		if (p <= rbe) {
			*out++ = '\n';
			p++;
		}
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- deindent ---------------------------------------------------------- */

static void
run_deindent(struct editor *g, int argc, char *argv[],
             int rs, int re)
{
	/*
	 * == :run deindent [N] — remove N indent levels from each line ==
	 *
	 * Removes up to N leading tabs or g->tabstop-width space runs per line.
	 * N defaults to 1.
	 */
	int levels = (argc >= 2) ? atoi(argv[1]) : 1;
	int tabstop = g->tabstop > 0 ? g->tabstop : 4;
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	if (levels < 1)
		levels = 1;
	if (!new_buf)
		return;

	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		int removed = 0;

		/* remove up to (levels * tabstop) leading spaces or levels tabs */
		while (p <= rbe && *p != '\n' && removed < levels) {
			if (*p == '\t') {
				p++;
				removed++;
			} else if (*p == ' ') {
				int sp = 0;
				while (p <= rbe && *p == ' ' && sp < tabstop) {
					p++;
					sp++;
				}
				removed++;
			} else {
				break;
			}
		}
		while (p <= rbe && *p != '\n')
			*out++ = *p++;
		if (p <= rbe) {
			*out++ = '\n';
			p++;
		}
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- align ------------------------------------------------------------- */

static void
run_align(struct editor *g, int argc, char *argv[],
          int rs, int re)
{
	/*
	 * == :run align CHAR — align each line so CHAR appears in the same column ==
	 *
	 * Pads each line with spaces before the first occurrence of CHAR so all
	 * occurrences land in the same column (the rightmost across all lines).
	 */
	char target;
	int range_len = (int)(re - rs + 1);
	char *new_buf;
	char *p, *out;
	char *rb, *rbe;
	int max_col, new_len;

	if (argc < 2 || argv[1][0] == '\0') {
		status_line(g, "align: usage: align CHAR");
		return;
	}
	target = argv[1][0];

	/* first pass: find the maximum column of target on any line */
	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	max_col = -1;
	p = rb;
	while (p <= rbe) {
		int col = 0;
		int found = 0;

		while (p <= rbe && *p != '\n') {
			if (*p == target && !found) {
				found = 1;
				if (col > max_col)
					max_col = col;
			}
			col++;
			p++;
		}
		if (p <= rbe)
			p++;
	}
	if (max_col < 0) {
		status_line(g, "align: character '%c' not found in range", target);
		free(rb);
		return;
	}

	/* second pass: rebuild lines, padding to max_col before target */
	new_buf = malloc((size_t)range_len + (size_t)max_col * range_len + 1);
	if (!new_buf) {
		free(rb);
		return;
	}

	out = new_buf;
	p = rb;
	while (p <= rbe) {
		char *line = p;
		int len = 0;
		int target_col = -1, col = 0;

		while (p <= rbe && *p != '\n') {
			if (*p == target && target_col < 0)
				target_col = col;
			col++;
			p++;
			len++;
		}
		if (p <= rbe)
			p++;

		if (target_col >= 0) {
			int pad = max_col - target_col;
			memcpy(out, line, (size_t)target_col);
			out += target_col;
			memset(out, ' ', (size_t)pad);
			out += pad;
			memcpy(out, line + target_col, (size_t)(len - target_col));
			out += len - target_col;
		} else {
			memcpy(out, line, (size_t)len);
			out += len;
		}
		*out++ = '\n';
	}
	/* strip trailing newline if range didn't end with one */
	if (out > new_buf && *(out - 1) == '\n' && text_char(g, re) != '\n')
		out--;
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- urlencode / urldecode -------------------------------------------- */

static int
url_safe(unsigned char c)
{
	/*
	 * == True if c is an RFC 3986 unreserved character (no percent-encoding needed) ==
	 */
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '_' ||
	       c == '.' || c == '~';
}

static void
run_urlencode(struct editor *g, int argc, char *argv[],
              int rs, int re)
{
	/*
	 * == :run urlencode — percent-encode the range as a URL component ==
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len * 3 + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;
	static const char hex[] = "0123456789ABCDEF";

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	for (p = rb; p <= rbe; p++) {
		unsigned char c = (unsigned char)*p;
		if (url_safe(c)) {
			*out++ = (char)c;
		} else {
			*out++ = '%';
			*out++ = hex[c >> 4];
			*out++ = hex[c & 0xf];
		}
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

static int
hex_val(unsigned char c)
{
	/*
	 * == Convert a hex digit character to its integer value (0–15), or -1 ==
	 */
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static void
run_urldecode(struct editor *g, int argc, char *argv[],
              int rs, int re)
{
	/*
	 * == :run urldecode — decode percent-encoded sequences in the range ==
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		if (*p == '%' && p + 2 <= rbe) {
			int hi = hex_val((unsigned char)p[1]);
			int lo = hex_val((unsigned char)p[2]);
			if (hi >= 0 && lo >= 0) {
				*out++ = (char)((hi << 4) | lo);
				p += 3;
				continue;
			}
		}
		if (*p == '+')
			*out++ = ' ';
		else
			*out++ = *p;
		p++;
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- base64enc / base64dec -------------------------------------------- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
run_base64enc(struct editor *g, int argc, char *argv[],
              int rs, int re)
{
	/*
	 * == :run base64enc — encode the range as Base64 (with line wrapping) ==
	 */
	int range_len = (int)(re - rs + 1);
	/* base64 output: 4 bytes per 3 input, plus newlines every 76 chars */
	char *new_buf = malloc((size_t)range_len * 2 + 8);
	char *rb;
	unsigned char *p;
	unsigned char *end;
	char *out;
	int new_len, col = 0;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	rb = text_copy(g, rs, range_len);
	p = (unsigned char *)rb;
	end = (unsigned char *)rb + (re - rs) + 1;
	out = new_buf;
	while (p < end) {
		unsigned int b0 = *p++;
		unsigned int b1 = (p < end) ? *p++ : 0;
		unsigned int b2 = (p < end) ? *p++ : 0;
		int take = (int)(p - (unsigned char *)rb) <= range_len ? 3 : (p - 1 < end) ? 2
		                                                                           : 1;
		unsigned int v = (b0 << 16) | (b1 << 8) | b2;

		*out++ = b64_table[(v >> 18) & 0x3f];
		*out++ = b64_table[(v >> 12) & 0x3f];
		*out++ = (take >= 2) ? b64_table[(v >> 6) & 0x3f] : '=';
		*out++ = (take >= 3) ? b64_table[v & 0x3f] : '=';
		col += 4;
		if (col >= 76) {
			*out++ = '\n';
			col = 0;
		}
	}
	if (col > 0)
		*out++ = '\n';
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

static int
b64_val(unsigned char c)
{
	/*
	 * == Map a Base64 digit character to its 6-bit value (0–63), or -1 ==
	 */
	if (c >= 'A' && c <= 'Z')
		return c - 'A';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 26;
	if (c >= '0' && c <= '9')
		return c - '0' + 52;
	if (c == '+')
		return 62;
	if (c == '/')
		return 63;
	return -1;
}

static void
run_base64dec(struct editor *g, int argc, char *argv[],
              int rs, int re)
{
	/*
	 * == :run base64dec — decode Base64 text in the range back to binary ==
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		int v[4], i, n = 0;
		unsigned int bits;

		for (i = 0; i < 4 && p <= rbe;) {
			unsigned char c = (unsigned char)*p++;
			if (c == '\n' || c == '\r' || c == ' ')
				continue;
			v[i++] = (c == '=') ? 0 : b64_val(c);
			n++;
		}
		if (n < 2)
			break;
		bits = ((unsigned)v[0] << 18) | ((unsigned)v[1] << 12) |
		       ((unsigned)v[2] << 6) | (unsigned)v[3];
		*out++ = (char)((bits >> 16) & 0xff);
		if (n >= 3)
			*out++ = (char)((bits >> 8) & 0xff);
		if (n >= 4)
			*out++ = (char)(bits & 0xff);
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- jsonesc / jsonunesc ---------------------------------------------- */

static void
run_jsonesc(struct editor *g, int argc, char *argv[],
            int rs, int re)
{
	/*
	 * == :run jsonesc — escape special characters for JSON string embedding ==
	 *
	 * Replaces each byte in the range with its JSON escape sequence where
	 * required: backslash, double-quote, control characters (\n \r \t \b \f),
	 * and any other byte below 0x20 encoded as \uXXXX.  Printable bytes are
	 * copied as-is.  Allocates up to 6× the range length to accommodate the
	 * worst-case expansion.
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len * 6 + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	for (p = rb; p <= rbe; p++) {
		unsigned char c = (unsigned char)*p;
		switch (c) {
		case '"':
			*out++ = '\\';
			*out++ = '"';
			break;
		case '\\':
			*out++ = '\\';
			*out++ = '\\';
			break;
		case '\n':
			*out++ = '\\';
			*out++ = 'n';
			break;
		case '\r':
			*out++ = '\\';
			*out++ = 'r';
			break;
		case '\t':
			*out++ = '\\';
			*out++ = 't';
			break;
		case '\b':
			*out++ = '\\';
			*out++ = 'b';
			break;
		case '\f':
			*out++ = '\\';
			*out++ = 'f';
			break;
		default:
			if (c < 0x20) {
				out += sprintf(out, "\\u%04x", c);
			} else {
				*out++ = (char)c;
			}
		}
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

static void
run_jsonunesc(struct editor *g, int argc, char *argv[],
              int rs, int re)
{
	/*
	 * == :run jsonunesc — decode JSON escape sequences back to raw bytes ==
	 *
	 * Walks the range and expands each \X escape: \", \\, \/, \n, \r, \t,
	 * \b, \f are converted to their single-byte equivalents.  \uXXXX is
	 * decoded as a Unicode codepoint and re-encoded as UTF-8 (BMP only;
	 * surrogates are not combined).  Unrecognised escapes are passed through
	 * literally.  Output is always shorter than or equal to the input.
	 */
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *rb, *rbe;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	rb = text_copy(g, rs, range_len);
	rbe = rb + (re - rs);
	out = new_buf;
	p = rb;
	while (p <= rbe) {
		if (*p == '\\' && p + 1 <= rbe) {
			p++;
			switch (*p) {
			case '"':
				*out++ = '"';
				p++;
				break;
			case '\\':
				*out++ = '\\';
				p++;
				break;
			case '/':
				*out++ = '/';
				p++;
				break;
			case 'n':
				*out++ = '\n';
				p++;
				break;
			case 'r':
				*out++ = '\r';
				p++;
				break;
			case 't':
				*out++ = '\t';
				p++;
				break;
			case 'b':
				*out++ = '\b';
				p++;
				break;
			case 'f':
				*out++ = '\f';
				p++;
				break;
			case 'u':
				if (p + 4 <= rbe) {
					unsigned int cp = 0;
					int i;
					p++;
					for (i = 0; i < 4; i++) {
						int hv = hex_val((unsigned char)*p++);
						cp = (cp << 4) | (unsigned)(hv >= 0 ? hv : 0);
					}
					/* encode as UTF-8 */
					if (cp < 0x80) {
						*out++ = (char)cp;
					} else if (cp < 0x800) {
						*out++ = (char)(0xc0 | (cp >> 6));
						*out++ = (char)(0x80 | (cp & 0x3f));
					} else {
						*out++ = (char)(0xe0 | (cp >> 12));
						*out++ = (char)(0x80 | ((cp >> 6) & 0x3f));
						*out++ = (char)(0x80 | (cp & 0x3f));
					}
				} else {
					*out++ = '\\';
					*out++ = 'u';
					p++;
				}
				break;
			default:
				*out++ = '\\';
				*out++ = *p++;
			}
		} else {
			*out++ = *p++;
		}
	}
	free(rb);
	new_len = (int)(out - new_buf);
	buffer_replace_range(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- freq -------------------------------------------------------------- */

#define FREQ_MAX_WORDS 256
#define FREQ_WORD_LEN 32

struct word_freq {
	char word[FREQ_WORD_LEN];
	int count;
};

static void
run_freq(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run freq — count word frequencies and display the top 5 ==
	 *
	 * Scans the range for identifier-like tokens (is_ident_start / is_ident),
	 * accumulates counts in a fixed-size table (up to FREQ_MAX_WORDS unique
	 * words, each up to FREQ_WORD_LEN-1 characters), then partial-sorts to
	 * find the five most frequent and displays them on the status bar.
	 * Words longer than FREQ_WORD_LEN are silently skipped.
	 */
	struct word_freq wf[FREQ_MAX_WORDS];
	int n = 0;
	char *rb = text_copy(g, rs, re - rs + 1);
	char *rbe = rb + (re - rs);
	char *p = rb;
	char status[STATUS_BUFFER_LEN];
	int i, j, top, out_len;

	(void)argc;
	(void)argv;
	memset(wf, 0, sizeof(wf));

	while (p <= rbe) {
		if (is_ident_start((unsigned char)*p)) {
			char *start = p;
			int len = 0;

			while (p <= rbe && is_ident((unsigned char)*p)) {
				p++;
				len++;
			}
			if (len >= FREQ_WORD_LEN)
				continue;
			/* find or insert */
			for (i = 0; i < n; i++) {
				if ((int)strlen(wf[i].word) == len &&
				    memcmp(wf[i].word, start, (size_t)len) == 0) {
					wf[i].count++;
					goto next_word;
				}
			}
			if (n < FREQ_MAX_WORDS) {
				memcpy(wf[n].word, start, (size_t)len);
				wf[n].word[len] = '\0';
				wf[n].count = 1;
				n++;
			}
		next_word:;
		} else {
			p++;
		}
	}

	free(rb);

	/* partial bubble sort: extract top 5 */
	top = n < 5 ? n : 5;
	for (i = 0; i < top; i++) {
		for (j = i + 1; j < n; j++) {
			if (wf[j].count > wf[i].count) {
				struct word_freq tmp = wf[i];
				wf[i] = wf[j];
				wf[j] = tmp;
			}
		}
	}

	out_len = snprintf(status, sizeof(status), "freq (%d unique):", n);
	for (i = 0; i < top && out_len < (int)sizeof(status) - 2; i++) {
		out_len += snprintf(status + out_len,
		                    sizeof(status) - (size_t)out_len,
		                    "  %s×%d", wf[i].word, wf[i].count);
	}
	status_line(g, "%s", status);
}

/* ---- col --------------------------------------------------------------- */

static void
run_col(struct editor *g, int argc, char *argv[],
        int rs, int re)
{
	/*
	 * == :run col — report the maximum column width across all lines in range ==
	 *
	 * Counts bytes per line (newline-delimited) and displays the widest line
	 * length on the status bar.  Useful for checking whether text exceeds a
	 * column limit before wrapping or aligning.
	 */
	int max_col = 0;
	char *rb = text_copy(g, rs, re - rs + 1);
	char *rbe = rb + (re - rs);
	char *p = rb;

	(void)argc;
	(void)argv;
	while (p <= rbe) {
		int col = 0;

		while (p <= rbe && *p != '\n') {
			col++;
			p++;
		}
		if (col > max_col)
			max_col = col;
		if (p <= rbe)
			p++;
	}
	free(rb);
	status_line(g, "max column width: %d", max_col);
}

/* ---- hash (FNV-1a) ---------------------------------------------------- */

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

static uint64_t
fnv1a(const char *s, int len)
{
	/*
	 * == FNV-1a 64-bit hash of a byte string ==
	 *
	 * Standard Fowler-Noll-Vo hash: XOR each byte into the accumulator then
	 * multiply by FNV_PRIME.  Used by run_hash for both collision analysis
	 * and identifier replacement.
	 */
	uint64_t h = FNV_OFFSET;
	int i;

	for (i = 0; i < len; i++) {
		h ^= (uint64_t)(unsigned char)s[i];
		h *= FNV_PRIME;
	}
	return h;
}

static void
run_hash(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run hash — FNV-1a hash analysis and replacement for the range ==
	 *
	 * Three sub-commands selected by argv[1]:
	 *   (default)      — hash the entire range, display as decimal + hex
	 *   mod N          — hash each identifier and report bucket
	 *                    collisions when spread across N slots (1..65536)
	 *   replace [mod N] — replace each identifier with its raw hash value
	 *                     (or hash mod N); changes are chained into one undo
	 */
	/* hash mod N — per-identifier: report bucket distribution/collisions */
	if (argc >= 3 && strcmp(argv[1], "mod") == 0) {
		uint64_t N = (uint64_t)atoi(argv[2]);
		/* bucket array: 1 bit per slot up to 65536 */
		int max_slots = 65536;
		char *seen;
		int collisions = 0, total = 0;
		char *rb, *rbe;
		char *p;
		char status[STATUS_BUFFER_LEN];

		if (N < 1 || N > (uint64_t)max_slots) {
			status_line(g, "hash mod: N must be 1..%d", max_slots);
			return;
		}
		seen = calloc((size_t)N, 1);
		if (!seen)
			return;

		rb = text_copy(g, rs, re - rs + 1);
		rbe = rb + (re - rs);
		p = rb;
		while (p <= rbe) {
			if (is_ident_start((unsigned char)*p)) {
				char *start = p;
				int len = 0;

				while (p <= rbe && is_ident((unsigned char)*p)) {
					p++;
					len++;
				}
				uint64_t bucket = fnv1a(start, len) % N;
				total++;
				if (seen[bucket])
					collisions++;
				else
					seen[bucket] = 1;
			} else {
				p++;
			}
		}
		free(seen);
		free(rb);
		snprintf(status, sizeof(status),
		         "hash mod %llu: %d identifiers, %d collision%s",
		         (unsigned long long)N, total,
		         collisions, collisions == 1 ? "" : "s");
		status_line(g, "%s", status);

		/* hash replace [mod N] — replace each identifier with its hash value */
	} else if (argc >= 2 && strcmp(argv[1], "replace") == 0) {
		uint64_t N = 0; /* 0 = no modulo, use raw hash */
		int p = rs;
		int re_cur = re;
		int undo = 0;

		if (argc >= 4 && strcmp(argv[2], "mod") == 0)
			N = (uint64_t)atoi(argv[3]);

		while (p <= re_cur) {
			if (is_ident_start((unsigned char)text_char(g, p))) {
				int id = p;
				int id_len = 0;

				while (p <= re_cur &&
				       is_ident((unsigned char)text_char(g, p))) {
					p++;
					id_len++;
				}
				char *id_s = text_copy(g, id, id_len);
				uint64_t h = fnv1a(id_s, id_len);
				free(id_s);
				if (N > 0)
					h %= N;

				char num[24];
				int num_len = snprintf(num, sizeof(num),
				                       "%llu",
				                       (unsigned long long)h);
				int bias;

				text_hole_delete(g, id, id + id_len - 1,
				                 undo++ ? ALLOW_UNDO_CHAIN
				                        : ALLOW_UNDO);
				p -= id_len;
				re_cur -= id_len;
				bias = string_insert(g, id, num,
				                     undo++ ? ALLOW_UNDO_CHAIN
				                            : ALLOW_UNDO);
				p += bias + num_len;
				re_cur += bias + num_len;
			} else {
				p++;
			}
		}

		/* hash — FNV-1a of entire range, displayed on status bar */
	} else {
		int len = (int)(re - rs + 1);
		char *rb = text_copy(g, rs, len);
		uint64_t h = fnv1a(rb, len);
		free(rb);
		status_line(g, "hash: %llu  (0x%016llx)  len=%d",
		            (unsigned long long)h, (unsigned long long)h, len);
	}
}

/* ---- convert: camelCase <-> snake_case --------------------------------- */

static int
c2s(const char *src, int len, char *dst)
{
	/*
	 * == Convert a camelCase identifier to snake_case ==
	 *
	 * Inserts an underscore before each uppercase letter that follows a
	 * lowercase letter, or before an uppercase letter that precedes a
	 * lowercase one (handles sequences like "XMLParser" → "xml_parser").
	 * All letters are lowercased in the output.  Returns the output length.
	 */
	int i, n = 0;

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (isupper(c) && i > 0) {
			unsigned char prev = (unsigned char)src[i - 1];
			unsigned char next = (i + 1 < len) ? (unsigned char)src[i + 1] : 0;
			if (islower(prev) || (isupper(prev) && islower(next)))
				dst[n++] = '_';
		}
		dst[n++] = (char)tolower(c);
	}
	return n;
}

static int
s2c(const char *src, int len, char *dst)
{
	/*
	 * == Convert a snake_case identifier to camelCase ==
	 *
	 * Removes each underscore and capitalises the following letter.
	 * Leading underscores are preserved as-is.  Returns the output length.
	 */
	int i, n = 0, cap_next = 0, leading = 1;

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c == '_' && leading) {
			dst[n++] = '_';
		} else if (c == '_') {
			cap_next = 1;
		} else {
			leading = 0;
			dst[n++] = (char)(cap_next ? toupper(c) : c);
			cap_next = 0;
		}
	}
	return n;
}

static int
s2uc(const char *src, int len, char *dst)
{
	/*
	 * == Convert a snake_case identifier to UpperCamelCase (PascalCase) ==
	 *
	 * Like s2c but the very first letter is also capitalised.  Leading
	 * underscores are preserved.  Returns the output length.
	 */
	int i, n = 0, cap_next = 1, leading = 1;

	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)src[i];
		if (c == '_' && leading) {
			dst[n++] = '_';
		} else if (c == '_') {
			cap_next = 1;
		} else {
			leading = 0;
			dst[n++] = (char)(cap_next ? toupper(c) : c);
			cap_next = 0;
		}
	}
	return n;
}

static void
run_convert(struct editor *g, int argc, char *argv[],
            int rs, int re)
{
	/*
	 * == :run convert — rename identifiers across a range ==
	 *
	 * Applies one of three transforms (selected by argv[1]) to every
	 * identifier-like token in the range:
	 *   c2s  — camelCase → snake_case
	 *   s2c  — snake_case → camelCase
	 *   s2uc — snake_case → UpperCamelCase
	 * Tokens that don't change are left untouched.  All substitutions are
	 * chained into a single undo entry.  Reports the count on the status bar.
	 */
	int (*transform)(const char *, int, char *);
	int p = rs;
	int re_cur = re;
	int subs = 0, undo = 0;

	if (argc < 2) {
		status_line(g, "convert: expected c2s, s2c, or s2uc");
		return;
	}
	if (strcmp(argv[1], "c2s") == 0)
		transform = c2s;
	else if (strcmp(argv[1], "s2c") == 0)
		transform = s2c;
	else if (strcmp(argv[1], "s2uc") == 0)
		transform = s2uc;
	else {
		status_line(g, "convert: unknown mode '%s' (use c2s, s2c, s2uc)",
		            argv[1]);
		return;
	}

	while (p <= re_cur) {
		if (is_ident_start((unsigned char)text_char(g, p))) {
			int id = p;
			int id_len = 0;

			while (p <= re_cur && is_ident((unsigned char)text_char(g, p))) {
				p++;
				id_len++;
			}
			char *dst = malloc((size_t)(id_len * 2 + 1));
			if (!dst)
				continue;

			char *id_s = text_copy(g, id, id_len);
			int new_len = transform(id_s, id_len, dst);
			dst[new_len] = '\0';

			if (new_len != id_len ||
			    memcmp(id_s, dst, (size_t)id_len) != 0) {
				int bias;

				text_hole_delete(g, id, id + id_len - 1,
				                 undo++ ? ALLOW_UNDO_CHAIN
				                        : ALLOW_UNDO);
				p -= id_len;
				re_cur -= id_len;
				bias = string_insert(g, id, dst,
				                     undo++ ? ALLOW_UNDO_CHAIN
				                            : ALLOW_UNDO);
				p += bias + new_len;
				re_cur += bias + new_len;
				subs++;
			}
			free(id_s);
			free(dst);
		} else {
			p++;
		}
	}

	if (subs)
		status_line(g, "convert: %d identifier%s changed",
		            subs, subs == 1 ? "" : "s");
	else
		status_line(g, "convert: nothing to change");
}

/* ---- echo -------------------------------------------------------------- */

static void
run_echo(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run echo — display arguments on the status bar ==
	 *
	 * Joins argv[1..argc-1] with single spaces and writes the result to
	 * the status line.  Does not modify the buffer; rs/re are ignored.
	 * Useful for debugging :run pipelines or displaying computed values.
	 */
	char buf[STATUS_BUFFER_LEN];
	int n = 0, i;

	(void)rs;
	(void)re;
	for (i = 1; i < argc; i++) {
		int len = (int)strlen(argv[i]);
		if (i > 1 && n < (int)sizeof(buf) - 1)
			buf[n++] = ' ';
		if (n + len >= (int)sizeof(buf))
			len = (int)sizeof(buf) - 1 - n;
		if (len > 0) {
			memcpy(buf + n, argv[i], (size_t)len);
			n += len;
		}
	}
	buf[n] = '\0';
	status_line(g, "%s", buf);
}

/* ---- wc ---------------------------------------------------------------- */

static void
run_wc(struct editor *g, int argc, char *argv[],
       int rs, int re)
{
	/*
	 * == :run wc — count lines, words, and bytes in the range ==
	 *
	 * Scans the range byte-by-byte, counting newlines (lines), whitespace-
	 * delimited runs (words), and total bytes.  Displays "N lines  N words
	 * N bytes" on the status bar.  Does not modify the buffer.
	 */
	int lines = 0, words = 0, bytes = 0, in_word = 0;
	char *rb = text_copy(g, rs, re - rs + 1);
	char *rbe = rb + (re - rs);
	char *p;

	(void)argc;
	(void)argv;
	for (p = rb; p <= rbe; p++) {
		unsigned char c = (unsigned char)*p;
		bytes++;
		if (c == '\n') {
			lines++;
			in_word = 0;
		} else if (c == ' ' || c == '\t') {
			in_word = 0;
		} else if (!in_word) {
			in_word = 1;
			words++;
		}
	}
	free(rb);
	status_line(g, "%d lines  %d words  %d bytes", lines, words, bytes);
}

/* ---- highlight --------------------------------------------------------- */

static void
run_highlight(struct editor *g, int argc, char *argv[],
              int rs, int re)
{
	/*
	 * == :run highlight — set or clear the persistent highlight pattern ==
	 *
	 * If argv[1] is non-empty, copies it into g->highlight_pattern (freeing
	 * any previous value) so that the screen renderer can mark matching text.
	 * Passing no argument or an empty string clears the highlight.
	 * Does not modify the buffer; rs/re are ignored.
	 */
	(void)rs;
	(void)re;
	free(g->highlight_pattern);
	g->highlight_pattern = NULL;
	if (argc >= 2 && argv[1][0] != '\0')
		g->highlight_pattern = xstrdup(argv[1]);
}

static void
run_color_escape(struct editor *g, int argc, char *argv[],
                 int rs, int re)
{
	/*
	 * == :run color-escape [on|off] — interpret SGR escapes as colour ==
	 *
	 * A document produced by a colourising tool carries SGR escape
	 * sequences, which vic normally displays as the literal text
	 * "^[[31m".  With this mode on such a sequence occupies no display
	 * column and is passed through to the terminal instead, so the text
	 * after it appears in the colour the document asks for.
	 *
	 * The buffer is not modified, so the mode is reversible.  With no
	 * argument it toggles; "on" and "off" set it explicitly.  The syntax
	 * colorizer is suppressed while the mode is on, because the two would
	 * emit competing SGR sequences.  rs/re are ignored.
	 *
	 * Clearing the shadow screen and dropping refresh_last_screenbegin
	 * forces the next refresh to redraw every row, since neither the text
	 * nor the cursor position changed.
	 */
	(void)rs;
	(void)re;

	if (argc >= 2) {
		if (strcmp(argv[1], "on") == 0) {
			g->color_escape = 1;
		} else if (strcmp(argv[1], "off") == 0) {
			g->color_escape = 0;
		} else {
			status_line(g, "color-escape: expected 'on' or 'off'");
			return;
		}
	} else {
		g->color_escape = !g->color_escape;
	}

	g->esc_sgr[0] = '\0';
	screen_erase(g);
	g->refresh_last_screenbegin = -1;

	if (g->color_escape && g->pager_mode)
		status_line(g, "color-escape on (-p already stripped the escapes)");
	else
		status_line(g, "color-escape %s", g->color_escape ? "on" : "off");
}

/* ---- pipe -------------------------------------------------------------- */

static int
pipe_needs_quotes(const char *s)
{
	/*
	 * == True if s must be quoted again before /bin/sh re-reads it ==
	 *
	 * run_tokenize() in ex.c splits unquoted words on whitespace and
	 * strips the quotes off the ones that were quoted, so a token that
	 * still holds a space or a tab can only have arrived quoted.  Quoting
	 * exactly those tokens again keeps both uses working: in
	 * ":run pipe jq '.a | .b'" the '|' stays inside one argument, while in
	 * ":run pipe grep foo | sort" the bare '|' stays the shell pipeline
	 * the user wrote.  An empty token is quoted too, since it would
	 * otherwise vanish.
	 *
	 * The case this misses is a quoted token with no whitespace in it,
	 * such as the '*' in ":run pipe grep '*'": it reaches the shell bare
	 * and the shell expands it.
	 */
	if (*s == '\0')
		return 1;
	for (; *s; s++) {
		if (*s == ' ' || *s == '\t')
			return 1;
	}
	return 0;
}

static char *
pipe_build_cmd(int argc, char *argv[])
{
	/*
	 * == Rebuild one shell command line out of argv[1..argc-1] ==
	 *
	 * Returns a malloc'd string the caller frees, or NULL when the
	 * allocation fails.  A single quote inside a quoted token is written
	 * as the four bytes '\'' — close, escape, reopen — which is what the
	 * size estimate of four bytes per input byte allows for.
	 */
	size_t need = 1;
	char *cmd;
	char *out;
	const char *s;
	int i;

	for (i = 1; i < argc; i++)
		need += 4 * strlen(argv[i]) + 3;
	cmd = malloc(need);
	if (cmd == NULL)
		return NULL;

	out = cmd;
	for (i = 1; i < argc; i++) {
		if (i > 1)
			*out++ = ' ';
		if (!pipe_needs_quotes(argv[i])) {
			for (s = argv[i]; *s; s++)
				*out++ = *s;
			continue;
		}
		*out++ = '\'';
		for (s = argv[i]; *s; s++) {
			if (*s == '\'') {
				memcpy(out, "'\\''", 4);
				out += 4;
			} else
				*out++ = *s;
		}
		*out++ = '\'';
	}
	*out = '\0';
	return cmd;
}

static void
run_pipe(struct editor *g, int argc, char *argv[],
         int rs, int re)
{
	/*
	 * == :run pipe <shell command> — filter the range through a command ==
	 *
	 * ":run pipe jq" sends the whole buffer to jq and replaces it with
	 * what jq prints; ":'<,'>run pipe jq" does the same for the selected
	 * lines.  The work is colon_do_filter() in ex.c, the same code behind
	 * ":{range}!cmd": the range becomes the command's stdin and, on exit
	 * status 0, is replaced by the command's stdout.  A non-zero exit
	 * leaves the text untouched and puts the command's stderr on the
	 * status line, so a parse error from jq costs nothing.
	 *
	 * The range is whole lines, as it is for ':!': an address resolves to
	 * line boundaries before run_dispatch sees it, and no address at all
	 * means the whole buffer.
	 */
	char *cmd;

	if (argc < 2) {
		status_line(g, "pipe: no command given");
		return;
	}
	if (re < rs) {
		status_line(g, "pipe: empty range");
		return;
	}
	cmd = pipe_build_cmd(argc, argv);
	if (cmd == NULL)
		return;
	colon_do_filter(g, rs, re, cmd);
	free(cmd);
}

/* ---- dispatch table ---------------------------------------------------- */

static const struct run_entry run_table[] = {
    {"echo", run_echo},
    {"wc", run_wc},
    {"convert", run_convert},
    {"upper", run_upper},
    {"lower", run_lower},
    {"trim", run_trim},
    {"trimsel", run_trimsel},
    {"uniq", run_uniq},
    {"sort", run_sort},
    {"wrap", run_wrap},
    {"number", run_number},
    {"deindent", run_deindent},
    {"align", run_align},
    {"urlencode", run_urlencode},
    {"urldecode", run_urldecode},
    {"base64enc", run_base64enc},
    {"base64dec", run_base64dec},
    {"jsonesc", run_jsonesc},
    {"jsonunesc", run_jsonunesc},
    {"freq", run_freq},
    {"col", run_col},
    {"hash", run_hash},
    {"pipe", run_pipe},
    {"highlight", run_highlight},
    {"color-escape", run_color_escape},
    {NULL, NULL},
};

/* ---- public API -------------------------------------------------------- */

void
run_dispatch(struct editor *g, int argc, char *argv[],
             int rs, int re)
{
	/*
	 * == Public entry point: dispatch a :run sub-command by name ==
	 *
	 * Searches run_table[] for an entry whose name matches argv[0] and calls
	 * its handler.  Reports an error on the status bar if the name is not
	 * found.  All built-in :run commands are registered in run_table[].
	 */
	int i;

	if (argc < 1)
		return;
	for (i = 0; run_table[i].name != NULL; i++) {
		if (strcmp(run_table[i].name, argv[0]) == 0) {
			run_table[i].fn(g, argc, argv, rs, re);
			return;
		}
	}
	status_line(g, "run: unknown command '%s'", argv[0]);
}
