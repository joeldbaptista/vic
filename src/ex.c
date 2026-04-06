/*
 * ex.c - colon-command parser and executor.
 *
 * colon() parses a ':'-prefixed command line, resolves one or two
 * ex addresses (line numbers, ., $, /, ?, marks, offsets), then
 * dispatches to one of the colon_do_* handler functions below.
 *
 * Address parsing: get_one_address / get_address.
 * Substitution:    colon_do_substitute / regex_search.
 * Global:          global() — :g and :v share one implementation.
 * Option mutation: excore.c (setops, expand_args, …).
 */
#include "ex.h"

#include "buffer.h"
#include "excore.h"
#include "line.h"
#include "motion.h"
#include "operator.h"
#include "run.h"
#include "screen.h"
#include "search.h"
#include "session.h"
#include "status.h"
#include "term.h"
#include "undo.h"

#include <regex.h>
#include <sys/wait.h>

#define Isprint(c) ((unsigned char)(c) >= ' ' && (unsigned char)(c) < ASCII_DEL)
#define ESC "\033"
#define ESC_BOLD_TEXT ESC "[7m"
#define ESC_NORM_TEXT ESC "[m"

enum {
	FORWARD = 1,
	BACK = -1,
	FULL = 1,
	YANKDEL = 1,
};

static char *
get_one_address(struct editor *g, char *p, int *result,
                int *valid)
{
	int num;
	int sign;
	int addr;
	int got_addr;
	char *q;
	char c;
	int dir;

	got_addr = FALSE;
	addr = count_lines(g, g->text, g->dot);
	sign = 0;
	for (;;) {
		if (isblank((unsigned char)*p)) {
			if (got_addr) {
				addr += sign;
				sign = 0;
			}
			p++;
		} else if (!got_addr && *p == '.') {
			p++;
			got_addr = TRUE;
		} else if (!got_addr && *p == '$') {
			p++;
			addr = count_lines(g, g->text, g->end - 1);
			got_addr = TRUE;
		} else if (!got_addr && *p == '\'') {
			p++;
			q = NULL;
			if (*p == '<') {
				p++;
				q = g->mark[MARK_LT];
			} else if (*p == '>') {
				p++;
				q = g->mark[MARK_GT];
			} else {
				c = (char)tolower((unsigned char)*p);
				p++;
				if (c >= 'a' && c <= 'z') {
					c = (char)(c - 'a');
					q = g->mark[(unsigned char)c];
				}
			}
			if (q == NULL) {
				status_line_bold(g, "Mark not set");
				return NULL;
			}
			addr = count_lines(g, g->text, q);
			got_addr = TRUE;
		} else if (!got_addr && (*p == '/' || *p == '?')) {
			c = *p;
			q = strchrnul(p + 1, c);
			if (p + 1 != q) {
				free(g->last_search_pattern);
				g->last_search_pattern = xstrndup(p, (size_t)(q - p));
			}
			p = q;
			if (*p == c)
				p++;
			if (c == '/') {
				q = next_line(g, g->dot);
				dir = (FORWARD << 1) | FULL;
			} else {
				q = begin_line(g, g->dot);
				dir = ((unsigned)BACK << 1) | FULL;
			}
			q = char_search(g, q, g->last_search_pattern + 1, dir);
			if (q == NULL) {
				q = char_search(g, dir > 0 ? g->text : g->end - 1,
				                g->last_search_pattern + 1, dir);
				if (q == NULL) {
					status_line_bold(g, "Pattern not found");
					return NULL;
				}
			}
			addr = count_lines(g, g->text, q);
			got_addr = TRUE;
		} else if (isdigit((unsigned char)*p)) {
			num = 0;
			while (isdigit((unsigned char)*p))
				num = num * 10 + *p++ - '0';
			if (!got_addr) {
				addr = num;
				got_addr = TRUE;
			} else {
				addr += sign >= 0 ? num : -num;
			}
			sign = 0;
		} else if (*p == '-' || *p == '+') {
			if (!got_addr) {
				got_addr = TRUE;
			} else {
				addr += sign;
			}
			sign = *p++ == '-' ? -1 : 1;
		} else {
			addr += sign;
			break;
		}
	}
	*result = addr;
	*valid = got_addr;

	return p;
}

#define GET_ADDRESS 0
#define GET_SEPARATOR 1

static char *
get_address(struct editor *g, char *p, int *b, int *e,
            unsigned *got)
{
	int state = GET_ADDRESS;
	int valid;
	int addr;
	char *save_dot = g->dot;

	for (;;) {
		if (isblank((unsigned char)*p)) {
			p++;
		} else if (state == GET_ADDRESS && *p == '%') {
			p++;
			*b = 1;
			*e = count_lines(g, g->text, g->end - 1);
			*got = 3;
			state = GET_SEPARATOR;
		} else if (state == GET_ADDRESS) {
			valid = FALSE;
			p = get_one_address(g, p, &addr, &valid);
			if (p == NULL || !(valid || *p == ',' || *p == ';' || *got & 1))
				break;
			*b = *e;
			*e = addr;
			*got = (*got << 1) | 1;
			state = GET_SEPARATOR;
		} else if (state == GET_SEPARATOR && (*p == ',' || *p == ';')) {
			if (*p == ';')
				g->dot = find_line(g, *e);
			p++;
			state = GET_ADDRESS;
		} else {
			break;
		}
	}
	g->dot = save_dot;

	return p;
}

#define MAX_SUBPATTERN 10

static char *
strchr_backslash(const char *s, int c)
{
	while (*s) {
		if (*s == c)
			return (char *)s;
		if (*s == '\\')
			if (*++s == '\0')
				break;
		s++;
	}
	return NULL;
}

static char *
regex_search(struct editor *g, char *q, regex_t *preg,
             const char *Rorig, size_t *len_F, size_t *len_R,
             char **R)
{
	regmatch_t regmatch[MAX_SUBPATTERN];
	regmatch_t *cur_match;
	char *found = NULL;
	const char *t;
	char *r;

	regmatch[0].rm_so = 0;
	regmatch[0].rm_eo = end_line(g, q) - q;
	if (regexec(preg, q, MAX_SUBPATTERN, regmatch, REG_STARTEND) != 0)
		return found;

	found = q + regmatch[0].rm_so;
	*len_F = (size_t)(regmatch[0].rm_eo - regmatch[0].rm_so);
	*R = NULL;

fill_result:
	*len_R = 0;
	for (t = Rorig, r = *R; *t; t++) {
		size_t len = 1;
		const char *from = t;
		if (*t == '\\') {
			from = ++t;
			if (*t >= '0' && *t < '0' + MAX_SUBPATTERN) {
				cur_match = regmatch + (*t - '0');
				if (cur_match->rm_so >= 0) {
					len = (size_t)(cur_match->rm_eo - cur_match->rm_so);
					from = q + cur_match->rm_so;
				}
			}
		}
		*len_R += len;
		if (*R) {
			memcpy(r, from, len);
			r += len;
		}
	}
	if (*R == NULL) {
		*R = xzalloc(*len_R + 1);
		goto fill_result;
	}

	return found;
}

/*
 * global - implement :g/pattern/cmd and :v/pattern/cmd.
 *
 * p     — points to the delimiter char (the '/' in /pattern/cmd)
 * invert — 0 for :g (run on matching lines), 1 for :v (run on non-matching)
 * b, e  — pre-parsed line range from colon (1-based); got encodes whether
 *          a range was given
 *
 * Algorithm:
 *   Pass 1: walk the range once, record the byte offset of each matching
 *           line start into a heap array.  O(range) with no allocations
 *           inside the loop.
 *   Pass 2: for each saved offset, set g->dot, call colon(g, cmd).
 *           After each command, adjust remaining offsets by the change in
 *           buffer size so that deletions/insertions don't corrupt positions.
 */
static void
global(struct editor *g, char *p, int invert, int b, int e,
       unsigned got)
{
	char delim;
	char *pat;
	char *gcmd;
	char *end_pat;
	int gstart, gend;
	regex_t preg;
	int cflags;
	int *offsets;
	int cap, nmatches;
	int gi, gj;
	char *line;

	if (!*p) {
		status_line_bold(g, ":g requires /pattern/cmd");
		return;
	}

	delim = *p++;

	/* Locate end of pattern and copy it so we can null-terminate. */
	end_pat = strchr_backslash(p, delim);
	if (!end_pat) {
		status_line_bold(g, ":g pattern missing closing delimiter");
		return;
	}
	pat = xstrndup(p, (size_t)(end_pat - p));
	gcmd =
	    xstrdup(end_pat + 1); /* own copy — inner colon mutates the string */

	/* Resolve range.  Default: whole file. */
	gstart = 1;
	gend = count_lines(g, g->text, g->end - 1);
	if (got & 1) {                /* GOT_ADDRESS */
		if ((got & 3) == 3) { /* GOT_RANGE */
			gstart = b;
			gend = e;
		} else {
			gstart = gend = e;
		}
	}
	if (gstart < 1)
		gstart = 1;
	if (gend > count_lines(g, g->text, g->end - 1))
		gend = count_lines(g, g->text, g->end - 1);

	/* Compile pattern. */
	cflags = IS_IGNORECASE(g) ? REG_ICASE : 0;
	memset(&preg, 0, sizeof(preg));
	if (regcomp(&preg, pat, cflags) != 0) {
		status_line_bold(g, ":g bad pattern");
		free(pat);
		return;
	}
	free(pat);

	/* Pass 1: collect byte offsets of matching line starts.
	 * Walk line-by-line with next_line to avoid O(n^2) find_line calls. */
	cap = 64;
	nmatches = 0;
	offsets = xmalloc((size_t)cap * sizeof(int));

	line = find_line(g, gstart);
	for (gi = gstart; gi <= gend && line < g->end; gi++) {
		char *eol = end_line(g, line);
		regmatch_t m;
		int matched;

		m.rm_so = 0;
		m.rm_eo = (regoff_t)(eol - line);
		matched = (regexec(&preg, line, 1, &m, REG_STARTEND) == 0);

		if (matched != invert) {
			if (nmatches == cap) {
				cap *= 2;
				offsets = xrealloc(offsets, (size_t)cap * sizeof(int));
			}
			offsets[nmatches++] = (int)(line - g->text);
		}
		line = next_line(g, line);
	}
	regfree(&preg);

	/* Pass 2: execute command on each recorded line. */
	for (gi = 0; gi < nmatches; gi++) {
		int old_size, size_delta;

		if (offsets[gi] < 0 || offsets[gi] >= (int)(g->end - g->text))
			continue; /* line was swallowed by a previous deletion */

		g->dot = g->text + offsets[gi];
		old_size = (int)(g->end - g->text);

		if (gcmd[0]) {
			char *gcopy = xstrdup(gcmd);
			colon(g, gcopy);
			free(gcopy);
		}

		/* Adjust remaining offsets for any buffer size change. */
		size_delta = (int)(g->end - g->text) - old_size;
		if (size_delta != 0) {
			for (gj = gi + 1; gj < nmatches; gj++)
				if (offsets[gj] > offsets[gi])
					offsets[gj] += size_delta;
		}
	}

	free(offsets);

	if (nmatches > 0 && gcmd[0])
		dot_skip_over_ws(g);
	else if (nmatches == 0)
		status_line_bold(g, "No match");
	free(gcmd);
}

/* ---- per-command handlers -------------------------------------------- */

/*
 * colon_state — all parsed state for one ex command, passed to every handler.
 * Avoids per-handler parameter lists and makes the dispatch table uniform.
 */
struct colon_state {
	char *buf;    /* full command token (buf after get_address), mutable */
	char *cmd;    /* alpha-only command name (points into cs_cmd) */
	char *args;   /* arguments after command name + force flag */
	char *q, *r;  /* range start/end pointers into text */
	int b, e;     /* address line numbers (-1 if not given) */
	unsigned got; /* address bitmask */
	int useforce; /* trailing ! flag */
};

typedef void (*colon_fn)(struct editor *, const struct colon_state *);

static void
colon_do_addr_jump(struct editor *g, int e)
{
	if (e >= 0) {
		g->dot = find_line(g, e);
		dot_skip_over_ws(g);
	}
}

static void
colon_do_linenum(struct editor *g, int e, unsigned got)
{
	if (!(got & 1))
		e = count_lines(g, g->text, g->dot);
	status_line(g, "%d", e);
}

static void
colon_do_delete(struct editor *g, const struct colon_state *cs)
{
	char *q = cs->q, *r = cs->r;

	if (!(cs->got & 1)) {
		q = begin_line(g, g->dot);
		r = end_line(g, g->dot);
	}
	g->dot = yank_delete_current(g, q, r, WHOLE, YANKDEL, ALLOW_UNDO);
	dot_skip_over_ws(g);
}

static void
colon_do_edit(struct editor *g, const struct colon_state *cs)
{
	int size;
	char *fn;

	if (g->modified_count && !cs->useforce) {
		status_line_bold(g, "No write since last change (:%s! overrides)", cs->cmd);
		return;
	}
	fn = g->current_filename;
	if (cs->args[0]) {
		fn = expand_args(g, cs->args);
		if (fn == NULL)
			return;
	} else if (g->current_filename == NULL) {
		status_line_bold(g, "No current filename");
		return;
	}

	size = init_text_buffer(g, fn);

	if (ureg >= 0 && ureg < 28) {
		free(g->reg[ureg]);
		g->reg[ureg] = NULL;
	}
	free(g->reg[g->ydreg]);
	g->reg[g->ydreg] = NULL;
	status_line(g, "'%s'%s%s %uL, %uC", fn,
	            (size < 0 ? " [New file]" : ""),
	            (g->readonly_mode ? " [Readonly]" : ""),
	            count_lines(g, g->text, g->end - 1),
	            (int)(g->end - g->text));
}

static void
colon_do_features(struct editor *g, const struct colon_state *cs)
{
	(void)cs;
	go_bottom_and_clear_to_eol(g);
	cookmode(g);
	show_help();
	rawmode(g);
	hit_return(g);
}

static void
colon_do_file(struct editor *g, const struct colon_state *cs)
{
	char *exp;

	if (cs->e >= 0) {
		status_line_bold(g, "No address allowed on this command");
		return;
	}
	if (cs->args[0]) {
		exp = expand_args(g, cs->args);
		if (exp == NULL)
			return;
		update_filename(g, exp);
		free(exp);
	} else {
		g->last_status_cksum = 0;
	}
}

static void
colon_do_mark(struct editor *g, const struct colon_state *cs)
{
	int idx;

	idx = ((unsigned char)cs->args[0] | 0x20) - 'a';
	if ((unsigned)idx > 25 || cs->args[1] != '\0') {
		status_line_bold(g, "Invalid mark name");
		return;
	}
	g->mark[idx] = (cs->got & 1) ? cs->q : g->dot;
}

static void
colon_do_list(struct editor *g, const struct colon_state *cs)
{
#define MAXPRINT (sizeof(ESC_BOLD_TEXT "^?" ESC_NORM_TEXT) + 1)
	char *q = cs->q, *r = cs->r;
	char *dst;

	if (!(cs->got & 1)) {
		q = begin_line(g, g->dot);
		r = end_line(g, g->dot);
	}
	g->have_status_msg = 1;
	dst = g->status_buffer;
	while (q <= r && dst < g->status_buffer + STATUS_BUFFER_LEN - MAXPRINT) {
		char c;
		int c_is_no_print;

		c = *q++;
		if (c == '\n') {
			*dst++ = '$';
			break;
		}
		c_is_no_print = (c & UTF8_MULTIBYTE_MIN) && !Isprint(c);
		if (c_is_no_print) {
			dst = stpcpy(dst, ESC_BOLD_TEXT);
			*dst++ = '.';
			dst = stpcpy(dst, ESC_NORM_TEXT);
			continue;
		}
		if (c < ' ' || c == 127) {
			*dst++ = '^';
			if (c == 127)
				c = '?';
			else
				c += '@';
		}
		*dst++ = c;
	}
	*dst = '\0';
#undef MAXPRINT
}

static void
colon_do_global(struct editor *g, const struct colon_state *cs)
{
	global(g, cs->args, 0, cs->b, cs->e, cs->got);
}

static void
colon_do_vglobal(struct editor *g, const struct colon_state *cs)
{
	global(g, cs->args, 1, cs->b, cs->e, cs->got);
}

static void
colon_do_quit(struct editor *g, const struct colon_state *cs)
{
	int n;

	if (cs->useforce) {
		if (*cs->cmd == 'q')
			optind = g->cmdline_filecnt;
		g->editing = 0;
		return;
	}
	if (g->modified_count) {
		status_line_bold(g, "No write since last change (:%s! overrides)", cs->cmd);
		return;
	}
	n = g->cmdline_filecnt - optind - 1;
	if (*cs->cmd == 'q' && n > 0) {
		status_line_bold(g, "%u more file(s) to edit", n);
		return;
	}
	if (*cs->cmd == 'n' && n <= 0) {
		status_line_bold(g, "No more files to edit");
		return;
	}
	if (*cs->cmd == 'p') {
		if (optind < 1) {
			status_line_bold(g, "No previous files to edit");
			return;
		}
		optind -= 2;
	}
	g->editing = 0;
}

/*
 * Run cmd via /bin/sh, capture stdout+stderr, insert output into the
 * buffer after the addressed line (or current line if no address).
 * Used by :r!cmd.
 */
static void
colon_do_read_cmd(struct editor *g, int e, unsigned got, const char *cmd)
{
	int pipefd[2];
	pid_t pid;
	char *buf;
	size_t cap, len;
	char *ins_pt;
	int num;
	uintptr_t ofs;

	if (pipe(pipefd) < 0) {
		status_line_bold(g, "pipe: %s", strerror(errno));
		return;
	}
	pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		status_line_bold(g, "fork: %s", strerror(errno));
		return;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(127);
	}
	close(pipefd[1]);

	cap = 4096;
	len = 0;
	buf = xmalloc(cap);
	{
		char tmp[4096];
		ssize_t n;
		while ((n = read(pipefd[0], tmp, sizeof(tmp))) > 0) {
			if (len + (size_t)n + 2 > cap) {
				cap = cap * 2 + (size_t)n;
				buf = xrealloc(buf, cap);
			}
			memcpy(buf + len, tmp, (size_t)n);
			len += (size_t)n;
		}
	}
	close(pipefd[0]);
	waitpid(pid, NULL, 0);

	if (len == 0) {
		free(buf);
		status_line_bold(g, "No output from command");
		return;
	}
	if (buf[len - 1] != '\n')
		buf[len++] = '\n';
	buf[len] = '\0';

	if (e == 0) {
		ins_pt = g->text;
	} else {
		ins_pt = next_line(g, (got & 1) ? find_line(g, e) : g->dot);
		if (ins_pt == g->end - 1)
			++ins_pt;
	}
	num = count_lines(g, g->text, ins_pt);
	if (ins_pt == g->end)
		num++;
	ofs = (uintptr_t)(ins_pt - g->text);
	text_hole_make(g, ins_pt, (int)len);
	ins_pt = g->text + ofs;
	memcpy(ins_pt, buf, len);
	undo_push_insert(g, ins_pt, (int)len, ALLOW_UNDO);
	g->modified_count++;
	g->dot = find_line(g, num);
	status_line(g, "%d lines read",
	            count_lines(g, ins_pt, ins_pt + len - 1));
	free(buf);
}

static void
colon_do_read(struct editor *g, const struct colon_state *cs)
{
	int size;
	int num;
	char *fn = g->current_filename;
	char *q;

	if (cs->args[0] == '!') {
		colon_do_read_cmd(g, cs->e, cs->got, skip_whitespace(cs->args + 1));
		return;
	}

	if (cs->args[0]) {
		fn = expand_args(g, cs->args);
		if (fn == NULL)
			return;
		init_filename(g, fn);
	} else if (g->current_filename == NULL) {
		status_line_bold(g, "No current filename");
		return;
	}
	if (cs->e == 0) {
		q = g->text;
	} else {
		q = next_line(g, (cs->got & 1) ? find_line(g, cs->e) : g->dot);
		if (q == g->end - 1)
			++q;
	}
	num = count_lines(g, g->text, q);
	if (q == g->end)
		num++;
	{
		uintptr_t ofs = (uintptr_t)(q - g->text);
		size = file_insert(g, fn, q, 0);
		q = g->text + ofs;
	}
	if (size < 0)
		return;
	status_line(g, "'%s'%s %uL, %uC", fn,
	            (g->readonly_mode ? " [Readonly]" : ""),
	            count_lines(g, q, q + size - 1), size);
	g->dot = find_line(g, num);
}

static void
colon_do_rewind(struct editor *g, const struct colon_state *cs)
{
	if (g->modified_count && !cs->useforce) {
		status_line_bold(g, "No write since last change (:%s! overrides)", cs->cmd);
	} else {
		optind = -1;
		g->editing = 0;
	}
}

static void
colon_do_set(struct editor *g, const struct colon_state *cs)
{
	char *args = cs->args;
	char *argp;
	char *argn;
	char oldch;

	if (!args[0] || strcmp(args, "all") == 0) {
		status_line_bold(g,
		                 "%sautoindent "
		                 "%sexpandtab "
		                 "%sflash "
		                 "%signorecase "
		                 "%sshowmatch "
		                 "%snumber "
		                 "%srelativenumber "
		                 "tabstop=%u "
		                 "cshp=%d cshpi=%d",
		                 IS_AUTOINDENT(g) ? "" : "no",
		                 IS_EXPANDTAB(g) ? "" : "no",
		                 IS_ERR_METHOD(g) ? "" : "no",
		                 IS_IGNORECASE(g) ? "" : "no",
		                 IS_SHOWMATCH(g) ? "" : "no",
		                 IS_NUMBER(g) ? "" : "no",
		                 IS_RELATIVENUMBER(g) ? "" : "no",
		                 g->tabstop, term_cursor_shape_get_configured());
		return;
	}
	argp = args;
	while (*argp) {
		int i = 0;
		if (argp[0] == 'n' && argp[1] == 'o')
			i = 2;
		argn = skip_non_whitespace(argp);
		oldch = *argn;
		*argn = '\0';
		setops(g, argp, i);
		*argn = oldch;
		argp = skip_whitespace(argn);
	}
}

static void
colon_do_substitute(struct editor *g, const struct colon_state *cs)
{
	char *buf = cs->args; /* already past 's' and whitespace */
	char *q = cs->q;
	int b = cs->b, e = cs->e;
	unsigned got = cs->got;
	char c;
	char *F;
	char *R = NULL;
	char *flags;
	size_t len_F;
	size_t len_R;
	int i;
	int gflag = 0;
	int subs = 0;
	int last_line = 0;
	int lines = 0;
	regex_t preg;
	int cflags;
	char *Rorig;
	int undo = 0;

	c = buf[0];
	F = buf + 1;
	R = strchr_backslash(F, c);
	if (!R) {
		status_line(g, ":s expression missing delimiters");
		return;
	}
	len_F = (size_t)(R - F);
	*R++ = '\0';
	flags = strchr_backslash(R, c);
	if (flags) {
		*flags++ = '\0';
		gflag = *flags;
	}

	if (len_F) {
		free(g->last_search_pattern);
		g->last_search_pattern = xstrdup(F - 1);
		g->last_search_pattern[0] = '/';
	} else if (g->last_search_pattern[1] == '\0') {
		status_line_bold(g, "No previous search");
		return;
	} else {
		F = g->last_search_pattern + 1;
		len_F = strlen(F);
	}

	if (!(got & 1)) {
		q = begin_line(g, g->dot);
		b = e = count_lines(g, g->text, q);
	} else if (!((got & 3) == 3)) {
		b = e;
	}

	Rorig = R;
	cflags = IS_IGNORECASE(g) ? REG_ICASE : 0;
	memset(&preg, 0, sizeof(preg));
	if (regcomp(&preg, F, cflags) != 0) {
		status_line(g, ":s bad search pattern");
		regfree(&preg);
		return;
	}
	for (i = b; i <= e; i++) {
		char *ls = q;
		char *found;
	vc4:
		found = regex_search(g, q, &preg, Rorig, &len_F, &len_R, &R);
		if (found) {
			uintptr_t bias;
			if (len_F)
				text_hole_delete(g, found, found + len_F - 1,
				                 undo++ ? ALLOW_UNDO_CHAIN : ALLOW_UNDO);
			if (len_R != 0) {
				bias = string_insert(g, found, R,
				                     undo++ ? ALLOW_UNDO_CHAIN : ALLOW_UNDO);
				found += bias;
				ls += bias;
			}
			free(R);
			R = NULL;
			if (len_F || len_R != 0) {
				g->dot = ls;
				subs++;
				if (last_line != i) {
					last_line = i;
					++lines;
				}
			}
			if (gflag == 'g') {
				if ((found + len_R) < end_line(g, ls)) {
					q = found + len_R;
					goto vc4;
				}
			}
		}
		q = next_line(g, ls);
	}
	regfree(&preg);

	if (subs == 0) {
		status_line_bold(g, "No match");
	} else {
		dot_skip_over_ws(g);
		if (subs > 1)
			status_line(g, "%d substitutions on %d lines", subs, lines);
	}
}

static void
colon_do_version(struct editor *g, const struct colon_state *cs)
{
	(void)cs;
	status_line(g, "standalone vi");
}

static void
colon_do_write(struct editor *g, const struct colon_state *cs)
{
	char *q = cs->q, *r = cs->r;
	int should_write;
	int size;
	int l;
	char *fn = g->current_filename;
	char *exp = NULL;

	should_write = (g->modified_count != 0 || cs->cmd[0] != 'x');
	if (should_write) {
		if (cs->args[0]) {
			struct stat statbuf;

			exp = expand_args(g, cs->args);
			if (exp == NULL)
				return;
			if (!cs->useforce && (fn == NULL || strcmp(fn, exp) != 0) &&
			    stat(exp, &statbuf) == 0) {
				status_line_bold(g, "File exists (:w! overrides)");
				free(exp);
				return;
			}
			fn = exp;
			init_filename(g, fn);
		} else if (g->readonly_mode && !cs->useforce && fn) {
			status_line_bold(g, "'%s' is read only", fn);
			return;
		}
		size = l = 0;
		size = r - q + 1;
		l = file_write(g, fn, q, r);
	} else {
		size = l = 0;
	}
	if (should_write && l < 0) {
		if (l == -1)
			status_line_bold_errno(g, fn);
	} else if (should_write) {
		int nlines = (l > 0) ? count_lines(g, q, q + l - 1) : 0;
		status_line(g, "'%s' %uL, %uC", fn, nlines, l);
	}
	if (!should_write || l == size) {
		if (should_write && q == g->text && q + l == g->end) {
			g->modified_count = 0;
			g->last_modified_count = -1;
		}
		if (cs->cmd[1] == 'n') {
			g->editing = 0;
		} else if (cs->cmd[0] == 'x' || cs->cmd[1] == 'q') {
			int n = g->cmdline_filecnt - optind - 1;
			if (n > 0) {
				if (!cs->useforce) {
					status_line_bold(g, "%u more file(s) to edit", n);
					free(exp);
					return;
				}
				optind = g->cmdline_filecnt;
			}
			g->editing = 0;
		}
	}
	free(exp);
}

static void
colon_do_yank(struct editor *g, const struct colon_state *cs)
{
	char *q = cs->q, *r = cs->r;
	int lines;

	if (!(cs->got & 1)) {
		q = begin_line(g, g->dot);
		r = end_line(g, g->dot);
	}
	text_yank(g, q, r, g->ydreg, WHOLE);
	lines = count_lines(g, q, r);
	status_line(g, "Yank %d lines (%d chars) into [%c]", lines,
	            strlen(g->reg[g->ydreg]), what_reg(g));
}

/* ---- :run dispatcher -------------------------------------------------- */

/*
 * Tokenise buf into argv[].  Tokens are whitespace-separated; single- and
 * double-quoted tokens may contain spaces.  Modifies buf in-place.
 * Returns argc (never exceeds max_argc).
 */
#define RUN_MAX_ARGS 32
static int
run_tokenize(char *buf, char *argv[], int max_argc)
{
	int argc = 0;
	char *p = buf;

	while (*p && argc < max_argc) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		if (*p == '"' || *p == '\'') {
			char q = *p++;
			argv[argc++] = p;
			while (*p && *p != q)
				p++;
			if (*p)
				*p++ = '\0';
		} else {
			argv[argc++] = p;
			while (*p && *p != ' ' && *p != '\t')
				p++;
			if (*p)
				*p++ = '\0';
		}
	}
	return argc;
}

static void
colon_do_run(struct editor *g, const struct colon_state *cs)
{
	char *argv[RUN_MAX_ARGS];
	int argc;
	char *range_start, *range_end;

	if (cs->got & 1) {
		range_start = cs->q;
		range_end = cs->r;
	} else {
		range_start = g->text;
		range_end = g->end - 1;
	}

	argc = run_tokenize(cs->args, argv, RUN_MAX_ARGS);
	if (argc == 0) {
		status_line(g, ":run: no command given");
		return;
	}
	run_dispatch(g, argc, argv, range_start, range_end);
}

/* ---- shell execution ------------------------------------------------- */

/*
 * Run a shell command, show its output on screen, wait for Enter.
 * Used by :!cmd.  An empty cmd launches $SHELL interactively.
 */
static void
colon_do_shell(struct editor *g, const char *cmd)
{
	const char *sh;

	if (!cmd || !*cmd) {
		sh = getenv("SHELL");
		if (!sh || !*sh)
			sh = "/bin/sh";
	} else {
		sh = cmd;
	}

	go_bottom_and_clear_to_eol(g);
	fflush(NULL);
	cookmode(g);
	system(sh);
	fputs("\n[Press ENTER to continue]", stdout);
	fflush(NULL);
	{
		char buf[1];
		while (read(STDIN_FILENO, buf, 1) == 1) {
			if (buf[0] == '\r' || buf[0] == '\n' ||
			    buf[0] == ASCII_ESC)
				break;
		}
	}
	rawmode(g);
	redraw(g, TRUE);
}

/* ---- dispatcher ------------------------------------------------------ */

/*
 * Dispatch table for colon commands.  The engine does a linear scan and
 * calls the first entry whose name starts with cmd (prefix match) and
 * whose minimum abbreviation length is satisfied.
 *
 * Ordering rules that matter:
 *   features before file     — 'f' → features; 'fi' mismatches "fe..." → file
 *   set before substitute    — 'se' → set (min=2); 's' skips set → substitute
 *   read before rewind       — 'r','re' → read; 'rew' mismatches "rea" → rewind
 *   vglobal before version   — 'v' → vglobal; 've' mismatches "vg" → version
 *   write before wq/wn/x     — order within write aliases is irrelevant
 */
struct colon_entry {
	const char *name;
	int min; /* 0 = any prefix; >0 = require at least this many chars */
	colon_fn fn;
};

static const struct colon_entry colon_cmds[] = {
    {"delete", 1, colon_do_delete},
    {"edit", 1, colon_do_edit},
    {"features", 1, colon_do_features},
    {"file", 2, colon_do_file}, /* 'fi' to avoid clash with 'f'=features */
    {"global", 1, colon_do_global},
    {"list", 1, colon_do_list},
    {"mark", 2, colon_do_mark}, /* 'ma' to avoid single-letter ambiguity */
    {"next", 1, colon_do_quit},
    {"prev", 1, colon_do_quit},
    {"quit", 1, colon_do_quit},
    {"read", 1, colon_do_read},
    {"rewind", 3, colon_do_rewind}, /* 'rew' to avoid clash with 'r'/'re'=read */
    {"run", 2, colon_do_run},        /* 'ru' to avoid clash with 'r'=read */
    {"set", 2, colon_do_set},       /* 'se' to avoid clash with 's'=substitute */
    {"substitute", 1, colon_do_substitute},
    {"version", 2, colon_do_version}, /* 've' to avoid clash with 'v'=vglobal */
    {"vglobal", 1, colon_do_vglobal},
    {"wn", 2, colon_do_write},
    {"wq", 2, colon_do_write},
    {"write", 1, colon_do_write},
    {"x", 1, colon_do_write},
    {"yank", 1, colon_do_yank},
};

void
colon(struct editor *g, char *buf)
{
	char cs_cmd[16]; /* longest command name: "substitute" (10 chars) */
	struct colon_state cs;
	size_t i;

	while (*buf == ':')
		buf++;
	buf = skip_whitespace(buf);
	if (!*buf || *buf == '"')
		goto done;

	cs.b = cs.e = -1;
	cs.got = 0;
	buf = get_address(g, buf, &cs.b, &cs.e, &cs.got);
	if (buf == NULL)
		goto done;
	cs.buf = buf;

	if (*buf == '!') {
		colon_do_shell(g, skip_whitespace(buf + 1));
		goto done;
	}
	if (*buf == '=') {
		colon_do_linenum(g, cs.e, cs.got);
		goto done;
	}

	/* extract alpha-only command name; detect trailing ! as force flag */
	{
		const char *src = buf;
		int n = 0;
		while (*src && isalpha((unsigned char)*src) &&
		       n < (int)sizeof(cs_cmd) - 1)
			cs_cmd[n++] = *src++;
		cs_cmd[n] = '\0';
		cs.useforce = (*src == '!' &&
		               (!src[1] || isspace((unsigned char)src[1])));
		src += cs.useforce ? 1 : 0;
		cs.args = skip_whitespace((char *)src);
	}
	cs.cmd = cs_cmd;

	cs.q = g->text;
	cs.r = g->end - 1;
	if (cs.got & 1) { /* GOT_ADDRESS */
		int lines;
		if (cs.e < 0 ||
		    cs.e > (lines = count_lines(g, g->text, g->end - 1))) {
			status_line_bold(g, "Invalid range");
			goto done;
		}
		cs.q = cs.r = find_line(g, cs.e);
		if ((cs.got & 3) != 3) { /* !GOT_RANGE */
			cs.r = end_line(g, cs.q);
		} else {
			if (cs.b < 0 || cs.b > lines || cs.b > cs.e) {
				status_line_bold(g, "Invalid range");
				goto done;
			}
			cs.q = find_line(g, cs.b);
			cs.r = end_line(g, cs.r);
		}
	}

	if (cs_cmd[0] == '\0') {
		colon_do_addr_jump(g, cs.e);
		goto done;
	}

	{
		int cmdlen = (int)strlen(cs_cmd);
		for (i = 0; i < ARRAY_SIZE(colon_cmds); i++) {
			const struct colon_entry *e = &colon_cmds[i];
			if (strncmp(cs_cmd, e->name, (size_t)cmdlen) == 0 &&
			    cmdlen >= e->min) {
				e->fn(g, &cs);
				goto done;
			}
		}
		not_implemented(g, cs_cmd);
	}

done:
	g->dot = bound_dot(g, g->dot);
}
