/*
 * buffer.c - text mutation and file I/O over the piece tree.
 *
 * Every change to the text goes through this file.  text_insert_bytes
 * inserts raw bytes at an offset and text_hole_delete removes a range;
 * both are thin layers over the piece tree (pt.c), so neither moves any
 * other byte in memory.  char_insert is the single-character insert path
 * used by insert/replace mode; it handles tab expansion, auto-indent,
 * backspace, showmatch, and undo queuing.  string_insert and file_insert
 * are bulk variants.
 *
 * Positions are int byte offsets (see text.h).  The old slab code
 * returned a realloc "bias" from its insert functions; offsets never move,
 * so those functions now always return 0 and old call sites that add the
 * bias keep working.
 */
#include "buffer.h"

#include "codepoint.h"
#include "input.h"
#include "line.h"
#include "status.h"
#include "text.h"
#include "undo.h"

int
text_insert_bytes(struct editor *g, int p, const char *s, int n)
{
	/*
	 * == Insert n bytes of s at offset p without an undo record ==
	 *
	 * The replacement for text_hole_make followed by a memcpy into the
	 * hole.  p is clamped into [0, text_len()].  dot, screenbegin and
	 * the marks are offsets and stay where they are, exactly as the old
	 * slab pointers stayed at their addresses.
	 *
	 * - Returns 0, the old "realloc bias", so p never needs adjusting.
	 */
	if (n <= 0)
		return 0;
	if (p < 0)
		p = 0;
	if (p > text_len(g))
		p = text_len(g);
	ptinsert(g->pt, (size_t)p, s, (size_t)n);
	return 0;
}

int
text_hole_delete(struct editor *g, int p, int q, int undo)
{
	/*
	 * == Delete the byte range [p, q] (inclusive) from the buffer ==
	 *
	 * Pushes an undo record according to the `undo` flag before removing
	 * content.  Handles reversed p/q automatically.
	 *
	 * - Returns the new position of what was p after the deletion.
	 */
	int src;
	int dest;
	int hole_size;
	int end;

	src = q + 1;
	dest = p;
	if (q < p) {
		src = p + 1;
		dest = q;
	}
	hole_size = q - p + 1;
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
	end = text_len(g);
	g->modified_count--;
	if (src < 0 || src > end)
		goto thd0;
	if (dest < 0 || dest >= end)
		goto thd0;
	g->modified_count++;
	ptdelete(g->pt, (size_t)dest, (size_t)(src - dest));
	end = text_len(g);
	if (dest >= end)
		dest = end - 1;
	if (end <= 0)
		dest = 0;
thd0:
	return dest;
}

int
file_insert(struct editor *g, const char *fn, int p, int initial)
{
	/*
	 * == Read a file into the buffer at position p ==
	 *
	 * Opens fn, stats it to get the size, reads the content into memory,
	 * then inserts it.  On the initial load (initial = 1) also sets
	 * g->readonly_mode when the file is not writable.
	 *
	 * - Returns the byte count inserted, or -1 on failure.
	 * - Errors are reported via status_line_bold.
	 */
	int cnt = -1;
	int fd;
	int size;
	char *buf;
	struct stat statbuf;

	if (p < 0)
		p = 0;
	if (p > text_len(g))
		p = text_len(g);

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
	buf = xmalloc((size_t)size + 1);
	cnt = (int)full_read(fd, buf, (size_t)size);
	if (cnt < 0) {
		status_line_bold_errno(g, fn);
	} else if (cnt < size) {
		text_insert_bytes(g, p, buf, cnt);
		status_line_bold(g, "can't read '%s'", fn);
	} else {
		undo_push_insert(g, p, size, ALLOW_UNDO);
		text_insert_bytes(g, p, buf, size);
	}
	free(buf);
fi:
	close(fd);

	if (initial && ((access(fn, W_OK) < 0) ||
	                !(statbuf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))) {
		SET_READONLY_FILE(g->readonly_mode);
	}
	return cnt;
}

static int
stupid_insert(struct editor *g, int p, char c)
{
	/*
	 * == Insert a single byte c at position p ==
	 *
	 * Does not record an undo entry.  Returns 0 (the old realloc bias).
	 */
	return text_insert_bytes(g, p, &c, 1);
}

int
char_insert(struct editor *g, int p, char c, int undo)
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
	 * - Returns the new value of p (after insertion the offset shifts).
	 */
	size_t len;
	int col;
	int ntab;
	int nspc;
	int bol;
	char *ind;

	bol = begin_line(g, p);

	if (c == ASCII_CTRL_V) {
		p += stupid_insert(g, p, '^');
		c = (char)get_one_char(g);
		text_set_char(g, p, c);
		undo_push_insert(g, p, 1, undo);
		p++;
	} else if (c == ASCII_ESC) {
		g->cmd_mode = 0;
		undo_queue_commit(g);
		g->cmdcnt = 0;
		reset_ydreg(g);
		g->last_status_cksum = 0;
		if ((g->dot > 0) && (text_char(g, p - 1) != '\n'))
			p--;
		if (IS_AUTOINDENT(g)) {
			len = indent_len(g, bol);
			col = get_column(g, bol + (int)len);
			if (len && col == g->indent_col &&
			    text_char(g, bol + (int)len) == '\n') {
				text_hole_delete(g, bol, bol + (int)len - 1, undo);
				p = bol;
			}
		}
	} else if (c == ASCII_CTRL_D) {
		int r = bol + (int)indent_len(g, bol);
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
		} else if (p > 0) {
			int prev = cp_prev(g, p);
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
				col = get_column(g, bol + (int)len);

				if (len && col == g->indent_col) {
					/*
					 * Move the indent down to the new line:
					 * the old code shifted [bol, bol+len)
					 * right by one over the new '\n' and put
					 * the '\n' at bol.  Same bytes, no undo.
					 */
					ptdelete(g->pt, (size_t)bol + len, 1);
					ptinsert(g->pt, (size_t)bol, "\n", 1);
					return p;
				}
			} else {
				if (p != text_len(g) - 1)
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
				undo_push_insert(g, p, ntab + nspc, undo);
				ind = xmalloc((size_t)(ntab + nspc) + 1);
				memset(ind, '\t', (size_t)ntab);
				memset(ind + ntab, ' ', (size_t)nspc);
				text_insert_bytes(g, p, ind, ntab + nspc);
				free(ind);
				return p + ntab + nspc;
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
stdin_text_insert(struct editor *g, int p)
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
	undo_push_insert(g, p, size, ALLOW_UNDO);
	text_insert_bytes(g, p, g->stdin_text, size);
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
	 * The text is copied out, filtered in place, and loaded back as a
	 * fresh tree.  Called before the trailing-newline fixup, which is why
	 * that check also guards against an emptied buffer.
	 */
	char *s;
	char *r;
	char *w;
	char *e;

	s = text_copy(g, 0, text_len(g));
	e = s + text_len(g);
	r = w = s;
	while (r < e) {
		if (*r == ASCII_ESC && r + 1 < e && r[1] == '[') {
			char *q = r + 2;
			while (q < e && *q >= 0x30 && *q <= 0x3f)
				q++;
			while (q < e && *q >= 0x20 && *q <= 0x2f)
				q++;
			if (q < e && *q >= 0x40 && *q <= 0x7e) {
				r = q + 1;
				continue;
			}
		}
		*w++ = *r++;
	}
	e = w;

	r = w = s;
	while (r < e) {
		if (r + 1 < e && r[1] == '\b') {
			r += 2;
			continue;
		}
		*w++ = *r++;
	}
	ptfree(g->pt);
	g->pt = ptnew(s, (size_t)(w - s));
}

int
init_text_buffer(struct editor *g, char *fn)
{
	/*
	 * == Initialise the text buffer for a new file ==
	 *
	 * Frees any previous text, starts an empty piece tree, loads fn via
	 * file_insert (ensuring the buffer always ends with '\n'), flushes
	 * undo history, resets the modified count and cache stamps, and
	 * clears all marks.  Loads the persistent undo sidecar last
	 * (undo_load).
	 *
	 * - Returns the byte count from file_insert, or <= 0 on a new/empty
	 *   file.
	 */
	int rc;
	int i;
	int from_stdin = (fn && strcmp(fn, "-") == 0);

	ptfree(g->pt);
	g->pt = ptnew(NULL, 0);
	g->screenbegin = g->dot = 0;

	if (from_stdin) {
		/*
		 * "-" is an unnamed buffer: there is no path to write back
		 * to and no undo sidecar to load, so fn is dropped here and
		 * the calls below see NULL.
		 */
		fn = NULL;
		rc = stdin_text_insert(g, 0);
	} else {
		update_filename(g, fn);
		rc = file_insert(g, fn, 0, 1);
	}
	if (g->pager_mode)
		strip_terminal_escapes(g);
	if (rc <= 0 || text_len(g) == 0 ||
	    text_char(g, text_len(g) - 1) != '\n') {
		char_insert(g, text_len(g), '\n', NO_UNDO);
	}

	flush_undo_data(g);
	g->modified_count = 0;
	g->last_modified_count = -1;
	g->line_count_cache_stamp = INT_MIN;
	g->refresh_last_modified_count = INT_MIN;
	g->refresh_last_screenbegin = -1;
	for (i = 0; i < (int)ARRAY_SIZE(g->mark); i++)
		g->mark[i] = -1;
	undo_load(g, fn);
	return rc;
}

int
string_insert(struct editor *g, int p, const char *s, int undo)
{
	/*
	 * == Insert a NUL-terminated string at position p ==
	 *
	 * Records one undo entry for the entire string, then inserts it.
	 *
	 * - Returns 0, the old realloc bias.
	 */
	int i;

	i = (int)strlen(s);
	undo_push_insert(g, p, i, undo);
	return text_insert_bytes(g, p, s, i);
}

int
buffer_replace_range(struct editor *g, int rs, int re,
                     const char *new_buf, int new_len)
{
	/*
	 * == Replace [rs, re] (inclusive) with new_buf[0..new_len-1] ==
	 *
	 * Deletes the old range via text_hole_delete, then inserts the new
	 * content, chaining both into one undo unit.  If new_len is 0, only
	 * the deletion is performed.
	 *
	 * If [rs, re] spans through the buffer's sentinel byte (re == end-1),
	 * that byte is itself a '\n', and new_buf also ends in '\n', leave
	 * that one byte physically undisturbed rather than deleting and
	 * reinserting it: undo_push() (undo.c) silently trims one byte off a
	 * DEL record that spans the whole live buffer, which would otherwise
	 * lose the final '\n' on undo.  Deleting/inserting one byte short is
	 * content-equivalent only when the byte left behind is the same '\n'
	 * the new content ends with, so the byte at re is checked too:
	 * init_text_buffer appends a sentinel newline, but commands that
	 * rewrite the tail (for example :run base64dec) can leave a
	 * non-newline byte there, and skipping it then duplicates that byte
	 * and drops the newline.
	 *
	 * - Returns 0, the old realloc bias.
	 */
	int deleted = 0;

	if (re == text_len(g) - 1 && text_char(g, re) == '\n' &&
	    new_len > 0 && new_buf[new_len - 1] == '\n') {
		re--;
		new_len--;
	}
	if (rs <= re) {
		text_hole_delete(g, rs, re, ALLOW_UNDO);
		deleted = 1;
	}
	if (new_len <= 0)
		return 0;
	/*
	 * The insert is chained onto the delete so one 'u' reverses both.  It
	 * must NOT be chained when there was no delete to chain onto (the
	 * range was empty, or the sentinel trim above consumed all of it):
	 * apply_undo_stack keeps popping while entries are chained, so an
	 * unanchored CHAIN record makes a single 'u' swallow the previous,
	 * unrelated command as well.
	 */
	undo_push_insert(g, rs, new_len,
	                 deleted ? ALLOW_UNDO_CHAIN : ALLOW_UNDO);
	text_insert_bytes(g, rs, new_buf, new_len);
	return 0;
}

int
file_write(struct editor *g, char *fn, int first, int last)
{
	/*
	 * == Write a buffer range to disk ==
	 *
	 * Opens (or creates) fn, writes [first, last] inclusive piece by
	 * piece, truncates the file to the written length (handles
	 * overwrites of shorter content), and saves the undo sidecar on
	 * success.
	 *
	 * - Returns byte count written, -2 if fn is NULL, -1 on open error,
	 *   0 when the write was short (disk full).
	 */
	int fd;
	int cnt;
	int charcnt;
	int p;
	const char *s;
	size_t n;
	ssize_t w;

	if (fn == 0) {
		status_line_bold(g, "No current filename");
		return -2;
	}
	fd = open(fn, (O_WRONLY | O_CREAT), 0666);
	if (fd < 0)
		return -1;
	cnt = last - first + 1;
	charcnt = 0;
	for (p = first; charcnt < cnt; p += (int)n) {
		s = ptchunk(g->pt, (size_t)p, &n);
		if (s == NULL)
			break;
		if ((int)n > cnt - charcnt)
			n = (size_t)(cnt - charcnt);
		w = full_write(fd, s, n);
		if (w < 0)
			break;
		charcnt += (int)w;
		if ((size_t)w != n)
			break;
	}
	ftruncate(fd, charcnt);
	if (charcnt != cnt)
		charcnt = 0;
	close(fd);

	if (charcnt > 0)
		undo_save(g, fn);

	return charcnt;
}
