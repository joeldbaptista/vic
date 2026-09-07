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
	/*
	 * == Open a gap of `size` bytes at position p ==
	 *
	 * Grows the backing allocation if needed (doubling up to 1 MB, then
	 * 1 MB steps), adjusts all interior pointers (dot, screenbegin, end,
	 * marks, rstart, undo_queue_spos) by the realloc bias, and memmoves
	 * existing content to make room.
	 *
	 * - Returns the bias (new_text - old_text); callers that hold a
	 *   pointer into the old allocation must add this value to it.
	 * - All callers that insert text follow this with a memcpy/assignment
	 *   into the freshly opened bytes.
	 */
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
		if (g->rstart)
			g->rstart += bias;
		if (g->undo_queue_spos)
			g->undo_queue_spos += bias;
		g->text = new_text;
		g->realloc_count++;
	}
	memmove(p + size, p, g->end - size - p);

	return bias;
}

char *
text_hole_delete(struct editor *g, char *p, char *q, int undo)
{
	/*
	 * == Delete the byte range [p, q] (inclusive) from the buffer ==
	 *
	 * Pushes an undo record according to the `undo` flag before removing
	 * content, then memmoves subsequent bytes down to close the gap.
	 * Handles reversed p/q automatically.
	 *
	 * - Returns the new position of what was p after the deletion.
	 * - Callers that hold other pointers into the buffer must re-derive
	 *   them after this call (no bias return; the allocation does not
	 *   change size, only content shifts).
	 */
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
	/*
	 * == Read a file into the buffer at position p ==
	 *
	 * Opens fn, stats it to get the size, calls text_hole_make to reserve
	 * space, then reads the file content in.  On the initial load (initial
	 * = 1) also sets g->readonly_mode when the file is not writable.
	 *
	 * - Returns the byte count inserted, or -1 on failure.
	 * - Errors are reported via status_line_bold and the hole is collapsed.
	 */
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
	/*
	 * == Insert a single byte c at position p ==
	 *
	 * Thin wrapper: opens a 1-byte hole via text_hole_make and writes c
	 * into it.  Returns the realloc bias for callers that need to adjust
	 * their pointers.  Does not record an undo entry.
	 */
	uintptr_t bias;

	bias = text_hole_make(g, p, 1);
	p += bias;
	*p = c;
	return bias;
}

char *
char_insert(struct editor *g, char *p, char c, int undo)
{
	/*
	 * == Insert one character from INSERT/REPLACE mode ==
	 *
	 * Handles all the special cases that arise during interactive typing:
	 * - Ctrl-V: literal next character (bypasses special handling).
	 * - ESC:    commits the undo queue, exits insert mode, strips trailing
	 *           autoindent whitespace from blank lines.
	 * - Ctrl-D: de-indents one tab stop (used in insert mode).
	 * - Tab:    expands to spaces when expandtab is set.
	 * - Backspace/DEL: deletes the preceding character, or (in REPLACE
	 *   mode) pops the undo stack to restore the overwritten byte.
	 * - '\n':   commits the undo queue then auto-indents the new line.
	 * - Other:  inserts the byte, optionally shows a matching bracket.
	 *
	 * - Returns the new value of p (after insertion the pointer shifts).
	 */
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
	/*
	 * == Set the current filename for the first time ==
	 *
	 * If g->current_filename is NULL, sets it to fn.  Otherwise pushes fn
	 * into g->alt_filename (the '#' register), discarding the old
	 * alternate.  Used when opening the very first file.
	 */
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
	/*
	 * == Update the current filename, preserving the previous as alternate ==
	 *
	 * When fn differs from g->current_filename, the old name becomes
	 * g->alt_filename (the '#' register) and fn becomes current.
	 * No-op when fn is NULL or matches the current name.
	 */
	if (fn == NULL)
		return;

	if (g->current_filename == NULL || strcmp(fn, g->current_filename) != 0) {
		free(g->alt_filename);
		g->alt_filename = g->current_filename;
		g->current_filename = xstrdup(fn);
	}
}

static int
stdin_text_insert(struct editor *g, char *p)
{
	/*
	 * == Insert the document slurped from stdin at position p ==
	 *
	 * Mirrors file_insert's bulk path, but the bytes come from the buffer
	 * setup_stdin_file() read before the terminal was reattached: a pipe
	 * has no size to stat and cannot be rewound, so it is already in
	 * memory.
	 *
	 * - Returns the byte count inserted, or 0 when stdin was empty.
	 */
	int size;

	if (!g->stdin_text || g->stdin_len == 0)
		return 0;
	size = (g->stdin_len < INT_MAX ? (int)g->stdin_len : INT_MAX);
	p += text_hole_make(g, p, size);
	memcpy(p, g->stdin_text, (size_t)size);
	undo_push_insert(g, p, size, ALLOW_UNDO);
	return size;
}

static void
strip_terminal_escapes(struct editor *g)
{
	/*
	 * == Remove terminal control markup from the freshly loaded buffer ==
	 *
	 * Pager mode (-p) exists so a formatted document can be piped in.
	 * vic renders no attributes, so the markup its producers emit would
	 * otherwise show up as literal text.  Two encodings are removed, and
	 * they are the only two nroff and groff produce:
	 *
	 *   1. CSI sequences — ESC '[', parameter bytes 0x30-0x3f,
	 *      intermediate bytes 0x20-0x2f, one final byte 0x40-0x7e.
	 *   2. Backspace overstrike — "X\bX" for bold and "_\bX" for
	 *      underline, which groff emits when GROFF_NO_SGR is set.
	 *
	 * Both passes rewrite in place and only ever shrink the text, so no
	 * reallocation happens and g->text stays valid.  Called before the
	 * trailing-newline fixup, which is why that check also guards against
	 * an emptied buffer.
	 */
	char *r;
	char *w;

	r = w = g->text;
	while (r < g->end) {
		if (*r == ASCII_ESC && r + 1 < g->end && r[1] == '[') {
			char *q = r + 2;
			while (q < g->end && *q >= 0x30 && *q <= 0x3f)
				q++;
			while (q < g->end && *q >= 0x20 && *q <= 0x2f)
				q++;
			if (q < g->end && *q >= 0x40 && *q <= 0x7e) {
				r = q + 1;
				continue;
			}
		}
		*w++ = *r++;
	}
	g->end = w;

	r = w = g->text;
	while (r < g->end) {
		if (r + 1 < g->end && r[1] == '\b') {
			r += 2;
			continue;
		}
		*w++ = *r++;
	}
	g->end = w;
}

int
init_text_buffer(struct editor *g, char *fn)
{
	/*
	 * == Initialise the text buffer for a new file ==
	 *
	 * Frees any previous buffer, allocates a fresh 10 KB slab, loads fn
	 * via file_insert (ensuring the buffer always ends with '\n'), flushes
	 * undo history, resets the modified count and cache stamps, and clears
	 * all marks.  Loads the persistent undo sidecar last (undo_load).
	 *
	 * - Returns the byte count from file_insert, or <= 0 on a new/empty
	 *   file.
	 */
	int rc;
	int from_stdin = (fn && strcmp(fn, "-") == 0);

	free(g->text);
	g->text_size = 10240;
	g->screenbegin = g->dot = g->end = g->text = xzalloc((size_t)g->text_size);

	if (from_stdin) {
		/*
		 * "-" is an unnamed buffer: there is no path to write back
		 * to and no undo sidecar to load, so fn is dropped here and
		 * the calls below see NULL.
		 */
		fn = NULL;
		rc = stdin_text_insert(g, g->text);
	} else {
		update_filename(g, fn);
		rc = file_insert(g, fn, g->text, 1);
	}
	if (g->pager_mode)
		strip_terminal_escapes(g);
	if (rc <= 0 || g->end == g->text || *(g->end - 1) != '\n') {
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
	/*
	 * == Insert a NUL-terminated string at position p ==
	 *
	 * Records one undo entry for the entire string, opens a gap via
	 * text_hole_make, and memcpys s into it.
	 *
	 * - Returns the realloc bias; callers must adjust any stale pointers.
	 */
	uintptr_t bias;
	int i;

	i = (int)strlen(s);
	undo_push_insert(g, p, i, undo);
	bias = text_hole_make(g, p, i);
	p += bias;
	memcpy(p, s, (size_t)i);

	return bias;
}

uintptr_t
buffer_replace_range(struct editor *g, char *rs, char *re,
                     const char *new_buf, int new_len)
{
	/*
	 * == Replace [rs, re] (inclusive) with new_buf[0..new_len-1] ==
	 *
	 * Deletes the old range via text_hole_delete, then inserts the new
	 * content via string_insert, chaining both into one undo unit.  If
	 * new_len is 0, only the deletion is performed.
	 *
	 * If [rs, re] spans through the buffer's sentinel byte (re == end-1),
	 * that byte is itself a '\n', and new_buf also ends in '\n', leave
	 * that one byte physically undisturbed rather than deleting and
	 * reinserting it: undo_push() (undo.c) silently trims one byte off a
	 * DEL record that spans the whole live buffer, which would otherwise
	 * lose the final '\n' on undo.  Deleting/inserting one byte short is
	 * content-equivalent only when the byte left behind is the same '\n'
	 * the new content ends with, so *re is checked too: init_text_buffer
	 * appends a sentinel newline, but commands that rewrite the tail (for
	 * example :run base64dec) can leave a non-newline byte there, and
	 * skipping it then duplicates that byte and drops the newline.
	 *
	 * - Returns the realloc bias, like string_insert: the insert can grow
	 *   the text store and move it, so callers holding rs, re or any other
	 *   raw pointer into the buffer must add this value to them.  g->dot,
	 *   g->end and the marks are adjusted by text_hole_make itself.
	 */
	char *tmp;
	uintptr_t bias;
	int deleted = 0;

	if (re == g->end - 1 && *re == '\n' && new_len > 0 &&
	    new_buf[new_len - 1] == '\n') {
		re--;
		new_len--;
	}
	if (rs <= re) {
		text_hole_delete(g, rs, re, ALLOW_UNDO);
		deleted = 1;
	}
	if (new_len <= 0)
		return 0;
	tmp = xmalloc((size_t)new_len + 1);
	memcpy(tmp, new_buf, (size_t)new_len);
	tmp[new_len] = '\0';
	/*
	 * The insert is chained onto the delete so one 'u' reverses both.  It
	 * must NOT be chained when there was no delete to chain onto (the
	 * range was empty, or the sentinel trim above consumed all of it):
	 * apply_undo_stack keeps popping while entries are chained, so an
	 * unanchored CHAIN record makes a single 'u' swallow the previous,
	 * unrelated command as well.
	 */
	bias = string_insert(g, rs, tmp,
	                     deleted ? ALLOW_UNDO_CHAIN : ALLOW_UNDO);
	free(tmp);
	return bias;
}

int
file_write(struct editor *g, char *fn, char *first, char *last)
{
	/*
	 * == Write a buffer range to disk ==
	 *
	 * Opens (or creates) fn, writes [first, last] inclusive, truncates
	 * the file to the written length (handles overwrites of shorter
	 * content), and saves the undo sidecar on success.
	 *
	 * - Returns byte count written, -2 if fn is NULL, -1 on open error,
	 *   0 when the write was short (disk full).
	 */
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
