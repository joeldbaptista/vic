/*
 * buffer.c - gap-buffer mutation and file I/O.
 *
 * Owns the raw text store: text_hole_make opens a gap of <size> bytes at
 * position p (adjusting all live pointers via the returned bias), and
 * text_hole_delete closes a range.  char_insert is the single-character
 * insert path used by insert/replace mode; it handles tab expansion,
 * auto-indent, backspace, showmatch, and undo queuing.  string_insert and
 * file_insert are bulk variants.
 *
 */
#include "buffer.h"

#include "codepoint.h"
#include "input.h"
#include "line.h"
#include "status.h"
#include "undo.h"

uintptr_t
text_hole_make(struct editor *g, char *p, int size)
{
	char *new_text;
	int i;
	uintptr_t bias = 0;

	if (size <= 0)
		return bias;

	g->end += size;
	if (g->end >= (g->text + g->text_size)) {
		/* Double capacity up to 1 MB, then grow by 1 MB steps.
		 * This amortises reallocs during scripted bulk inserts while
		 * keeping overshoot bounded for large files. */
		int needed = (int)(g->end - g->text);
		if (g->text_size == 0)
			g->text_size = 4096;
		while (g->text_size < needed) {
			if (g->text_size < 1024 * 1024)
				g->text_size *= 2;
			else
				g->text_size += 1024 * 1024;
		}
		new_text = xrealloc(g->text, g->text_size);
		bias = (uintptr_t)(new_text - g->text);
		g->screenbegin += bias;
		g->dot += bias;
		g->end += bias;
		p += bias;
		for (i = 0; i < (int)ARRAY_SIZE(g->mark); i++)
			if (g->mark[i])
				g->mark[i] += bias;
		g->text = new_text;
	}
	memmove(p + size, p, g->end - size - p);

	return bias;
}

char *
text_hole_delete(struct editor *g, char *p, char *q, int undo)
{
	char *src;
	char *dest;
	int cnt;
	int hole_size;

	src = q + 1;
	dest = p;
	if (q < p) {
		src = p + 1;
		dest = q;
	}
	hole_size = q - p + 1;
	cnt = g->end - src;
	switch (undo) {
	case NO_UNDO:
		break;
	case ALLOW_UNDO:
		undo_push(g, p, (unsigned)hole_size, UNDO_DEL);
		break;
	case ALLOW_UNDO_CHAIN:
		undo_push(g, p, (unsigned)hole_size, UNDO_DEL_CHAIN);
		break;
	case ALLOW_UNDO_QUEUED:
		undo_push(g, p, (unsigned)hole_size, UNDO_DEL_QUEUED);
		break;
	}
	g->modified_count--;
	if (src < g->text || src > g->end)
		goto thd0;
	if (dest < g->text || dest >= g->end)
		goto thd0;
	g->modified_count++;
	if (src >= g->end)
		goto thd_atend;
	memmove(dest, src, (size_t)cnt);
thd_atend:
	g->end = g->end - hole_size;
	if (dest >= g->end)
		dest = g->end - 1;
	if (g->end <= g->text)
		dest = g->end = g->text;
thd0:
	return dest;
}

int
file_insert(struct editor *g, const char *fn, char *p, int initial)
{
	int cnt = -1;
	int fd;
	int size;
	struct stat statbuf;

	if (p < g->text)
		p = g->text;
	if (p > g->end)
		p = g->end;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		if (!initial)
			status_line_bold_errno(g, fn);
		return cnt;
	}

	if (fstat(fd, &statbuf) < 0) {
		status_line_bold_errno(g, fn);
		goto fi;
	}
	if (!S_ISREG(statbuf.st_mode)) {
		status_line_bold(g, "'%s' is not a regular file", fn);
		goto fi;
	}
	size = (statbuf.st_size < INT_MAX ? (int)statbuf.st_size : INT_MAX);
	p += text_hole_make(g, p, size);
	cnt = (int)full_read(fd, p, (size_t)size);
	if (cnt < 0) {
		status_line_bold_errno(g, fn);
		p = text_hole_delete(g, p, p + size - 1, NO_UNDO);
	} else if (cnt < size) {
		p = text_hole_delete(g, p + cnt, p + size - 1, NO_UNDO);
		status_line_bold(g, "can't read '%s'", fn);
	} else {
		undo_push_insert(g, p, size, ALLOW_UNDO);
	}
fi:
	close(fd);

	if (initial && ((access(fn, W_OK) < 0) ||
	                !(statbuf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))) {
		SET_READONLY_FILE(g->readonly_mode);
	}
	return cnt;
}

static uintptr_t
stupid_insert(struct editor *g, char *p, char c)
{
	uintptr_t bias;

	bias = text_hole_make(g, p, 1);
	p += bias;
	*p = c;
	return bias;
}

char *
char_insert(struct editor *g, char *p, char c, int undo)
{
	size_t len;
	int col;
	int ntab;
	int nspc;
	char *bol;
	bol = begin_line(g, p);

	if (c == ASCII_CTRL_V) {
		p += stupid_insert(g, p, '^');
		c = (char)get_one_char(g);
		*p = c;
		undo_push_insert(g, p, 1, undo);
		p++;
	} else if (c == ASCII_ESC) {
		g->cmd_mode = 0;
		undo_queue_commit(g);
		g->cmdcnt = 0;
		reset_ydreg(g);
		g->last_status_cksum = 0;
		if ((g->dot > g->text) && (p[-1] != '\n'))
			p--;
		if (IS_AUTOINDENT(g)) {
			len = indent_len(g, bol);
			col = get_column(g, bol + len);
			if (len && col == g->indent_col && bol[len] == '\n') {
				text_hole_delete(g, bol, bol + len - 1, undo);
				p = bol;
			}
		}
	} else if (c == ASCII_CTRL_D) {
		char *r = bol + indent_len(g, bol);
		int prev = prev_tabstop(g, get_column(g, r));
		while (r > bol && get_column(g, r) > prev) {
			if (p > bol)
				p--;
			r--;
			r = text_hole_delete(g, r, r, ALLOW_UNDO_QUEUED);
		}

		if (IS_AUTOINDENT(g) &&
		    g->indent_col && r == end_line(g, p)) {
			g->indent_col = get_column(g, p);
			return p;
		}
	} else if (c == '\t' && IS_EXPANDTAB(g)) {
		col = get_column(g, p);
		col = next_tabstop(g, col) - col + 1;
		while (col--) {
			undo_push_insert(g, p, 1, undo);
			p += 1 + stupid_insert(g, p, ' ');
		}
	} else if (c == g->term_orig.c_cc[VERASE] || c == 8 || c == 127) {
		if (g->cmd_mode == 2) {
			if (p > g->rstart) {
				p--;
				undo_pop(g);
			}
		} else if (p > g->text) {
			char *prev = cp_prev(g, p);
			p = text_hole_delete(g, prev, p - 1, ALLOW_UNDO_QUEUED);
		}
	} else {
		if (c == ASCII_CR)
			c = '\n';
		if (c == '\n')
			undo_queue_commit(g);
		undo_push_insert(g, p, 1, undo);
		p += 1 + stupid_insert(g, p, c);
		if (IS_SHOWMATCH(g) && strchr(")]}", c) != NULL)
			showmatching(g, p - 1);
		if (IS_AUTOINDENT(g) && c == '\n') {
			if (g->newindent < 0) {
				bol = prev_line(g, p);
				len = indent_len(g, bol);
				col = get_column(g, bol + len);

				if (len && col == g->indent_col) {
					memmove(bol + 1, bol, len);
					*bol = '\n';
					return p;
				}
			} else {
				if (p != g->end - 1)
					p--;
				col = g->newindent;
			}

			if (col) {
				g->indent_col = g->cmd_mode != 0 ? col : 0;
				if (IS_EXPANDTAB(g)) {
					ntab = 0;
					nspc = col;
				} else {
					ntab = col / g->tabstop;
					nspc = col % g->tabstop;
				}
				p += text_hole_make(g, p, ntab + nspc);
				undo_push_insert(g, p, ntab + nspc, undo);
				memset(p, '\t', (size_t)ntab);
				p += ntab;
				memset(p, ' ', (size_t)nspc);
				return p + nspc;
			}
		}
	}
	g->indent_col = 0;
	return p;
}

void
init_filename(struct editor *g, char *fn)
{
	char *copy = xstrdup(fn);

	if (g->current_filename == NULL) {
		g->current_filename = copy;
	} else {
		free(g->alt_filename);
		g->alt_filename = copy;
	}
}

void
update_filename(struct editor *g, char *fn)
{
	if (fn == NULL)
		return;

	if (g->current_filename == NULL || strcmp(fn, g->current_filename) != 0) {
		free(g->alt_filename);
		g->alt_filename = g->current_filename;
		g->current_filename = xstrdup(fn);
	}
}

int
init_text_buffer(struct editor *g, char *fn)
{
	int rc;

	free(g->text);
	g->text_size = 10240;
	g->screenbegin = g->dot = g->end = g->text = xzalloc((size_t)g->text_size);

	update_filename(g, fn);
	rc = file_insert(g, fn, g->text, 1);
	if (rc <= 0 || *(g->end - 1) != '\n') {
		char_insert(g, g->end, '\n', NO_UNDO);
	}

	flush_undo_data(g);
	g->modified_count = 0;
	g->last_modified_count = -1;
	g->line_count_cache_stamp = INT_MIN;
	g->refresh_last_modified_count = INT_MIN;
	g->refresh_last_screenbegin = NULL;
	memset(g->mark, 0, sizeof(g->mark));
	undo_load(g, fn);
	return rc;
}

uintptr_t
string_insert(struct editor *g, char *p, const char *s, int undo)
{
	uintptr_t bias;
	int i;

	i = (int)strlen(s);
	undo_push_insert(g, p, i, undo);
	bias = text_hole_make(g, p, i);
	p += bias;
	memcpy(p, s, (size_t)i);

	return bias;
}

int
file_write(struct editor *g, char *fn, char *first, char *last)
{
	int fd;
	int cnt;
	int charcnt;

	if (fn == 0) {
		status_line_bold(g, "No current filename");
		return -2;
	}
	fd = open(fn, (O_WRONLY | O_CREAT), 0666);
	if (fd < 0)
		return -1;
	cnt = last - first + 1;
	charcnt = (int)full_write(fd, first, (size_t)cnt);
	ftruncate(fd, charcnt);
	if (charcnt != cnt)
		charcnt = 0;
	close(fd);

	if (charcnt > 0)
		undo_save(g, fn);

	return charcnt;
}
