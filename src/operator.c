/*
 * operator.c - yank/delete/change operators and register management.
 *
 * Implements the operator commands that act on a range of text:
 *   d / x / X  — delete into register
 *   y          — yank into register
 *   c / C / D  — change / change-to-EOL / delete-to-EOL
 *   s / S      — substitute
 *   r          — replace character(s)
 *   ~ / gu / gU — case operators (via operator_run_change_delete_yank_cmd)
 *   > / <      — indent / de-indent
 *   p / P      — put from register
 *
 * Register helpers (text_yank, what_reg, yank_delete,
 * yank_status) are also here — they are called directly by visual.c
 * and ex.c.
 *
 * Range resolution is delegated to range.c via range_find().
 */
#include "operator.h"

#include "buffer.h"
#include "codepoint.h"
#include "editcmd.h"
#include "input.h"
#include "line.h"
#include "motion.h"
#include "range.h"
#include "status.h"
#include "undo.h"

enum {
	YANKONLY = FALSE,
	YANKDEL = TRUE,
};

static void
shared_yank_path(char *buf, size_t size)
{
	const char *base;
	const char *home;

	base = getenv("XDG_CACHE_HOME");
	if (base && base[0]) {
		snprintf(buf, size, "%s/vic/yank", base);
		return;
	}
	home = getenv("HOME");
	if (!home || !home[0])
		home = "/tmp";
	snprintf(buf, size, "%s/.cache/vic/yank", home);
}

static void
shared_yank_out(struct editor *g)
{
	char path[PATH_MAX];
	char dir[PATH_MAX];
	char *sep;
	int fd;
	const char *s;

	s = g->reg[SHARED_REG];
	if (!s)
		return;
	shared_yank_path(path, sizeof(path));
	snprintf(dir, sizeof(dir), "%s", path);
	sep = strrchr(dir, '/');
	if (sep) {
		char *sep2;

		*sep = '\0';
		sep2 = strrchr(dir, '/');
		if (sep2) {
			*sep2 = '\0';
			mkdir(dir, 0700);
			*sep2 = '/';
		}
		mkdir(dir, 0700);
	}
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return;
	full_write(fd, s, strlen(s));
	close(fd);
}

void
shared_yank_in(struct editor *g)
{
	char path[PATH_MAX];
	char *content;
	size_t len;

	shared_yank_path(path, sizeof(path));
	content = xmalloc_open_read_close(path, &len);
	if (!content)
		return;
	free(g->reg[SHARED_REG]);
	g->reg[SHARED_REG] = content;
	if (len > 0 && content[len - 1] == '\n')
		g->regtype[SHARED_REG] = WHOLE;
	else if (memchr(content, '\n', len))
		g->regtype[SHARED_REG] = MULTI;
	else
		g->regtype[SHARED_REG] = PARTIAL;
}

static void
yank_sync_out(struct editor *g)
{
	char path[64];
	const char *s = g->reg[g->ydreg];
	int fd;

	if (!s)
		return;
	snprintf(path, sizeof(path), "/tmp/vic-%ld.dat", (long)g->session_epoch);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return;
	full_write(fd, s, strlen(s));
	close(fd);
}

void
yank_sync_in(struct editor *g)
{
	char path[64];
	char *content;
	size_t len;

	snprintf(path, sizeof(path), "/tmp/vic-%ld.dat", (long)g->session_epoch);
	content = xmalloc_open_read_close(path, &len);
	if (!content)
		return;
	free(g->reg[g->ydreg]);
	g->reg[g->ydreg] = content;
	if (len > 0 && content[len - 1] == '\n')
		g->regtype[g->ydreg] = WHOLE;
	else if (memchr(content, '\n', len))
		g->regtype[g->ydreg] = MULTI;
	else
		g->regtype[g->ydreg] = PARTIAL;
}

char *
text_yank(struct editor *g, char *p, char *q, int dest, int buftype)
{
	char *oldreg = g->reg[dest];
	int cnt = q - p;

	if (cnt < 0) {
		p = q;
		cnt = -cnt;
	}
	g->reg[dest] = xstrndup(p, cnt + 1);
	g->regtype[dest] = buftype;
	free(oldreg);

	if (dest == SHARED_REG)
		shared_yank_out(g);
	else if (dest == (int)g->ydreg)
		yank_sync_out(g);

	return p;
}

char
what_reg(const struct editor *g)
{
	if (g->ydreg == SHARED_REG)
		return '+';
	if (g->ydreg <= 25)
		return 'a' + (char)g->ydreg;
	if (g->ydreg == ureg)
		return 'U';
	return 'D';
}

void
yank_status(struct editor *g, const char *op, const char *p, int cnt)
{
	int lines;
	int chars;

	lines = 0;
	chars = 0;
	while (*p) {
		++chars;
		if (*p++ == '\n')
			++lines;
	}
	status_line(g, "%s %d lines (%d chars) from [%c]", op, lines * cnt,
	            chars * cnt, what_reg(g));
}

char *
yank_delete(struct editor *g, char *start, char *stop, int buftype,
            int do_delete, int undo, int dest_reg)
{
	char *p;

	if (start > stop) {
		p = start;
		start = stop;
		stop = p;
	}
	if (buftype == PARTIAL && *start == '\n')
		return start;
	p = start;
	text_yank(g, start, stop, dest_reg, buftype);
	if (do_delete)
		p = text_hole_delete(g, start, stop, undo);

	return p;
}

char *
yank_delete_current(struct editor *g, char *start, char *stop,
                    int buftype, int yf, int undo)
{
	return yank_delete(g, start, stop, buftype, yf == YANKDEL, undo, g->ydreg);
}

void
operator_run_change_delete_yank_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	int c = (int)(unsigned char)ctx->op;
	int yf = YANKDEL;
	int buftype;
	char *p;
	char *q;
	char *save_dot = NULL;
	char *savereg = g->reg[g->ydreg];

	if (c == 'y' || c == 'Y')
		yf = YANKONLY;

	buftype = range_find(g, &p, &q, ctx);
	if (buftype == -1) {
		reset_ydreg(g);
		return;
	}

	if (buftype == WHOLE) {
		save_dot = p;
		p = begin_line(g, p);
		if (c == 'c')
			g->newindent = get_column(g, p + indent_len(g, p));
		q = end_line(g, q);
	}

	g->dot = yank_delete_current(g, p, q, buftype, yf, ALLOW_UNDO);
	if (buftype == WHOLE) {
		if (c == 'c') {
			g->cmd_mode = 1;
			g->dot = char_insert(g, g->dot, '\n', ALLOW_UNDO_CHAIN);
			if (g->dot != (g->end - 1) && !IS_AUTOINDENT(g))
				dot_prev(g);
		} else if (c == 'd') {
			dot_begin(g);
			dot_skip_over_ws(g);
		} else {
			g->dot = save_dot;
		}
	}

	if (c == 'c') {
		edit_run_start_insert_cmd(g);
		return;
	}

	if (g->reg[g->ydreg] != savereg)
		yank_status(g, c == 'd' ? "Delete" : "Yank", g->reg[g->ydreg], 1);

	reset_ydreg(g);
}

void
operator_run_delete_or_substitute_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	int c = (int)(unsigned char)ctx->op;
	int allow_undo = ALLOW_UNDO;

	do {
		char *start;
		char *stop;

		if (c == 'X') {
			if (g->dot <= g->text || g->dot[-1] == '\n')
				continue;
			start = cp_prev(g, cp_start(g, g->dot));
		} else {
			start = cp_start(g, g->dot);
			if (*start == '\n')
				continue;
		}
		stop = cp_end(g, start) - 1;
		g->dot = yank_delete_current(g, start, stop, PARTIAL, YANKDEL, allow_undo);
		allow_undo = ALLOW_UNDO_CHAIN;
	} while (--g->cmdcnt > 0);

	reset_ydreg(g);
	if (c == 's')
		edit_run_start_insert_cmd(g);
}

void
operator_run_change_or_delete_eol_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	int c = (int)(unsigned char)ctx->op;
	char *save_dot;

	save_dot = g->dot;
	g->dot = dollar_line(g, g->dot);
	g->dot = yank_delete_current(g, save_dot, g->dot, PARTIAL, YANKDEL, ALLOW_UNDO);
	if (c == 'C')
		edit_run_start_insert_cmd(g);
	else
		reset_ydreg(g);
}

void
operator_run_replace_char_cmd(struct editor *g, const struct cmd_ctx *ctx)
{
	int c1;
	int allow_undo = ALLOW_UNDO;

	c1 = (ctx && ctx->anchor) ? (int)(unsigned char)ctx->anchor : get_one_char(g);
	if (c1 == ASCII_ESC) {
		reset_ydreg(g);
		return;
	}

	if (end_line(g, g->dot) - g->dot < (g->cmdcnt ?: 1)) {
		indicate_error(g);
		reset_ydreg(g);
		return;
	}

	do {
		g->dot = text_hole_delete(g, g->dot, g->dot, allow_undo);
		allow_undo = ALLOW_UNDO_CHAIN;
		g->dot = char_insert(g, g->dot, c1, allow_undo);
	} while (--g->cmdcnt > 0);
	dot_left(g);
	reset_ydreg(g);
}

void
operator_run_flip_case_cmd(struct editor *g)
{
	int undo_del = UNDO_DEL;

	do {
		unsigned char ch = (unsigned char)*g->dot;
		if (ch < UTF8_MULTIBYTE_MIN && isalpha(ch)) {
			undo_push(g, g->dot, 1, undo_del);
			*g->dot = islower(ch) ? toupper(ch) : tolower(ch);
			undo_push(g, g->dot, 1, UNDO_INS_CHAIN);
			undo_del = UNDO_DEL_CHAIN;
		}
		dot_right(g);
	} while (--g->cmdcnt > 0);
	reset_ydreg(g);
}
