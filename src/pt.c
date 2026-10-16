/*
 * pt.c - piece tree: the text storage of vic.
 *
 * The tree is a treap: a binary search tree ordered by text position, and
 * a heap ordered by random priority.  Two primitives do all the work.
 * split() cuts a tree at a byte offset, and cuts a piece in two when the
 * offset falls inside it.  merge() joins two trees whose texts are
 * adjacent.  Insertion is a split followed by two merges; deletion is two
 * splits followed by one merge.
 *
 *   ptnew       — build a tree over a buffer and take ownership of it
 *   ptfree      — release the pieces, both buffers and the tree
 *   ptlen       — bytes in the text
 *   ptnls       — newlines in the text
 *   ptchar      — the byte at an offset, or -1 past the end
 *   ptchunk     — the bytes from an offset to the end of their piece
 *   ptget       — copy a byte range into the caller's buffer
 *   ptinsert    — insert bytes at an offset
 *   ptdelete    — remove a byte range
 *   ptlinestart — offset where a line starts
 *   ptlineof    — number of the line holding an offset
 *   ptpieces    — number of pieces, for the tests and diagnostics
 *   ptcheck     — 1 when every tree invariant holds
 *
 * The static helpers split into three groups.  rnd, countnl, base, mkpiece
 * and update build and measure a single piece; split, merge and freepieces
 * restructure the tree; find, checkpiece and countpieces walk it.
 *
 * No piece is longer than CHUNK bytes.  So cutting a piece costs at most
 * one newline count over CHUNK bytes.
 *
 * No hooks.  This module knows nothing of struct editor: text.c and
 * buffer.c wrap it for the rest of vic.
 */
#include <stdlib.h>
#include <string.h>

#include "pt.h"
#include "vic.h"

#define CHUNK 4096
#define MIN(a, b) ((a) < (b) ? (a) : (b))

static unsigned rnd(void);
static size_t countnl(const char *s, size_t n);
static const char *base(struct ptree *t, struct piece *p);
static struct piece *mkpiece(struct ptree *t, int buf, size_t off, size_t len);
static void update(struct piece *p);
static void split(struct ptree *t, struct piece *p, size_t k, struct piece **l, struct piece **r);
static struct piece *merge(struct piece *a, struct piece *b);
static void freepieces(struct piece *p);
static struct piece *find(struct ptree *t, size_t off, size_t *start);
static int checkpiece(struct ptree *t, struct piece *p);
static size_t countpieces(struct piece *p);

static unsigned seed = 2463534242u;

static unsigned
rnd(void)
{
	/*
	 * == The next treap priority ==
	 *
	 * A xorshift32 generator.  The seed is a file-scope variable that
	 * starts from a fixed value, so a given sequence of edits always
	 * builds the same tree and the tests are reproducible.
	 */
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static size_t
countnl(const char *s, size_t n)
{
	/*
	 * == Number of newlines in the n bytes at s ==
	 *
	 * Every newline counter in the tree comes from here, so the scan
	 * uses memchr rather than a byte loop.  This is the only linear work
	 * in the module, and split() and mkpiece() bound it to CHUNK bytes.
	 */
	const char *e;
	size_t c;

	c = 0;
	e = s + n;
	while (s < e && (s = memchr(s, '\n', e - s))) {
		c++;
		s++;
	}
	return c;
}

static const char *
base(struct ptree *t, struct piece *p)
{
	/*
	 * == The buffer that p names ==
	 *
	 * A piece holds an offset and a length, not a pointer, because the
	 * add buffer moves when it grows.  So every read resolves the buffer
	 * here first, and base(t, p) + p->off is the first byte of p.
	 */
	return p->buf == PT_ORIG ? t->orig : t->add;
}

static struct piece *
mkpiece(struct ptree *t, int buf, size_t off, size_t len)
{
	/*
	 * == A fresh piece over buf[off, off + len) ==
	 *
	 * Counts the newlines in the range and takes a priority from rnd().
	 * The piece has no children, so update() leaves the subtree counters
	 * equal to its own.  The caller must have the named buffer in place
	 * already: the newline count reads it.
	 */
	struct piece *p;

	p = xmalloc(sizeof(*p));
	p->l = p->r = NULL;
	p->pri = rnd();
	p->buf = buf;
	p->off = off;
	p->len = len;
	p->nl = countnl(base(t, p) + off, len);
	update(p);
	return p;
}

static void
update(struct piece *p)
{
	/*
	 * == Recompute the subtree counters of p ==
	 *
	 * tlen and tnl summarise the subtree, which is what makes the offset
	 * and line lookups logarithmic.  Both are the node's own count plus
	 * the counts of the two children, so every function that changes a
	 * child pointer, a len or an nl must call this on the way back up.
	 */
	p->tlen = p->len;
	p->tnl = p->nl;
	if (p->l) {
		p->tlen += p->l->tlen;
		p->tnl += p->l->tnl;
	}
	if (p->r) {
		p->tlen += p->r->tlen;
		p->tnl += p->r->tnl;
	}
}

/* split p so that *l holds the first k bytes and *r holds the rest */
static void
split(struct ptree *t, struct piece *p, size_t k, struct piece **l, struct piece **r)
{
	/*
	 * == Cut the tree at byte offset k ==
	 *
	 * Three cases.  When k falls in the left subtree, p and everything
	 * right of it belong to *r, so the recursion continues left and p
	 * takes the right half as its new left child.  The case of k in the
	 * right subtree mirrors it.  Otherwise k falls strictly inside p, so
	 * the piece itself is cut in two: p keeps the first m bytes and the
	 * new piece q takes the rest.
	 *
	 * The cut gives q the priority of p, which keeps the heap order
	 * intact without a rotation, because q replaces p's old position.
	 * The newline count of the halves is the one scan in the operation,
	 * so it runs over the shorter half and subtracts to get the other.
	 *
	 * k is not clamped.  A k at or past the length of the tree leaves
	 * *r NULL, and a k of 0 leaves *l NULL.  Both are deliberate: that
	 * is how ptinsert() and ptdelete() handle an edit at either end.
	 * The strict inequality in the third case also means a cut never
	 * produces an empty piece.
	 */
	struct piece *q;
	size_t ll, m, nl;

	if (!p) {
		*l = *r = NULL;
		return;
	}
	ll = p->l ? p->l->tlen : 0;
	if (k <= ll) {
		split(t, p->l, k, l, &p->l);
		update(p);
		*r = p;
	} else if (k >= ll + p->len) {
		split(t, p->r, k - ll - p->len, &p->r, r);
		update(p);
		*l = p;
	} else {
		/* k falls inside p: cut the piece in two */
		m = k - ll;
		q = xmalloc(sizeof(*q));
		q->pri = p->pri;
		q->buf = p->buf;
		q->off = p->off + m;
		q->len = p->len - m;
		if (m < q->len) {
			nl = countnl(base(t, p) + p->off, m);
			q->nl = p->nl - nl;
		} else {
			q->nl = countnl(base(t, q) + q->off, q->len);
			nl = p->nl - q->nl;
		}
		p->len = m;
		p->nl = nl;
		q->l = NULL;
		q->r = p->r;
		p->r = NULL;
		update(p);
		update(q);
		*l = p;
		*r = q;
	}
}

static struct piece *
merge(struct piece *a, struct piece *b)
{
	/*
	 * == Join two trees whose texts are adjacent ==
	 *
	 * The text of a comes before the text of b, so no comparison of
	 * offsets is needed: the search order is already settled by the
	 * argument order.  Only the heap order has to be restored, so the
	 * root is whichever of the two roots has the higher priority, and
	 * the recursion walks the facing edges of the two trees.
	 *
	 * The caller must not pass trees that overlap or leave a gap.  So
	 * merge is only ever called on the output of a split, or on a split
	 * output and a piece built for the new text.
	 */
	if (!a)
		return b;
	if (!b)
		return a;
	if (a->pri > b->pri) {
		a->r = merge(a->r, b);
		update(a);
		return a;
	}
	b->l = merge(a, b->l);
	update(b);
	return b;
}

static void
freepieces(struct piece *p)
{
	/*
	 * == Release a subtree ==
	 *
	 * Post-order, so a node is freed after both of its children.  The
	 * pieces point into the two buffers and never own text, so nothing
	 * else is released here.
	 */
	if (!p)
		return;
	freepieces(p->l);
	freepieces(p->r);
	free(p);
}

/* return the piece holding offset off, and its text offset in *start */
static struct piece *
find(struct ptree *t, size_t off, size_t *start)
{
	/*
	 * == The piece holding off, and where that piece starts ==
	 *
	 * The descent compares off against the byte count of the left
	 * subtree, then subtracts the bytes it has passed before going
	 * right, so pos accumulates the text offset of the current node.
	 * Returns NULL when off is at or past the end of the text.
	 *
	 * One piece is cached in t->cp with its offset in t->cpos.  A scan
	 * that walks the text one byte at a time therefore pays for the
	 * descent once per piece instead of once per byte.  ptinsert() and
	 * ptdelete() clear the cache, because they move pieces around.
	 */
	struct piece *p;
	size_t ll, pos;

	if (t->cp && t->cpos <= off && off < t->cpos + t->cp->len) {
		*start = t->cpos;
		return t->cp;
	}
	pos = 0;
	p = t->root;
	while (p) {
		ll = p->l ? p->l->tlen : 0;
		if (off < ll) {
			p = p->l;
		} else if (off < ll + p->len) {
			*start = pos + ll;
			t->cp = p;
			t->cpos = *start;
			return p;
		} else {
			pos += ll + p->len;
			off -= ll + p->len;
			p = p->r;
		}
	}
	return NULL;
}

static int
checkpiece(struct ptree *t, struct piece *p)
{
	/*
	 * == 1 when the subtree at p is well formed ==
	 *
	 * Checks four invariants per node, over the whole subtree: the piece
	 * is neither empty nor longer than CHUNK; its newline count matches
	 * a direct scan of the bytes it names; neither child outranks it in
	 * the heap; and its two subtree counters equal the sums they are
	 * meant to cache.  The scan makes this linear in the text, so only
	 * the tests call it.
	 */
	size_t len, nl;

	if (!p)
		return 1;
	if (!checkpiece(t, p->l) || !checkpiece(t, p->r))
		return 0;
	if (p->len == 0 || p->len > CHUNK)
		return 0;
	if (p->nl != countnl(base(t, p) + p->off, p->len))
		return 0;
	if ((p->l && p->l->pri > p->pri) || (p->r && p->r->pri > p->pri))
		return 0;
	len = p->len + (p->l ? p->l->tlen : 0) + (p->r ? p->r->tlen : 0);
	nl = p->nl + (p->l ? p->l->tnl : 0) + (p->r ? p->r->tnl : 0);
	return len == p->tlen && nl == p->tnl;
}

/* make a tree over the n bytes of s; the tree takes ownership of s */
struct ptree *
ptnew(char *s, size_t n)
{
	/*
	 * == A tree over the n bytes of s ==
	 *
	 * s becomes the original buffer, which never changes and which
	 * ptfree() frees, so s must come from the allocator.  A NULL s with
	 * an n of 0 gives an empty tree, which is how a new file starts.
	 *
	 * The text is cut into pieces of CHUNK bytes merged left to right.
	 * Each piece takes a fresh priority, so the result is a treap of
	 * expected logarithmic depth rather than the right-leaning chain the
	 * insertion order would otherwise produce.
	 */
	struct ptree *t;
	size_t off, m;

	t = xmalloc(sizeof(*t));
	t->orig = s;
	t->olen = n;
	t->acap = 4096;
	t->alen = 0;
	t->add = xmalloc(t->acap);
	t->root = NULL;
	t->cp = NULL;
	t->cpos = 0;
	for (off = 0; off < n; off += m) {
		m = MIN(n - off, CHUNK);
		t->root = merge(t->root, mkpiece(t, PT_ORIG, off, m));
	}
	return t;
}

void
ptfree(struct ptree *t)
{
	/*
	 * == Release the tree and everything it owns ==
	 *
	 * That is the pieces, the original buffer, the add buffer and the
	 * struct itself.  Accepts NULL.  Any pointer handed out by
	 * ptchunk() dangles afterwards.
	 */
	if (!t)
		return;
	freepieces(t->root);
	free(t->orig);
	free(t->add);
	free(t);
}

size_t
ptlen(struct ptree *t)
{
	/*
	 * == Bytes in the text ==
	 *
	 * The byte counter of the root subtree, so this is a read and not a
	 * walk.  Also the one offset past the last byte, which is where an
	 * append inserts.
	 */
	return t->root ? t->root->tlen : 0;
}

/* return the number of newlines in the text */
size_t
ptnls(struct ptree *t)
{
	/*
	 * == Newlines in the text ==
	 *
	 * The newline counter of the root subtree.  A text that ends with a
	 * newline has an empty last line, so the count of lines is this plus
	 * one, and ptlinestart() accepts every line from 0 to this value.
	 */
	return t->root ? t->root->tnl : 0;
}

/* return the byte at off, or -1 past the end */
int
ptchar(struct ptree *t, size_t off)
{
	/*
	 * == The byte at off ==
	 *
	 * Returns it as an unsigned char, so a high byte of UTF-8 comes back
	 * positive and -1 means off is at or past the end of the text.  The
	 * lookup cache in find() makes a sequential scan cheap, but a scan
	 * over whole pieces should use ptchunk() instead.
	 */
	struct piece *p;
	size_t start;

	if (!(p = find(t, off, &start)))
		return -1;
	return (unsigned char)base(t, p)[p->off + off - start];
}

/*
 * return the bytes from off to the end of their piece, and their count in
 * *n.  The pointer stays valid until the next ptinsert().
 */
const char *
ptchunk(struct ptree *t, size_t off, size_t *n)
{
	/*
	 * == A run of bytes starting at off ==
	 *
	 * The run stops at the end of the piece holding off, so a loop that
	 * advances off by *n visits every piece once and copies nothing.
	 * Returns NULL with *n set to 0 at the end of the text, and never
	 * returns an empty run otherwise.
	 *
	 * The pointer is into one of the two buffers.  So the next
	 * ptinsert() may invalidate it, because growing the add buffer
	 * reallocates it, and the next ptdelete() may free the piece it
	 * describes.
	 */
	struct piece *p;
	size_t start;

	if (!(p = find(t, off, &start))) {
		*n = 0;
		return NULL;
	}
	*n = p->len - (off - start);
	return base(t, p) + p->off + off - start;
}

/* copy up to n bytes from off into dst; return the number copied */
size_t
ptget(struct ptree *t, size_t off, size_t n, char *dst)
{
	/*
	 * == Copy the n bytes at off into dst ==
	 *
	 * Walks the pieces with ptchunk() and copies each run.  A range that
	 * runs past the end of the text is clipped, so the return value is
	 * the number of bytes actually copied and a value below n marks the
	 * clip.  dst must have room for n bytes.
	 */
	const char *s;
	size_t m, done;

	done = 0;
	while (done < n && (s = ptchunk(t, off + done, &m))) {
		m = MIN(m, n - done);
		memcpy(dst + done, s, m);
		done += m;
	}
	return done;
}

void
ptinsert(struct ptree *t, size_t off, const char *s, size_t n)
{
	/*
	 * == Insert the n bytes of s at off ==
	 *
	 * The bytes are appended to the add buffer, which only grows, so the
	 * text is never moved and the original buffer is never touched.  The
	 * tree is then cut at off and the new pieces go between the halves.
	 * An off past the end of the text is clipped to the end, and an n of
	 * 0 does nothing.
	 *
	 * Typing is the common case, and it arrives as one insertion per
	 * byte at the end of the previous one.  So when the last piece of
	 * the left half ends exactly where the new bytes start in the add
	 * buffer, that piece is extended in place instead of a new piece
	 * being made.  Extending it changes the counters of every node above
	 * it, which are the nodes on the right spine of the left half, hence
	 * the walk down that spine.  The CHUNK limit still holds, so a long
	 * run of typing eventually starts a new piece.
	 */
	struct piece *l, *r, *p;
	size_t at, m, nl;

	if (n == 0)
		return;
	t->cp = NULL;
	if (t->alen + n > t->acap) {
		while (t->alen + n > t->acap)
			t->acap *= 2;
		t->add = xrealloc(t->add, t->acap);
	}
	at = t->alen;
	memcpy(t->add + at, s, n);
	t->alen += n;
	split(t, t->root, MIN(off, ptlen(t)), &l, &r);
	for (; n > 0; at += m, n -= m) {
		for (p = l; p && p->r; p = p->r)
			;
		if (p && p->buf == PT_ADD && p->off + p->len == at &&
		    p->len < CHUNK) {
			/* extend the last piece: typing appends here */
			m = MIN(n, CHUNK - p->len);
			nl = countnl(t->add + at, m);
			for (p = l; p; p = p->r) {
				p->tlen += m;
				p->tnl += nl;
				if (!p->r) {
					p->len += m;
					p->nl += nl;
				}
			}
		} else {
			m = MIN(n, CHUNK);
			l = merge(l, mkpiece(t, PT_ADD, at, m));
		}
	}
	t->root = merge(l, r);
}

void
ptdelete(struct ptree *t, size_t off, size_t n)
{
	/*
	 * == Remove the n bytes at off ==
	 *
	 * Two splits isolate the range as its own tree, which is then freed,
	 * and one merge closes the gap.  The text itself is never moved, and
	 * the bytes stay in their buffer, so an undo only has to insert a
	 * piece naming them again.
	 *
	 * An off at or past the end of the text does nothing, as does an n
	 * of 0.  A range that runs past the end is clipped by the second
	 * split, which leaves r NULL.
	 */
	struct piece *l, *m, *d, *r;

	if (off >= ptlen(t) || n == 0)
		return;
	t->cp = NULL;
	split(t, t->root, off, &l, &m);
	split(t, m, n, &d, &r);
	freepieces(d);
	t->root = merge(l, r);
}

/* return the offset where line starts; ptlen() if there is no such line */
size_t
ptlinestart(struct ptree *t, size_t line)
{
	/*
	 * == Offset where the given line starts ==
	 *
	 * Line 0 starts at offset 0, and line n starts one byte after the
	 * n-th newline.  A line number above the newline count has no start,
	 * so the answer is ptlen().
	 *
	 * The descent is the offset descent of find() with the newline
	 * counters in place of the byte counters, so it finds the piece
	 * holding the n-th newline in logarithmic time.  Only that piece is
	 * scanned, and it is at most CHUNK bytes.  The inner loop needs no
	 * bound test: the piece holds at least k newlines, so memchr finds
	 * each one.
	 */
	struct piece *p;
	const char *s, *q;
	size_t k, pos, ll, ln;

	if (line == 0)
		return 0;
	if (line > ptnls(t))
		return ptlen(t);
	k = line;
	pos = 0;
	p = t->root;
	while (p) {
		ll = p->l ? p->l->tlen : 0;
		ln = p->l ? p->l->tnl : 0;
		if (k <= ln) {
			p = p->l;
			continue;
		}
		k -= ln;
		if (k <= p->nl) {
			s = base(t, p) + p->off;
			for (q = s;; q++) {
				q = memchr(q, '\n', p->len - (q - s));
				if (--k == 0)
					return pos + ll + (q - s) + 1;
			}
		}
		k -= p->nl;
		pos += ll + p->len;
		p = p->r;
	}
	return ptlen(t); /* NOTREACHED */
}

/* return the number of the line holding off: the newlines before off */
size_t
ptlineof(struct ptree *t, size_t off)
{
	/*
	 * == Number of the line holding off ==
	 *
	 * The line number is the count of newlines before off, which makes
	 * this the inverse of ptlinestart().  The descent sums the newline
	 * counters of the subtrees it passes, then scans the part of the
	 * last piece that lies before off.  An off past the end of the text
	 * gives the total count, which is the number of the empty last line
	 * of a text that ends with a newline.
	 */
	struct piece *p;
	size_t nl, ll;

	nl = 0;
	p = t->root;
	while (p) {
		ll = p->l ? p->l->tlen : 0;
		if (off <= ll) {
			p = p->l;
			continue;
		}
		nl += p->l ? p->l->tnl : 0;
		off -= ll;
		if (off <= p->len)
			return nl + countnl(base(t, p) + p->off, off);
		nl += p->nl;
		off -= p->len;
		p = p->r;
	}
	return nl;
}

static size_t
countpieces(struct piece *p)
{
	/*
	 * == Nodes in the subtree at p ==
	 *
	 * A plain recursive count, because no node caches it.
	 */
	return p ? 1 + countpieces(p->l) + countpieces(p->r) : 0;
}

/* return the number of pieces */
size_t
ptpieces(struct ptree *t)
{
	/*
	 * == Number of pieces in the tree ==
	 *
	 * Reports fragmentation, which no other function exposes.  The tests
	 * use it to confirm that typing extends one piece, and it is useful
	 * when reading the output of tests/pt-test-run.
	 */
	return countpieces(t->root);
}

/* return 1 if every tree invariant holds; used by the tests */
int
ptcheck(struct ptree *t)
{
	/*
	 * == 1 when every tree invariant holds ==
	 *
	 * See checkpiece() for the list.  The check rescans the whole text,
	 * so it belongs in the tests and not on an edit path.
	 */
	return checkpiece(t, t->root);
}
