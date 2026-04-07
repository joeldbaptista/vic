/*
 * run.c - `:run` command dispatch table and built-in commands.
 *
 * run_dispatch() tokenises the argument string and calls the matching
 * run_fn from run_table[].  Add new commands by appending entries there.
 *
 * Built-in commands
 * -----------------
 *   echo [args…]             — print args to the status bar
 *   wc                       — count lines, words, bytes in range
 *   convert c2s|s2c|s2uc     — identifier case conversion
 *   upper / lower            — uppercase / lowercase all text in range
 *   trim                     — strip trailing whitespace from each line
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
 */
#include "run.h"
#include "buffer.h"
#include "status.h"
#include "undo.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- shared helpers ---------------------------------------------------- */

static int
is_ident_start(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident(unsigned char c)
{
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

/*
 * Replace [rs, re] (inclusive) with new_buf[0..new_len-1].
 * Assumes new_buf contains no NUL bytes (safe for text content).
 */
static void
raw_replace(struct editor *g, char *rs, char *re,
            const char *new_buf, int new_len)
{
	char *tmp;

	if (rs <= re)
		text_hole_delete(g, rs, re, ALLOW_UNDO);
	if (new_len <= 0)
		return;
	tmp = malloc((size_t)new_len + 1);
	if (!tmp)
		return;
	memcpy(tmp, new_buf, (size_t)new_len);
	tmp[new_len] = '\0';
	string_insert(g, rs, tmp, ALLOW_UNDO_CHAIN);
	free(tmp);
}

/* ---- upper / lower ---------------------------------------------------- */

static void
run_upper(struct editor *g, int argc, char *argv[],
          char *rs, char *re)
{
	int len = (int)(re - rs + 1);
	char *p;

	(void)argc;
	(void)argv;
	undo_push(g, rs, (unsigned)len, UNDO_SWAP);
	for (p = rs; p <= re; p++)
		*p = (char)toupper((unsigned char)*p);
}

static void
run_lower(struct editor *g, int argc, char *argv[],
          char *rs, char *re)
{
	int len = (int)(re - rs + 1);
	char *p;

	(void)argc;
	(void)argv;
	undo_push(g, rs, (unsigned)len, UNDO_SWAP);
	for (p = rs; p <= re; p++)
		*p = (char)tolower((unsigned char)*p);
}

/* ---- trim -------------------------------------------------------------- */

static void
run_trim(struct editor *g, int argc, char *argv[],
         char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;

	out = new_buf;
	p = rs;
	while (p <= re) {
		char *line_start = out;
		char *last_non_ws = out - 1;

		while (p <= re && *p != '\n') {
			*out = *p++;
			if (*out != ' ' && *out != '\t')
				last_non_ws = out;
			out++;
		}
		/* truncate trailing whitespace */
		out = last_non_ws + 1;
		if (p <= re) {
			*out++ = '\n';
			p++;
		}
		(void)line_start;
	}
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- uniq -------------------------------------------------------------- */

static void
run_uniq(struct editor *g, int argc, char *argv[],
         char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	char *prev_line = NULL;
	int prev_len = 0;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;

	out = new_buf;
	p = rs;
	while (p <= re) {
		char *line = p;
		int len = 0;

		while (p <= re && *p != '\n') {
			p++;
			len++;
		}
		if (prev_line == NULL || len != prev_len ||
		    memcmp(line, prev_line, (size_t)len) != 0) {
			memcpy(out, line, (size_t)len);
			out += len;
			if (p <= re)
				*out++ = '\n';
			prev_line = line;
			prev_len = len;
		}
		if (p <= re)
			p++;
	}
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
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
	const struct line_ref *la = a, *lb = b;
	int min_len = la->len < lb->len ? la->len : lb->len;
	int cmp = memcmp(la->start, lb->start, (size_t)min_len);
	return cmp != 0 ? cmp : la->len - lb->len;
}

static int
line_cmp_desc(const void *a, const void *b)
{
	return line_cmp_asc(b, a);
}

static void
run_sort(struct editor *g, int argc, char *argv[],
         char *rs, char *re)
{
	int reverse = (argc >= 2 && strcmp(argv[1], "-r") == 0);
	int range_len = (int)(re - rs + 1);
	int max_lines = range_len / 2 + 2;
	struct line_ref *lines = malloc((size_t)max_lines * sizeof(*lines));
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	int n = 0, i, new_len;

	if (!lines || !new_buf) {
		free(lines);
		free(new_buf);
		return;
	}

	p = rs;
	while (p <= re && n < max_lines) {
		lines[n].start = p;
		lines[n].len = 0;
		while (p <= re && *p != '\n') {
			p++;
			lines[n].len++;
		}
		n++;
		if (p <= re)
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
	if (re >= rs && *re == '\n')
		*out++ = '\n';

	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(lines);
	free(new_buf);
}

/* ---- wrap -------------------------------------------------------------- */

static void
run_wrap(struct editor *g, int argc, char *argv[],
         char *rs, char *re)
{
	int col;
	int range_len = (int)(re - rs + 1);
	char *new_buf;
	char *p, *out;
	int new_len;

	if (argc < 2 || (col = atoi(argv[1])) < 4) {
		status_line(g, "wrap: usage: wrap N  (N >= 4)");
		return;
	}

	/* worst case: every char becomes a line of 1 + newline */
	new_buf = malloc((size_t)range_len * 2 + 1);
	if (!new_buf)
		return;

	out = new_buf;
	p = rs;
	while (p <= re) {
		char *line = p;
		int len = 0;

		while (p <= re && *p != '\n') {
			p++;
			len++;
		}
		if (p <= re)
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
	new_len = (int)(out - new_buf);
	/* strip extra trailing newline if original didn't end with one */
	if (new_len > 0 && new_buf[new_len - 1] == '\n' &&
	    (re < rs || *re != '\n'))
		new_len--;
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- number ------------------------------------------------------------ */

static void
run_number(struct editor *g, int argc, char *argv[],
           char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	/* max prefix "99999. " = 7 chars per line */
	char *new_buf = malloc((size_t)range_len + range_len / 2 + 16);
	char *p, *out;
	int lineno = 1, new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;

	out = new_buf;
	p = rs;
	while (p <= re) {
		char prefix[16];
		int plen = snprintf(prefix, sizeof(prefix), "%d. ", lineno++);
		memcpy(out, prefix, (size_t)plen);
		out += plen;
		while (p <= re && *p != '\n')
			*out++ = *p++;
		if (p <= re) {
			*out++ = '\n';
			p++;
		}
	}
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- deindent ---------------------------------------------------------- */

static void
run_deindent(struct editor *g, int argc, char *argv[],
             char *rs, char *re)
{
	int levels = (argc >= 2) ? atoi(argv[1]) : 1;
	int tabstop = g->tabstop > 0 ? g->tabstop : 4;
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	int new_len;

	if (levels < 1)
		levels = 1;
	if (!new_buf)
		return;

	out = new_buf;
	p = rs;
	while (p <= re) {
		int removed = 0;

		/* remove up to (levels * tabstop) leading spaces or levels tabs */
		while (p <= re && *p != '\n' && removed < levels) {
			if (*p == '\t') {
				p++;
				removed++;
			} else if (*p == ' ') {
				int sp = 0;
				while (p <= re && *p == ' ' && sp < tabstop) {
					p++;
					sp++;
				}
				removed++;
			} else {
				break;
			}
		}
		while (p <= re && *p != '\n')
			*out++ = *p++;
		if (p <= re) {
			*out++ = '\n';
			p++;
		}
	}
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- align ------------------------------------------------------------- */

static void
run_align(struct editor *g, int argc, char *argv[],
          char *rs, char *re)
{
	char target;
	int range_len = (int)(re - rs + 1);
	char *new_buf;
	char *p, *out;
	int max_col, new_len;

	if (argc < 2 || argv[1][0] == '\0') {
		status_line(g, "align: usage: align CHAR");
		return;
	}
	target = argv[1][0];

	/* first pass: find the maximum column of target on any line */
	max_col = -1;
	p = rs;
	while (p <= re) {
		int col = 0;
		int found = 0;

		while (p <= re && *p != '\n') {
			if (*p == target && !found) {
				found = 1;
				if (col > max_col)
					max_col = col;
			}
			col++;
			p++;
		}
		if (p <= re)
			p++;
	}
	if (max_col < 0) {
		status_line(g, "align: character '%c' not found in range", target);
		return;
	}

	/* second pass: rebuild lines, padding to max_col before target */
	new_buf = malloc((size_t)range_len + (size_t)max_col * range_len + 1);
	if (!new_buf)
		return;

	out = new_buf;
	p = rs;
	while (p <= re) {
		char *line = p;
		int len = 0;
		int target_col = -1, col = 0;

		while (p <= re && *p != '\n') {
			if (*p == target && target_col < 0)
				target_col = col;
			col++;
			p++;
			len++;
		}
		if (p <= re)
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
	if (out > new_buf && *(out - 1) == '\n' && *re != '\n')
		out--;
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- urlencode / urldecode -------------------------------------------- */

static int
url_safe(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '-' || c == '_' ||
	       c == '.' || c == '~';
}

static void
run_urlencode(struct editor *g, int argc, char *argv[],
              char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len * 3 + 1);
	char *p, *out;
	int new_len;
	static const char hex[] = "0123456789ABCDEF";

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	out = new_buf;
	for (p = rs; p <= re; p++) {
		unsigned char c = (unsigned char)*p;
		if (url_safe(c)) {
			*out++ = (char)c;
		} else {
			*out++ = '%';
			*out++ = hex[c >> 4];
			*out++ = hex[c & 0xf];
		}
	}
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

static int
hex_val(unsigned char c)
{
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
              char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	out = new_buf;
	p = rs;
	while (p <= re) {
		if (*p == '%' && p + 2 <= re) {
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
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- base64enc / base64dec -------------------------------------------- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
run_base64enc(struct editor *g, int argc, char *argv[],
              char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	/* base64 output: 4 bytes per 3 input, plus newlines every 76 chars */
	char *new_buf = malloc((size_t)range_len * 2 + 8);
	unsigned char *p = (unsigned char *)rs;
	unsigned char *end = (unsigned char *)re + 1;
	char *out;
	int new_len, col = 0;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	out = new_buf;
	while (p < end) {
		unsigned int b0 = *p++;
		unsigned int b1 = (p < end) ? *p++ : 0;
		unsigned int b2 = (p < end) ? *p++ : 0;
		int take = (int)(p - (unsigned char *)rs) <= range_len ? 3 : (p - 1 < end) ? 2
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
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

static int
b64_val(unsigned char c)
{
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
              char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	out = new_buf;
	p = rs;
	while (p <= re) {
		int v[4], i, n = 0;
		unsigned int bits;

		for (i = 0; i < 4 && p <= re;) {
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
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

/* ---- jsonesc / jsonunesc ---------------------------------------------- */

static void
run_jsonesc(struct editor *g, int argc, char *argv[],
            char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len * 6 + 1);
	char *p, *out;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	out = new_buf;
	for (p = rs; p <= re; p++) {
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
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
	free(new_buf);
}

static void
run_jsonunesc(struct editor *g, int argc, char *argv[],
              char *rs, char *re)
{
	int range_len = (int)(re - rs + 1);
	char *new_buf = malloc((size_t)range_len + 1);
	char *p, *out;
	int new_len;

	(void)argc;
	(void)argv;
	if (!new_buf)
		return;
	out = new_buf;
	p = rs;
	while (p <= re) {
		if (*p == '\\' && p + 1 <= re) {
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
				if (p + 4 <= re) {
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
	new_len = (int)(out - new_buf);
	raw_replace(g, rs, re, new_buf, new_len);
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
         char *rs, char *re)
{
	struct word_freq wf[FREQ_MAX_WORDS];
	int n = 0;
	char *p = rs;
	char status[STATUS_BUFFER_LEN];
	int i, j, top, out_len;

	(void)argc;
	(void)argv;
	memset(wf, 0, sizeof(wf));

	while (p <= re) {
		if (is_ident_start((unsigned char)*p)) {
			char *start = p;
			int len = 0;

			while (p <= re && is_ident((unsigned char)*p)) {
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
        char *rs, char *re)
{
	int max_col = 0;
	char *p = rs;

	(void)argc;
	(void)argv;
	while (p <= re) {
		int col = 0;

		while (p <= re && *p != '\n') {
			col++;
			p++;
		}
		if (col > max_col)
			max_col = col;
		if (p <= re)
			p++;
	}
	status_line(g, "max column width: %d", max_col);
}

/* ---- hash (FNV-1a) ---------------------------------------------------- */

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

static uint64_t
fnv1a(const char *s, int len)
{
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
         char *rs, char *re)
{
	/* hash mod N — per-identifier: report bucket distribution/collisions */
	if (argc >= 3 && strcmp(argv[1], "mod") == 0) {
		uint64_t N = (uint64_t)atoi(argv[2]);
		/* bucket array: 1 bit per slot up to 65536 */
		int max_slots = 65536;
		char *seen;
		int collisions = 0, total = 0;
		char *p = rs;
		char status[STATUS_BUFFER_LEN];

		if (N < 1 || N > (uint64_t)max_slots) {
			status_line(g, "hash mod: N must be 1..%d", max_slots);
			return;
		}
		seen = calloc((size_t)N, 1);
		if (!seen)
			return;

		while (p <= re) {
			if (is_ident_start((unsigned char)*p)) {
				char *start = p;
				int len = 0;

				while (p <= re && is_ident((unsigned char)*p)) {
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
		snprintf(status, sizeof(status),
		         "hash mod %llu: %d identifiers, %d collision%s",
		         (unsigned long long)N, total,
		         collisions, collisions == 1 ? "" : "s");
		status_line(g, "%s", status);

		/* hash replace [mod N] — replace each identifier with its hash value */
	} else if (argc >= 2 && strcmp(argv[1], "replace") == 0) {
		uint64_t N = 0; /* 0 = no modulo, use raw hash */
		char *p = rs;
		char *re_cur = re;
		int undo = 0;

		if (argc >= 4 && strcmp(argv[2], "mod") == 0)
			N = (uint64_t)atoi(argv[3]);

		while (p <= re_cur) {
			if (is_ident_start((unsigned char)*p)) {
				char *id = p;
				int id_len = 0;

				while (p <= re_cur &&
				       is_ident((unsigned char)*p)) {
					p++;
					id_len++;
				}
				uint64_t h = fnv1a(id, id_len);
				if (N > 0)
					h %= N;

				char num[24];
				int num_len = snprintf(num, sizeof(num),
				                       "%llu",
				                       (unsigned long long)h);
				uintptr_t bias;

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
		uint64_t h = fnv1a(rs, len);
		status_line(g, "hash: %llu  (0x%016llx)  len=%d",
		            (unsigned long long)h, (unsigned long long)h, len);
	}
}

/* ---- convert: camelCase <-> snake_case --------------------------------- */

static int
c2s(const char *src, int len, char *dst)
{
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
            char *rs, char *re)
{
	int (*transform)(const char *, int, char *);
	char *p = rs;
	char *re_cur = re;
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
		if (is_ident_start((unsigned char)*p)) {
			char *id = p;
			int id_len = 0;

			while (p <= re_cur && is_ident((unsigned char)*p)) {
				p++;
				id_len++;
			}
			char *dst = malloc((size_t)(id_len * 2 + 1));
			if (!dst)
				continue;

			int new_len = transform(id, id_len, dst);
			dst[new_len] = '\0';

			if (new_len != id_len ||
			    memcmp(id, dst, (size_t)id_len) != 0) {
				uintptr_t bias;

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
         char *rs, char *re)
{
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
       char *rs, char *re)
{
	int lines = 0, words = 0, bytes = 0, in_word = 0;
	char *p;

	(void)argc;
	(void)argv;
	for (p = rs; p <= re; p++) {
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
	status_line(g, "%d lines  %d words  %d bytes", lines, words, bytes);
}

/* ---- highlight --------------------------------------------------------- */

static void
run_highlight(struct editor *g, int argc, char *argv[],
              char *rs, char *re)
{
	(void)rs;
	(void)re;
	free(g->highlight_pattern);
	g->highlight_pattern = NULL;
	if (argc >= 2 && argv[1][0] != '\0')
		g->highlight_pattern = xstrdup(argv[1]);
}

/* ---- dispatch table ---------------------------------------------------- */

static const struct run_entry run_table[] = {
    {"echo", run_echo},
    {"wc", run_wc},
    {"convert", run_convert},
    {"upper", run_upper},
    {"lower", run_lower},
    {"trim", run_trim},
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
    {"highlight", run_highlight},
    {NULL, NULL},
};

/* ---- public API -------------------------------------------------------- */

void
run_dispatch(struct editor *g, int argc, char *argv[],
             char *rs, char *re)
{
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
