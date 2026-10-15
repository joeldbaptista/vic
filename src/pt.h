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
 */
#ifndef SRC_PT_H
#define SRC_PT_H

#include <stddef.h>

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

struct ptree {
	char *orig;     /* original buffer */
	size_t olen;
	char *add;      /* add buffer */
	size_t alen, acap;
	struct piece *root;
	struct piece *cp;      /* piece of the last lookup */
	size_t cpos;    /* text offset where cp starts */
};

enum {
	PT_ORIG,
	PT_ADD
};

struct ptree *ptnew(char *s, size_t n);
void ptfree(struct ptree *t);
size_t ptlen(struct ptree *t);
size_t ptnls(struct ptree *t);
int ptchar(struct ptree *t, size_t off);
const char *ptchunk(struct ptree *t, size_t off, size_t *n);
size_t ptget(struct ptree *t, size_t off, size_t n, char *dst);
void ptinsert(struct ptree *t, size_t off, const char *s, size_t n);
void ptdelete(struct ptree *t, size_t off, size_t n);
size_t ptlinestart(struct ptree *t, size_t line);
size_t ptlineof(struct ptree *t, size_t off);
size_t ptpieces(struct ptree *t);
int ptcheck(struct ptree *t);

#endif
