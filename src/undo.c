/*
 * undo.c - undo/redo stack.
 *
 * Each undo entry records an operation type (UNDO_INS or UNDO_DEL), a copy
 * of the affected bytes, and the buffer offset, so it can be mechanically
 * reversed.
 *
 * undo_push()        — record a delete (called by buffer.c before removal)
 * undo_push_insert() — record an insert (called after insertion)
 * undo_queue_commit()— flush the small in-progress queue into a stack entry;
 *                      call this at command boundaries to bundle rapid
 *                      keystrokes into one undoable unit
 * undo_pop()         — undo one entry (u)
 * undo_with_redo()   — undo and push onto the redo stack
 * redo_pop()         — redo one entry (Ctrl-R)
 * flush_undo_data()  — discard all undo history (on file load)
 *
 */
#include "undo.h"

#include "buffer.h"
#include "screen.h"
#include "status.h"

static void
push_undo_entry(struct undo_object **stack_tail,
                struct undo_object *undo_entry)
{
	/*
	 * == Push one entry onto a LIFO undo/redo stack ==
	 */
	undo_entry->prev = *stack_tail;
	*stack_tail = undo_entry;
}

void
undo_queue_commit(struct editor *g)
{
	/*
	 * == Flush the in-progress undo queue into a stack entry ==
	 *
	 * The queue accumulates single-byte inserts/deletes during rapid
	 * typing so they can be bundled into one undoable unit.  Call this at
	 * command boundaries (before any motion or at ESC) to finalise the
	 * bundle.  No-op when the queue is empty.
	 */
	if (g->undo_q > 0) {
		undo_push(g, g->undo_queue + UNDO_QUEUE_MAX - g->undo_q,
		          g->undo_q, (g->undo_queue_state | UNDO_USE_SPOS));
		g->undo_queue_state = UNDO_EMPTY;
		g->undo_q = 0;
	}
}

void
flush_undo_data(struct editor *g)
{
	/*
	 * == Discard all undo and redo history ==
	 *
	 * Frees every entry on both stacks.  Called when a new file is loaded
	 * so undo does not leak across files.
	 */
	struct undo_object *undo_entry;

	while (g->undo_stack_tail) {
		undo_entry = g->undo_stack_tail;
		g->undo_stack_tail = undo_entry->prev;
		free(undo_entry);
	}
	while (g->redo_stack_tail) {
		undo_entry = g->redo_stack_tail;
		g->redo_stack_tail = undo_entry->prev;
		free(undo_entry);
	}
}

void
undo_push(struct editor *g, char *src, unsigned length, int u_type)
{
	/*
	 * == Record a buffer mutation for later undo ==
	 *
	 * For queued types (UNDO_DEL_QUEUED, UNDO_INS_QUEUED), accumulates
	 * bytes in the small in-memory queue and flushes it when full or when
	 * the operation type changes direction.
	 *
	 * For direct types (UNDO_DEL, UNDO_INS, UNDO_SWAP, and _CHAIN
	 * variants), allocates an undo_object and pushes it onto the stack.
	 * _CHAIN variants link adjacent operations so u applies them all as
	 * one unit.
	 *
	 * - Clears the redo stack on every direct push (a new edit invalidates
	 *   the redo history).
	 */
	struct undo_object *undo_entry;
	int use_spos = u_type & UNDO_USE_SPOS;

	switch (u_type) {
	case UNDO_EMPTY:
		return;
	case UNDO_DEL_QUEUED:
		if (length != 1)
			return;
		switch (g->undo_queue_state) {
		case UNDO_EMPTY:
			g->undo_queue_state = UNDO_DEL;
			/* fall through */
		case UNDO_DEL:
			g->undo_queue_spos = src;
			g->undo_q++;
			g->undo_queue[UNDO_QUEUE_MAX - g->undo_q] = *src;
			if (g->undo_q == UNDO_QUEUE_MAX)
				undo_queue_commit(g);
			return;
		case UNDO_INS:
			undo_queue_commit(g);
			undo_push(g, src, length, UNDO_DEL_QUEUED);
			return;
		}
		break;
	case UNDO_INS_QUEUED:
		if (length < 1)
			return;
		switch (g->undo_queue_state) {
		case UNDO_EMPTY:
			g->undo_queue_state = UNDO_INS;
			g->undo_queue_spos = src;
			/* fall through */
		case UNDO_INS:
			while (length--) {
				g->undo_q++;
				if (g->undo_q == UNDO_QUEUE_MAX)
					undo_queue_commit(g);
			}
			return;
		case UNDO_DEL:
			undo_queue_commit(g);
			undo_push(g, src, length, UNDO_INS_QUEUED);
			return;
		}
		break;
	}

	u_type &= ~UNDO_USE_SPOS;
	while (g->redo_stack_tail) {
		struct undo_object *redo_entry = g->redo_stack_tail;
		g->redo_stack_tail = redo_entry->prev;
		free(redo_entry);
	}

	if (u_type == UNDO_SWAP) {
		/* Save the exact original bytes without truncation. */
		undo_entry = xzalloc(offsetof(struct undo_object, undo_text) + length);
		memcpy(undo_entry->undo_text, src, length);
	} else if (u_type == UNDO_DEL || u_type == UNDO_DEL_CHAIN) {
		if ((g->text + length) == g->end)
			length--;
		undo_entry = xzalloc(offsetof(struct undo_object, undo_text) + length);
		memcpy(undo_entry->undo_text, src, length);
	} else {
		undo_entry = xzalloc(sizeof(*undo_entry));
	}
	undo_entry->length = length;
	if (use_spos)
		undo_entry->start = g->undo_queue_spos - g->text;
	else
		undo_entry->start = src - g->text;
	undo_entry->u_type = u_type;

	push_undo_entry(&g->undo_stack_tail, undo_entry);
	g->modified_count++;
}

void
undo_push_insert(struct editor *g, char *p, int len, int undo)
{
	/*
	 * == Record an insertion for undo, routing to the right entry type ==
	 *
	 * Translates the ALLOW_UNDO / ALLOW_UNDO_CHAIN / ALLOW_UNDO_QUEUED
	 * flag into the appropriate UNDO_INS / UNDO_INS_CHAIN / UNDO_INS_QUEUED
	 * call to undo_push.  No-op for NO_UNDO.
	 */
	switch (undo) {
	case ALLOW_UNDO:
		undo_push(g, p, len, UNDO_INS);
		break;
	case ALLOW_UNDO_CHAIN:
		undo_push(g, p, len, UNDO_INS_CHAIN);
		break;
	case ALLOW_UNDO_QUEUED:
		undo_push(g, p, len, UNDO_INS_QUEUED);
		break;
	}
}

static struct undo_object *
new_undo_entry(uint8_t u_type, int start, int length,
               const char *text)
{
	/*
	 * == Allocate an inverse undo_object to push onto the redo stack ==
	 *
	 * For DEL types, copies `length` bytes from `text` (the bytes being
	 * re-inserted on redo).  For INS types, allocates just the header
	 * (no payload needed — the bytes are already in the buffer).
	 */
	struct undo_object *undo_entry;

	if (u_type == UNDO_DEL || u_type == UNDO_DEL_CHAIN) {
		undo_entry =
		    xzalloc(offsetof(struct undo_object, undo_text) + (size_t)length);
		if (length > 0 && text)
			memcpy(undo_entry->undo_text, text, (size_t)length);
	} else {
		undo_entry = xzalloc(sizeof(*undo_entry));
	}
	undo_entry->u_type = u_type;
	undo_entry->start = start;
	undo_entry->length = length;
	return undo_entry;
}

static void
apply_undo_stack(struct editor *g, struct undo_object **from_stack,
                 struct undo_object **to_stack,
                 const char *empty_msg, const char *op_label,
                 int dir)
{
	/*
	 * == Replay entries from from_stack, pushing inverses onto to_stack ==
	 *
	 * Shared engine for undo, undo-with-redo, and redo.  Pops and applies
	 * entries in a chain loop:
	 * - UNDO_DEL/DEL_CHAIN: restores bytes via text_hole_make + memcpy.
	 * - UNDO_INS/INS_CHAIN: removes bytes via text_hole_delete.
	 * - UNDO_SWAP: replaces bytes in-place with a pure memcpy.
	 *
	 * The inverse of each applied entry is pushed onto to_stack so the
	 * operation can be reversed again.  The loop stops at the first
	 * non-chained entry.
	 */
	char *u_start;
	char *u_end;
	struct undo_object *undo_entry;
	struct undo_object *inverse;
	int chain;

	undo_queue_commit(g);

	if (!*from_stack) {
		status_line(g, "%s", empty_msg);
		return;
	}

	for (;;) {
		undo_entry = *from_stack;
		if (!undo_entry)
			break;

		inverse = NULL;
		switch (undo_entry->u_type) {
		case UNDO_DEL:
		case UNDO_DEL_CHAIN:
			u_start = g->text + undo_entry->start;
			text_hole_make(g, u_start, undo_entry->length);
			memcpy(u_start, undo_entry->undo_text, (size_t)undo_entry->length);
			inverse = new_undo_entry(
			    undo_entry->u_type == UNDO_DEL_CHAIN ? UNDO_INS_CHAIN : UNDO_INS,
			    undo_entry->start, undo_entry->length, NULL);

			status_line(g, "%s [%d] %s %d chars at position %d", op_label,
			            g->modified_count, "restored", undo_entry->length,
			            undo_entry->start);

			chain = (undo_entry->u_type == UNDO_DEL_CHAIN);
			break;
		case UNDO_INS:
		case UNDO_INS_CHAIN:
			u_start = undo_entry->start + g->text;
			u_end = u_start - 1 + undo_entry->length;
			inverse = new_undo_entry(
			    undo_entry->u_type == UNDO_INS_CHAIN ? UNDO_DEL_CHAIN : UNDO_DEL,
			    undo_entry->start, undo_entry->length, u_start);
			text_hole_delete(g, u_start, u_end, NO_UNDO);

			status_line(g, "%s [%d] %s %d chars at position %d", op_label,
			            g->modified_count, "deleted", undo_entry->length,
			            undo_entry->start);

			chain = (undo_entry->u_type == UNDO_INS_CHAIN);
			break;
		case UNDO_SWAP:
			u_start = g->text + undo_entry->start;
			/* Create inverse entry by saving the current (possibly converted)
			 * bytes before overwriting them. */
			inverse = new_undo_entry(UNDO_SWAP, undo_entry->start, undo_entry->length,
			                         u_start);
			/* Restore original bytes — pure memcpy, no memmove. */
			memcpy(u_start, undo_entry->undo_text, (size_t)undo_entry->length);

			status_line(g, "%s [%d] %s %d chars at position %d", op_label,
			            g->modified_count, "swapped", undo_entry->length,
			            undo_entry->start);

			chain = 0;
			break;
		default:
			chain = 0;
			break;
		}

		if (!chain)
			g->dot = g->text + undo_entry->start;

		*from_stack = undo_entry->prev;
		free(undo_entry);

		if (to_stack && inverse)
			push_undo_entry(to_stack, inverse);

		if (dir < 0)
			g->modified_count--;
		else
			g->modified_count++;

		if (!chain) {
			refresh(g, FALSE);
			break;
		}
	}
}

void
undo_pop(struct editor *g)
{
	/*
	 * == Undo the last change, discarding redo history (u without ^R) ==
	 *
	 * Inverse entries are not saved (to_stack = NULL), so redo is not
	 * possible after this call.  Used by the plain 'u' binding.
	 */
	apply_undo_stack(g, &g->undo_stack_tail, NULL, "Already at oldest change",
	                 "Undo", -1);
}

void
undo_with_redo(struct editor *g)
{
	/*
	 * == Undo the last change, saving an inverse for redo (U command) ==
	 *
	 * Inverse entries are pushed onto the redo stack so Ctrl-R can
	 * replay them.
	 */
	apply_undo_stack(g, &g->undo_stack_tail, &g->redo_stack_tail,
	                 "Already at oldest change", "Undo", -1);
}

void
redo_pop(struct editor *g)
{
	/*
	 * == Redo the last undone change (Ctrl-R) ==
	 */
	apply_undo_stack(g, &g->redo_stack_tail, &g->undo_stack_tail,
	                 "Already at newest change", "Redo", 1);
}

/* ---------------------------------------------------------------------------
 * Persistent undo (undofile)
 *
 * Sidecar format:
 *
 *   Header (16 bytes):
 *     char     magic[4]    "VUND"
 *     uint8_t  version     1
 *     uint8_t  pad[3]      0
 *     int32_t  file_sz     st_size of the edited file at write time
 *     uint32_t buf_csum    additive byte checksum of the in-memory buffer
 *
 *   Followed by one record per undo entry, oldest entry first:
 *     uint8_t  u_type
 *     uint8_t  pad[3]
 *     int32_t  start       byte offset into the buffer
 *     int32_t  length      byte count
 *     uint8_t  data[length]  (only present for DEL / DEL_CHAIN / SWAP types)
 *
 * On load the header is validated against the current file size and a
 * buffer checksum.  Any mismatch silently skips the sidecar.  Size=0 files
 * (new files) never produce a match.
 *
 * The format is native-endian and not portable across machines; it is only
 * ever read back by the same binary on the same host.
 * --------------------------------------------------------------------------- */

#define UNDOFILE_MAGIC "VUND"
#define UNDOFILE_VERSION 1

struct undo_file_hdr {
	char magic[4];
	uint8_t version;
	uint8_t pad[3];
	int32_t file_sz;
	uint32_t buf_csum;
} __attribute__((packed));

struct undo_rec_hdr {
	uint8_t u_type;
	uint8_t pad[3];
	int32_t start;
	int32_t length;
} __attribute__((packed));

/* 1 if this u_type stores saved bytes in undo_text; 0 otherwise */
static int
utype_has_data(uint8_t t)
{
	/*
	 * == True if this u_type stores saved bytes in undo_text ==
	 *
	 * DEL and SWAP entries embed the original bytes so they can be
	 * restored without re-reading the file.  INS entries do not (the
	 * bytes are already in the live buffer).
	 */
	return (t == UNDO_DEL || t == UNDO_DEL_CHAIN || t == UNDO_SWAP);
}

/* Sidecar path: "/dir/.basename.vundo" for file "/dir/basename" */
static char *
undofile_path(const char *fn)
{
	/*
	 * == Build the sidecar path for the undo file ==
	 *
	 * For a file at "/dir/basename", returns "/dir/.basename.vundo".
	 * For a bare name "basename", returns ".basename.vundo".
	 * The caller is responsible for freeing the returned string.
	 */
	const char *slash = strrchr(fn, '/');
	const char *base = slash ? slash + 1 : fn;
	size_t dirlen = (size_t)(base - fn); /* 0 if no slash */
	size_t baselen = strlen(base);
	char *out = xmalloc(dirlen + 1 + baselen + 7); /* '.' + base + ".vundo\0" */

	memcpy(out, fn, dirlen);
	out[dirlen] = '.';
	memcpy(out + dirlen + 1, base, baselen);
	strcpy(out + dirlen + 1 + baselen, ".vundo");
	return out;
}

static uint32_t
buf_checksum(const struct editor *g)
{
	/*
	 * == Additive byte checksum over the entire in-memory buffer ==
	 *
	 * Used to verify that the saved undo sidecar still matches the file on
	 * disk when loading.  A mismatch means the file was edited externally
	 * and the undo history is no longer valid.
	 */
	uint32_t s = 0;
	const unsigned char *p = (const unsigned char *)g->text;
	const unsigned char *e = (const unsigned char *)g->end;

	while (p < e)
		s += *p++;
	return s;
}

void
undo_save(struct editor *g, const char *fn)
{
	/*
	 * == Persist the undo stack to a sidecar file ==
	 *
	 * Writes a binary ".basename.vundo" file next to the edited file so
	 * undo history survives across editing sessions.  The file header
	 * records the buffer size and a byte checksum; these are checked on
	 * load to reject stale data if the file was modified externally.
	 *
	 * - Does nothing when VI_UNDOFILE is off or the stack is empty.
	 * - Removes any existing sidecar when the stack is empty.
	 * - Records are written oldest-first so push-on-load reconstructs the
	 *   original LIFO order.
	 */
	char *path;
	int fd;
	struct stat st;
	struct undo_file_hdr hdr;
	struct undo_rec_hdr rec;
	struct undo_object **arr;
	struct undo_object *e;
	int n, i;

	if (!(g->setops & VI_UNDOFILE))
		return;
	if (!fn || !*fn)
		return;

	path = undofile_path(fn);

	if (!g->undo_stack_tail) {
		/* No history — remove stale sidecar if it exists */
		unlink(path);
		free(path);
		return;
	}

	if (stat(fn, &st) < 0) {
		free(path);
		return;
	}

	/* Collect stack into an array to write oldest-first */
	n = 0;
	for (e = g->undo_stack_tail; e; e = e->prev)
		n++;
	arr = xmalloc((size_t)n * sizeof *arr);
	i = 0;
	for (e = g->undo_stack_tail; e; e = e->prev)
		arr[i++] = e;
	/* arr[0] = newest (tail), arr[n-1] = oldest (deepest) */

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	free(path);
	if (fd < 0) {
		free(arr);
		return;
	}

	memcpy(hdr.magic, UNDOFILE_MAGIC, 4);
	hdr.version = UNDOFILE_VERSION;
	hdr.pad[0] = hdr.pad[1] = hdr.pad[2] = 0;
	hdr.file_sz = (int32_t)st.st_size;
	hdr.buf_csum = buf_checksum(g);
	full_write(fd, &hdr, sizeof hdr);

	/* Write oldest-first so push-on-load reconstructs the same LIFO order */
	for (i = n - 1; i >= 0; i--) {
		e = arr[i];
		rec.u_type = e->u_type;
		rec.pad[0] = rec.pad[1] = rec.pad[2] = 0;
		rec.start = (int32_t)e->start;
		rec.length = (int32_t)e->length;
		full_write(fd, &rec, sizeof rec);
		if (utype_has_data(e->u_type) && e->length > 0)
			full_write(fd, e->undo_text, (size_t)e->length);
	}

	close(fd);
	free(arr);
}

void
undo_load(struct editor *g, const char *fn)
{
	/*
	 * == Restore the undo stack from a sidecar file ==
	 *
	 * Reads the ".basename.vundo" sidecar written by undo_save().  Before
	 * loading, validates that the magic bytes, version, file size, and
	 * buffer checksum all match; if any check fails the sidecar is silently
	 * ignored.  Records are pushed in read (oldest-first) order, which
	 * reconstructs the same LIFO stack as the original session.
	 *
	 * - Silently returns if VI_UNDOFILE is off, the file has no sidecar,
	 *   or the sidecar does not match the current buffer.
	 */
	char *path;
	int fd;
	struct stat st;
	struct undo_file_hdr hdr;
	struct undo_rec_hdr rec;
	struct undo_object *entry;
	ssize_t nr;

	if (!(g->setops & VI_UNDOFILE))
		return;
	if (!fn || !*fn)
		return;

	path = undofile_path(fn);
	fd = open(path, O_RDONLY);
	free(path);
	if (fd < 0)
		return; /* no sidecar — normal for a new or first-opened file */

	/* Validate header */
	nr = full_read(fd, &hdr, sizeof hdr);
	if (nr != (ssize_t)sizeof hdr || memcmp(hdr.magic, UNDOFILE_MAGIC, 4) != 0 || hdr.version != UNDOFILE_VERSION) {
		close(fd);
		return;
	}

	/* The file on disk must still match what was saved */
	if (stat(fn, &st) < 0 || hdr.file_sz == 0 || (int32_t)st.st_size != hdr.file_sz) {
		close(fd);
		return;
	}
	if (buf_checksum(g) != hdr.buf_csum) {
		close(fd);
		return;
	}

	/* Deserialise and push records.  Oldest was written first, so pushing
	 * in read order reconstructs the original LIFO stack correctly. */
	while ((nr = full_read(fd, &rec, sizeof rec)) == (ssize_t)sizeof rec) {
		int len = (int)rec.length;

		if (utype_has_data(rec.u_type) && len > 0) {
			entry = xzalloc(offsetof(struct undo_object, undo_text) + (size_t)len);
			if (full_read(fd, entry->undo_text, (size_t)len) != len) {
				free(entry);
				break;
			}
		} else {
			entry = xzalloc(sizeof *entry);
		}
		entry->u_type = rec.u_type;
		entry->start = (int)rec.start;
		entry->length = len;
		push_undo_entry(&g->undo_stack_tail, entry);
	}

	close(fd);
}