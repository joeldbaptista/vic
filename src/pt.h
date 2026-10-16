/*
 * pt.h - piece tree: the text storage of vic.
 *
 * The text is a sequence of pieces.  A piece names a byte range in one of
 * two buffers: the original buffer, which holds the loaded file and never
 * changes, and the add buffer, which only grows.  The pieces are the nodes
 * of a treap ordered by text position.  Each node keeps the byte count and
 * the newline count of its subtree, so the tree maps a byte offset to a
 * piece, and a line number to a byte offset, in logarithmic time.
 *
 * Offsets are byte offsets into the text.  Line n starts after the n-th
 * newline; line 0 starts at offset 0.
 *
 * A tree owns the buffer handed to ptnew() and both buffers thereafter, so
 * the buffer must come from the allocator and ptfree() releases it.  The
 * text itself is never moved by an edit, which is why ptchunk() can hand
 * out a pointer into a buffer, and why the undo of a deletion costs no
 * copy: the deleted bytes stay where they were.
 *
 * This module knows nothing of struct editor.  src/text.h wraps it for
 * reading and src/buffer.h for editing, and those two are what the rest of
 * vic calls.  See src/pt.c for the structure of the implementation, and
 * tests/pt-test-run.c for a worked example of every function below.
 */
#ifndef SRC_PT_H
#define SRC_PT_H

#include <stddef.h>

/*
 * One piece, and one node of the treap.  The byte range is an offset and a
 * length rather than a pointer, because the add buffer moves when it grows.
 * len and nl describe the piece; tlen and tnl describe the subtree, and are
 * the caches that make the lookups logarithmic.
 */
struct piece {
	struct piece *l, *r;   /* children */
	unsigned pri;   /* treap priority */
	int buf;        /* PT_ORIG or PT_ADD */
	size_t off;     /* start in the buffer */
	size_t len;     /* bytes in this piece */
	size_t nl;      /* newlines in this piece */
	size_t tlen;    /* bytes in this subtree */
	size_t tnl;     /* newlines in this subtree */
};

/*
 * One text.  cp and cpos cache the piece found by the last lookup, so a
 * scan that walks the text pays for the descent once per piece; an edit
 * clears the cache.  Treat every field as private to pt.c.
 */
struct ptree {
	char *orig;     /* original buffer */
	size_t olen;
	char *add;      /* add buffer */
	size_t alen, acap;
	struct piece *root;
	struct piece *cp;      /* piece of the last lookup */
	size_t cpos;    /* text offset where cp starts */
};

/* which buffer a piece names */
enum {
	PT_ORIG,
	PT_ADD
};

/* Build a tree over the n bytes of s, which the tree then owns, and
 * release one.  ptnew accepts a NULL s when n is 0, and ptfree accepts a
 * NULL tree. */
struct ptree *ptnew(char *s, size_t n);
void ptfree(struct ptree *t);

/* Size of the text.  ptlen is also the offset at which an append inserts,
 * and ptnls is the highest line number ptlinestart accepts. */
size_t ptlen(struct ptree *t);
size_t ptnls(struct ptree *t);

/* Read the text.  ptchar returns -1 at the end, ptchunk returns NULL
 * there, and ptget returns the number of bytes it copied, which is short
 * of n when the range runs past the end.  The pointer from ptchunk reaches
 * to the end of one piece and stays valid only until the next edit. */
int ptchar(struct ptree *t, size_t off);
const char *ptchunk(struct ptree *t, size_t off, size_t *n);
size_t ptget(struct ptree *t, size_t off, size_t n, char *dst);

/* Edit the text.  An offset past the end is clipped to the end, so an
 * insert there appends; a deletion at or past the end does nothing, and
 * one that overruns the end is clipped. */
void ptinsert(struct ptree *t, size_t off, const char *s, size_t n);
void ptdelete(struct ptree *t, size_t off, size_t n);

/* Convert between a line number and an offset.  The two are inverses:
 * ptlinestart gives the offset where a line begins, or ptlen when there is
 * no such line, and ptlineof gives the number of the line holding an
 * offset, which is the count of newlines before it. */
size_t ptlinestart(struct ptree *t, size_t line);
size_t ptlineof(struct ptree *t, size_t off);

/* Inspect the tree itself.  ptpieces reports fragmentation; ptcheck
 * returns 1 while every invariant holds, and rescans the whole text, so it
 * belongs in the tests. */
size_t ptpieces(struct ptree *t);
int ptcheck(struct ptree *t);

#endif
