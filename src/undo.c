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
	undo_entry->prev = *stack_tail;
	*stack_tail = undo_entry;
}

void
undo_queue_commit(struct editor *g)
{
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
	apply_undo_stack(g, &g->undo_stack_tail, NULL, "Already at oldest change",
	                 "Undo", -1);
}

void
undo_with_redo(struct editor *g)
{
	apply_undo_stack(g, &g->undo_stack_tail, &g->redo_stack_tail,
	                 "Already at oldest change", "Undo", -1);
}

void
redo_pop(struct editor *g)
{
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
	return (t == UNDO_DEL || t == UNDO_DEL_CHAIN || t == UNDO_SWAP);
}

/* Sidecar path: "/dir/.basename.vundo" for file "/dir/basename" */
static char *
undofile_path(const char *fn)
{
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

/* Additive byte checksum over the entire in-memory buffer */
static uint32_t
buf_checksum(const struct editor *g)
{
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