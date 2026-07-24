/*
 * buffer.c - gap-buffer mutation and file I/O.
 *
 * The text store is a classic gap buffer:
 *
 *   [text .. gap_start)[gap_start .. gap_end)[gap_end .. text+text_size)
 *    ^^^pre-gap content   ^^^zero-filled gap    ^^^post-gap content
 *
 * g->end is always text + text_size (physical allocation end = logical
 * content end, since the after-gap content runs to the end of the slab).
 *
 * Insertion at gap_start is O(1); insertion elsewhere requires one O(n)
 * memmove to relocate the gap, then O(1) fill.  Sequential edits (typing)
 * keep the gap at the cursor, so they stay O(1).
 *
 */
#include "buffer.h"

#include "codepoint.h"
#include "gap.h"
#include "input.h"
#include "line.h"
#include "status.h"
#include "undo.h"

/* ---------------------------------------------------------------------------
 * Internal gap primitives
 * --------------------------------------------------------------------------- */

/*
 * struct side_positions / save_side_positions / restore_side_positions —
 * carry screenbegin, marks, the visual anchor, and the Replace-mode start
 * across a gap-buffer mutation by logical position rather than physical
 * pointer.
 *
 * A gap move can physically relocate content -- a leftward move, for
 * instance, memmove's everything between the target and the old gap_start
 * into the gap's tail end.  Any of these stored pointers that happened to
 * sit in the relocated span is left holding a stale address once that
 * happens: still numerically valid (inside the allocation), but no longer
 * pointing at the byte it used to.
 *
 * An earlier version of this fix tried to patch that up after the fact,
 * by redirecting anything caught resting in [gap_start, gap_end) over to
 * gap_end. That looked right immediately after the gap moved, but both
 * callers advance one gap boundary further right after the move --
 * text_hole_make consumes bytes by advancing gap_start, text_hole_delete
 * by advancing gap_end -- and gap_end alone does not track that: the same
 * physical address it pointed at can mean a different logical position by
 * the time the whole operation is done. That patch quietly broke
 * screenbegin specifically whenever an insert or delete landed at logical
 * position 0, since screenbegin starts there on any file that fits on one
 * screen.
 *
 * Logical position doesn't have this problem -- it is unaffected by
 * *where* content physically lives, which is exactly what makes
 * logical_pos()/phys_ptr() the right tool: capture before the mutation,
 * restore after it has fully settled (gap moved *and* consumed/absorbed),
 * and the round-trip is a no-op for anything the mutation didn't actually
 * touch. Same pattern already used at the call-site level in undo.c,
 * visual.c, and run.c; this just applies it at the two primitives
 * themselves so every caller gets it for free.
 *
 * g->dot is deliberately NOT included: bound_dot's contract is that
 * dot == gap_start is a valid resting position in its own right (the
 * cursor after a sequential insert), and every caller that moves dot
 * across a gap already sets it explicitly afterward. Restoring it here
 * would fight that and defeat the O(1) sequential-insert fast path in
 * gap_move_to's own p == gap_start early return.
 */
struct side_positions {
	int screenbegin;
	int visual_anchor; /* -1 = unset */
	int rstart;         /* -1 = unset */
	int mark[30];        /* -1 = unset; size matches struct editor's mark[] */
};

static void
save_side_positions(struct editor *g, struct side_positions *sp)
{
	int i;

	sp->screenbegin = logical_pos(g, g->screenbegin);
	sp->visual_anchor = g->visual_anchor ? logical_pos(g, g->visual_anchor) : -1;
	sp->rstart = g->rstart ? logical_pos(g, g->rstart) : -1;
	for (i = 0; i < (int)ARRAY_SIZE(g->mark); i++)
		sp->mark[i] = g->mark[i] ? logical_pos(g, g->mark[i]) : -1;
}

static void
restore_side_positions(struct editor *g, const struct side_positions *sp)
{
	int i;

	g->screenbegin = phys_ptr(g, sp->screenbegin);
	g->visual_anchor = sp->visual_anchor >= 0 ? phys_ptr(g, sp->visual_anchor) : NULL;
	g->rstart = sp->rstart >= 0 ? phys_ptr(g, sp->rstart) : NULL;
	for (i = 0; i < (int)ARRAY_SIZE(g->mark); i++)
		g->mark[i] = sp->mark[i] >= 0 ? phys_ptr(g, sp->mark[i]) : NULL;
}

/*
 * gap_move_to — relocate the gap so that gap_start == p.
 *
 * p must be a valid content pointer (< gap_start or >= gap_end).
 * After the call all other content pointers remain valid.
 */
static void
gap_move_to(struct editor *g, char *p)
{
	int shift;
	int gap_size = (int)(g->gap_end - g->gap_start);

	if (p == g->gap_start)
		return;

	if (p < g->gap_start) {
		/*
		 * Move gap left: slide [p, gap_start) into the gap's right end.
		 * The gap keeps its size; only its position changes, so the
		 * zeroed span must be exactly gap_size, not `shift'.  When the
		 * move distance exceeds the gap size (e.g. editing near the
		 * start of a large file whose gap still sits at the tail),
		 * zeroing `shift' bytes instead would stomp on the real
		 * content that the memmove just relocated into the tail of
		 * that same range.
		 */
		shift = (int)(g->gap_start - p);
		memmove(g->gap_end - shift, p, (size_t)shift);
		memset(p, 0, (size_t)gap_size);
		g->gap_end -= shift;
		g->gap_start = p;
	} else {
		/* p >= gap_end: move gap right; slide [gap_end, p) into the
		 * gap's left end.  Same fixed-size zeroing rationale as above. */
		shift = (int)(p - g->gap_end);
		memmove(g->gap_start, g->gap_end, (size_t)shift);
		memset(p - gap_size, 0, (size_t)gap_size);
		g->gap_start += shift;
		g->gap_end = p;
	}
}

/*
 * gap_grow — grow the allocation so the gap has room for at least `needed'
 * additional bytes.  Adjusts all interior pointers (dot, screenbegin, marks,
 * gap boundaries, and the caller's *pp) for both the realloc base change and
 * the post-gap content relocation.  Returns the total shift applied to *pp
 * (the realloc bias, plus the post-gap relocation delta if *pp itself was
 * on the far side of the gap) — i.e. exactly (*pp after) - (*pp before).
 */
static uintptr_t
gap_grow(struct editor *g, char **pp, int needed)
{
	char *new_text;
	int new_size;
	int after_size;
	int delta;
	uintptr_t bias;
	int pp_after_gap;
	int i;

	after_size = (int)(g->text + g->text_size - g->gap_end);

	new_size = g->text_size;
	if (new_size == 0)
		new_size = 4096;
	while (new_size - buf_content_size(g) < needed) {
		if (new_size < 1024 * 1024)
			new_size *= 2;
		else
			new_size += 1024 * 1024;
	}

	new_text = xrealloc(g->text, (size_t)new_size);
	bias = (uintptr_t)(new_text - g->text);
	delta = new_size - g->text_size;

	/* Bias-adjust all interior pointers. */
	g->dot += bias;
	g->screenbegin += bias;
	if (g->visual_anchor)
		g->visual_anchor += bias;
	if (g->rstart)
		g->rstart += bias;
	/* undo_queue_spos is now a logical int, no adjustment needed */
	g->gap_start += bias;
	g->gap_end += bias;
	for (i = 0; i < (int)ARRAY_SIZE(g->mark); i++)
		if (g->mark[i])
			g->mark[i] += bias;
	*pp += bias;
	g->text = new_text;

	/* Post-gap pointers also shift by delta (content moves to end). */
	if (g->dot >= g->gap_end)
		g->dot += delta;
	if (g->screenbegin >= g->gap_end)
		g->screenbegin += delta;
	if (g->visual_anchor && g->visual_anchor >= g->gap_end)
		g->visual_anchor += delta;
	if (g->rstart && g->rstart >= g->gap_end)
		g->rstart += delta;
	/* undo_queue_spos is now a logical int, no adjustment needed */
	for (i = 0; i < (int)ARRAY_SIZE(g->mark); i++)
		if (g->mark[i] && g->mark[i] >= g->gap_end)
			g->mark[i] += delta;
	pp_after_gap = (*pp >= g->gap_end);
	if (pp_after_gap)
		*pp += delta;

	/* Move after-gap content to the end of the new (larger) allocation. */
	char *new_gap_end = g->text + new_size - after_size;
	memmove(new_gap_end, g->gap_end, (size_t)after_size);
	memset(g->gap_end, 0, (size_t)(new_gap_end - g->gap_end));

	g->gap_end = new_gap_end;
	g->text_size = new_size;
	g->end = g->text + new_size;

	/*
	 * The caller (text_hole_make) uses this return value as the amount
	 * to add to any pointer that was equal to the original *pp in order
	 * to track it through the grow.  That must match the total shift
	 * *pp itself just received above: the realloc bias, plus `delta' if
	 * *pp lay on the far (post-gap) side of the gap.  Returning just the
	 * realloc bias here silently drops the `delta' term for post-gap
	 * insertion points, which corrupts the caller's tracked pointer.
	 */
	return pp_after_gap ? bias + (uintptr_t)delta : bias;
}

/* ---------------------------------------------------------------------------
 * Public gap-buffer API
 * --------------------------------------------------------------------------- */

/*
 * gap_flush — move the gap to the end of content, making the buffer
 * temporarily contiguous.  Called before operations that require a flat
 * byte array (regex search, case-change UNDO_SWAP).
 */
void
gap_flush(struct editor *g)
{
	int after_size = (int)(g->end - g->gap_end);

	if (after_size > 0) {
		memmove(g->gap_start, g->gap_end, (size_t)after_size);
		memset(g->gap_start + after_size, 0,
		       (size_t)(g->gap_end - g->gap_start));
	}
	g->gap_start += after_size;
	g->gap_end = g->end;
}

uintptr_t
text_hole_make(struct editor *g, char *p, int size)
{
	/*
	 * == Open a gap of `size' bytes at position p ==
	 *
	 * Grows the backing allocation if the gap is too small, then moves
	 * the gap to p and consumes `size' bytes from it.  After the call,
	 * p points to the first of the newly reserved bytes (which are zero).
	 * Callers fill those bytes and must not advance gap_start themselves.
	 *
	 * Returns the realloc bias; callers that hold a pointer into the old
	 * allocation must add this value to it.
	 */
	uintptr_t bias = 0;
	struct side_positions sp;

	if (size <= 0)
		return bias;

	save_side_positions(g, &sp);

	if ((int)(g->gap_end - g->gap_start) < size)
		bias = gap_grow(g, &p, size);

	gap_move_to(g, p);

	/*
	 * gap_move_to sets gap_start == p only when the gap moved left (or
	 * was already there).  When it moved right to reach p, the reserved
	 * bytes land at gap_start instead, which is (gap size) behind p.
	 * Fold that shift into the returned bias so p + bias always points
	 * at the newly reserved region.
	 */
	bias += (uintptr_t)(g->gap_start - p);

	/* Consume `size' bytes from the gap (they become pre-gap content). */
	g->gap_start += size;

	restore_side_positions(g, &sp);

	return bias;
}

char *
text_hole_delete(struct editor *g, char *p, char *q, int undo)
{
	/*
	 * == Delete the content range [p, q] (inclusive) from the buffer ==
	 *
	 * Moves the gap to p, saves the bytes for undo, then extends the gap
	 * over the deleted content.  Returns the first content byte after the
	 * deletion point (= new gap_end), clamped to a valid content position.
	 */
	int lp, lq, hole_size;
	char *result;
	struct side_positions sp;

	save_side_positions(g, &sp);

	/* Normalise to logical offsets so gap movement doesn't invalidate q. */
	lp = logical_pos(g, p);
	lq = logical_pos(g, q);
	if (lq < lp) {
		int tmp = lp;
		lp = lq;
		lq = tmp;
	}
	hole_size = lq - lp + 1;

	/* Move gap to deletion start so the bytes to delete are contiguous at
	 * gap_end, making the undo copy straightforward. */
	gap_move_to(g, phys_ptr(g, lp));
	/* bytes to delete: [gap_end, gap_end + hole_size) */

	g->modified_count--;
	if (g->gap_end >= g->end || hole_size <= 0)
		goto thd0;
	if (hole_size > (int)(g->end - g->gap_end))
		hole_size = (int)(g->end - g->gap_end);
	g->modified_count++;

	switch (undo) {
	case NO_UNDO:
		break;
	case ALLOW_UNDO:
		undo_push(g, g->gap_end, (unsigned)hole_size, UNDO_DEL);
		break;
	case ALLOW_UNDO_CHAIN:
		undo_push(g, g->gap_end, (unsigned)hole_size, UNDO_DEL_CHAIN);
		break;
	case ALLOW_UNDO_QUEUED:
		undo_push(g, g->gap_end, (unsigned)hole_size, UNDO_DEL_QUEUED);
		break;
	}

	/* Absorb deleted bytes into gap. */
	memset(g->gap_end, 0, (size_t)hole_size);
	g->gap_end += hole_size;

thd0:
	restore_side_positions(g, &sp);
	result = g->gap_end;
	if (result >= g->end)
		result = g->gap_start > g->text ? g->gap_start - 1 : g->text;
	if (g->end <= g->text)
		result = g->gap_start = g->gap_end = g->text;
	return result;
}

int
file_insert(struct editor *g, const char *fn, char *p, int initial)
{
	/*
	 * == Read a file into the buffer at position p ==
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
	/* text_hole_make consumed `size' bytes from the gap; p now points at
	 * the reserved (zero-filled) region in pre-gap content.  Read the
	 * file directly into it. */
	cnt = (int)full_read(fd, p, (size_t)size);
	if (cnt < 0) {
		status_line_bold_errno(g, fn);
		/* Reclaim the space: re-absorb via delete. */
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
		/*
		 * logical_pos, not a raw pointer compare: p can rest at gap_end
		 * (e.g. right after a delete that landed dot there) while still
		 * being logically at position 0, where g->dot > g->text would
		 * wrongly read true.  Step back via buf_prev rather than p--
		 * for the same reason -- p-1 across a gap boundary lands inside
		 * the gap's zeroed interior, not the preceding real byte.
		 */
		if (logical_pos(g, p) > 0 && buf_char_before(g, p) != '\n')
			p = buf_prev(g, p);
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
		} else if (logical_pos(g, p) > 0) {
			char *prev = cp_prev(g, p);
			/*
			 * Guard is logical_pos, not a raw pointer compare, for the
			 * same reason as the ESC handler above: p can rest at
			 * gap_end while logically at position 0, which a physical
			 * p > g->text compare would misread as "there's a
			 * preceding character," letting this delete into content
			 * before the actual start of the buffer.
			 *
			 * Deletion range end must be the last byte of prev's own
			 * codepoint (cp_end(prev) - 1), not p - 1: p can legitimately
			 * rest at gap_end (e.g. right after a delete that landed
			 * dot there), in which case p - 1 is one byte into the
			 * gap's zeroed interior rather than adjacent real content.
			 */
			if (*prev == '\n')
				g->line_count_cache_stamp = INT_MIN;
			p = text_hole_delete(g, prev, cp_end(g, prev) - 1,
			                     ALLOW_UNDO_QUEUED);
		}
	} else {
		if (c == ASCII_CR)
			c = '\n';
		if (c == '\n') {
			undo_queue_commit(g);
			/*
			 * Queued inserts (the common case for interactive typing)
			 * defer bumping modified_count until the queue commits, but
			 * total_line_count()'s cache is keyed on modified_count --
			 * so a '\n' typed as the first byte of a fresh queue (e.g.
			 * Enter right after entering INSERT mode) already changed
			 * the real line count while the cached total, and anything
			 * that reads it (the status line, screen.c's scroll clamp),
			 * still reflects the pre-edit count until some later event
			 * happens to commit the queue. Force the recompute now,
			 * synchronously with the byte that actually changes the
			 * count, independent of when the undo entry lands.
			 */
			g->line_count_cache_stamp = INT_MIN;
		}
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
				/*
				 * Compare logical positions, not raw pointers: right
				 * after the insert above, p sits at gap_start while
				 * g->end - 1 is physically on the far side of the gap.
				 * The two can be the same content position (this is the
				 * buffer's last line, so p is already exactly where the
				 * indent belongs) while being different physical
				 * pointers, and a raw != would wrongly back p up onto
				 * the line above instead of the fresh blank line.
				 */
				if (logical_pos(g, p) != logical_pos(g, g->end - 1))
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
	 */
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
	/*
	 * == Initialise the text buffer for a new file ==
	 *
	 * Allocates a fresh slab with the gap occupying the whole slab initially.
	 * file_insert fills content and the gap shrinks accordingly.
	 */
	int rc;

	free(g->text);
	g->text_size = 10240;
	g->text = xzalloc((size_t)g->text_size);
	/* Gap spans the whole allocation; content is empty. */
	g->gap_start = g->text;
	g->gap_end = g->text + g->text_size;
	g->end = g->text + g->text_size;
	g->screenbegin = g->dot = g->text;

	update_filename(g, fn);
	rc = file_insert(g, fn, g->text, 1);
	if (rc <= 0 || buf_char_before(g, buf_end(g)) != '\n') {
		char_insert(g, g->end, '\n', NO_UNDO);
	}

	flush_undo_data(g);
	g->modified_count = 0;
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

int
file_write(struct editor *g, char *fn, char *first, char *last)
{
	/*
	 * == Write a buffer range to disk ==
	 *
	 * Writes the content in [first, last] inclusive.  The range may span
	 * the gap, in which case two write calls are issued.
	 *
	 * Returns byte count written, -2 if fn is NULL, -1 on open error,
	 * 0 when the write was short.
	 */
	int fd;
	int cnt;
	int charcnt = 0;

	if (fn == 0) {
		status_line_bold(g, "No current filename");
		return -2;
	}
	fd = open(fn, (O_WRONLY | O_CREAT), 0666);
	if (fd < 0)
		return -1;

	/* Segment 1: [first, min(last, gap_start-1)] */
	if (first < g->gap_start) {
		char *seg_end = (last < g->gap_start) ? last : g->gap_start - 1;
		cnt = (int)(seg_end - first + 1);
		if (cnt > 0) {
			int w = (int)full_write(fd, first, (size_t)cnt);
			if (w != cnt)
				goto short_write;
			charcnt += w;
		}
	}

	/* Segment 2: [max(first, gap_end), last] */
	{
		char *seg_start = (first >= g->gap_end) ? first : g->gap_end;
		if (seg_start <= last) {
			cnt = (int)(last - seg_start + 1);
			int w = (int)full_write(fd, seg_start, (size_t)cnt);
			if (w != cnt)
				goto short_write;
			charcnt += w;
		}
	}

	ftruncate(fd, charcnt);
	close(fd);
	if (charcnt > 0)
		undo_save(g, fn);
	return charcnt;

short_write:
	ftruncate(fd, charcnt);
	close(fd);
	return 0;
}
