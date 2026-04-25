/*
 * excore.c - shared ex helper routines.
 *
 * setops  — parses a ':set' argument string and mutates g->setops
 *              (the option bitmask) or g->tabstop.  Reports unknown options
 *              via the status_line_bold hook.
 *
 * expand_args — expands '%' (current filename) and '#' (alternate
 *                  filename) in an ex command argument string.
 *
 * These helpers are split out of ex.c so that session.c can call
 * setops when processing startup commands without pulling in the
 * full ex command machinery.
 *
 */
#include "excore.h"

#include "status.h"
#include "term.h"

void
setops(struct editor *g, char *args, int flg_no)
{
	/*
	 * == Parse one :set argument and update g->setops or g->tabstop ==
	 *
	 * args is the option string (e.g. "ts=4", "noignorecase", "number").
	 * flg_no is 1 if the "no" prefix was already stripped from args.
	 * Integer options (tabstop, cursorshape, cursorshape_insert) require
	 * an "=" value and reject flg_no.  Boolean options are set or cleared
	 * in g->setops according to flg_no.  Unknown option names are reported
	 * via status_line_bold.
	 */
	char *eq;
	int index;

	eq = strchr(args, '=');
	if (eq)
		*eq = '\0';
	index = index_in_strings(OPTS_STR, args + flg_no);
	if (eq)
		*eq = '=';
	if (index < 0) {
	bad:
		status_line_bold(g, "bad option: %s", args);
		return;
	}

	index = 1 << (index >> 1);

	if (index & VI_TABSTOP) {
		int t;
		if (!eq || flg_no)
			goto bad;
		t = (int)strtoul(eq + 1, NULL, 10);
		if (t <= 0 || t > 32)
			goto bad;
		g->tabstop = t;
		g->refresh_last_screenbegin = NULL;
		return;
	}
	if (index & VI_CURSORSHAPE) {
		int t;
		if (!eq || flg_no)
			goto bad;
		t = (int)strtoul(eq + 1, NULL, 10);
		if (term_cursor_shape_set_configured(t) < 0)
			goto bad;
		return;
	}
	if (index & VI_CURSORSHAPE_INSERT) {
		int t;
		if (!eq || flg_no)
			goto bad;
		t = (int)strtoul(eq + 1, NULL, 10);
		if (term_cursor_shape_set_insert(t) < 0)
			goto bad;
		return;
	}
	if (eq)
		goto bad;
	if (index & VI_SYNTAX) {
		/* :set syntax toggles; :set nosyntax forces off. */
		if (flg_no)
			g->setops &= ~index;
		else
			g->setops ^= index;
		g->refresh_last_screenbegin = NULL;
		return;
	}
	if (flg_no)
		g->setops &= ~index;
	else
		g->setops |= index;

	if (index & (VI_NUMBER | VI_RELATIVENUMBER))
		g->refresh_last_screenbegin = NULL;
}

char *
expand_args(struct editor *g, char *args)
{
	/*
	 * == Expand % and # in an ex command argument string ==
	 *
	 * Returns a newly allocated string with:
	 *   %  replaced by g->current_filename
	 *   #  replaced by g->alt_filename
	 *   \X unescaped (backslash removed)
	 *
	 * Returns NULL and shows an error if % or # is used when the
	 * corresponding filename is not set.  The caller is responsible for
	 * free()ing the result.
	 */
	char *s;
	const char *replace;

	args = xstrdup(args);
	for (s = args; *s; s++) {
		unsigned n;

		if (*s == '%') {
			replace = g->current_filename;
		} else if (*s == '#') {
			replace = g->alt_filename;
		} else {
			if (*s == '\\' && s[1] != '\0') {
				char *t;
				for (t = s; *t; t++)
					*t = t[1];
				s++;
			}
			continue;
		}

		if (replace == NULL) {
			free(args);
			status_line_bold(g, "No previous filename");
			return NULL;
		}

		n = (unsigned)(s - args);
		{
			size_t rlen = strlen(replace);
			size_t tail = strlen(s + 1);
			char *p = xmalloc(n + rlen + tail + 1);
			memcpy(p, args, n);
			memcpy(p + n, replace, rlen);
			memcpy(p + n + rlen, s + 1, tail + 1);
			free(args);
			args = p;
			s = args + n + rlen;
		}
	}

	return args;
}
