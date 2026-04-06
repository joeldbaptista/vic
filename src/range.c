/*
 * range.c - operator-pending range resolution.
 *
 * range_find() resolves the motion/range for an operator command.  When
 * called with a cmd_ctx (parser path), it reads the range key and anchor
 * from ctx->rg and ctx->anchor.  When ctx is NULL or ctx->rg is zero (the
 * '.' replay path), it falls back to reading via get_motion_char().
 *
 * Special cases handled here:
 *   - doubled operator (dd, cc, yy) selects whole lines
 *   - text objects (iw, a(, …) are delegated to textobj.c via
 *     textobj_find_range
 *   - linewise vs characterwise vs blockwise classification
 *   - correct endpoint trimming for motions that overshoot (b, ^, 0, …)
 */
#include "range.h"

#include "codepoint.h"
#include "input.h"
#include "line.h"
#include "textobj.h"

static int
is_ascii_space(unsigned char c)
{
	return c < UTF8_MULTIBYTE_MIN && isspace(c);
}

static int
is_ascii_punct(unsigned char c)
{
	return c < UTF8_MULTIBYTE_MIN && ispunct(c);
}

int
range_find(struct editor *g, char **start, char **stop, const struct cmd_ctx *ctx)
{
	char *p;
	char *q;
	char *t;
	int buftype = -1;
	int c;
	int cmd = ctx ? (int)(unsigned char)ctx->op : 0;

	p = q = g->dot;

	if (ctx && ctx->op == 'Y') {
		/* Y yanks the current line — treat as doubled 'y' */
		c = 'y';
	} else if (ctx && ctx->rg) {
		/* parser already captured the range key and optional count */
		c = (int)(unsigned char)ctx->rg;
		if (ctx->rcount > 1)
			g->cmdcnt = ctx->rcount;
	} else {
		/* interactive fallback: read range key (and optional count) */
		c = get_motion_char(g);
	}

	if (c == 'a' || c == 'i') {
		int obj;
		if (ctx && ctx->anchor)
			obj = (int)(unsigned char)ctx->anchor;
		else
			obj = get_one_char(g);
		if (textobj_find_range(g, c, obj, &p, &q, &buftype) == 0) {
			*start = p;
			*stop = q;
			return buftype;
		}
		return -1;
	}

	if ((cmd == 'Y' || cmd == c) && strchr("cdy><", c)) {
		buftype = WHOLE;
		if (--g->cmdcnt > 0) {
			do_cmd(g, 'j', NULL);
			if (g->cmd_error)
				buftype = -1;
		}
	} else if (strchr("^%$0bBeEfFtThnN/?|{}\b\177", c)) {
		buftype = strchr("nN/?", c) ? MULTI : PARTIAL;
		if (strchr("fFtT", c) && ctx && ctx->anchor) {
			struct cmd_ctx mc;
			memset(&mc, 0, sizeof(mc));
			mc.op = (char)c;
			mc.count = 1;
			mc.rcount = 1;
			mc.anchor = ctx->anchor;
			do_cmd(g, c, &mc);
		} else {
			do_cmd(g, c, NULL);
		}
		if (p == g->dot)
			buftype = -1;
	} else if (strchr("wW", c)) {
		char *prev;

		buftype = MULTI;
		do_cmd(g, c, NULL);
		if (g->dot > p && (!at_eof(g, g->dot) ||
		                   (c == 'w' && is_ascii_punct((unsigned char)*g->dot))))
			g->dot = cp_prev(g, g->dot);
		t = g->dot;
		while (g->dot > p && is_ascii_space((unsigned char)*g->dot)) {
			prev = cp_prev(g, g->dot);
			if (*g->dot == '\n')
				t = prev;
			g->dot = prev;
		}
		if (cmd != 'c' && g->dot != t && *g->dot != '\n')
			g->dot = t;
	} else if (strchr("GHL+-gjk'\r\n", c)) {
		buftype = WHOLE;
		if (c == '\'' && ctx && ctx->anchor) {
			struct cmd_ctx mc;
			memset(&mc, 0, sizeof(mc));
			mc.op = '\'';
			mc.count = 1;
			mc.rcount = 1;
			mc.anchor = ctx->anchor;
			do_cmd(g, c, &mc);
		} else {
			do_cmd(g, c, NULL);
		}
		if (g->cmd_error)
			buftype = -1;
	} else if (c == ' ' || c == 'l') {
		char *m;
		int moved;
		int tmpcnt = (g->cmdcnt ?: 1);
		buftype = PARTIAL;
		do_cmd(g, c, NULL);
		moved = 0;
		for (m = p; m < g->dot; m = cp_next(g, m))
			moved++;
		if (tmpcnt == moved)
			g->dot = cp_prev(g, g->dot);
	}

	if (buftype == -1) {
		if (c != 27)
			indicate_error(g);
		return buftype;
	}

	q = g->dot;
	if (q < p) {
		t = q;
		q = p;
		p = t;
	}

	if (q > p) {
		if (strchr("^0bBFThnN/?|\b\177", c)) {
			q = cp_prev(g, q);
		} else if (strchr("{}", c)) {
			buftype =
			    (p == begin_line(g, p) && (*q == '\n' || at_eof(g, q)))
			        ? WHOLE
			        : MULTI;
			if (!at_eof(g, q)) {
				q = cp_prev(g, q);
				if (q > p && p != begin_line(g, p))
					q = cp_prev(g, q);
			}
		}
	}

	*start = p;
	*stop = q;
	return buftype;
}
