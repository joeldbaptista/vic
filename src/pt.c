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
 * No piece is longer than CHUNK bytes.  So cutting a piece costs at most
 * one newline count over CHUNK bytes.
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
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static size_t
countnl(const char *s, size_t n)
{
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
	return p->buf == PT_ORIG ? t->orig : t->add;
}

static struct piece *
mkpiece(struct ptree *t, int buf, size_t off, size_t len)
{
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
	return t->root ? t->root->tlen : 0;
}

/* return the number of newlines in the text */
size_t
ptnls(struct ptree *t)
{
	return t->root ? t->root->tnl : 0;
}

/* return the byte at off, or -1 past the end */
int
ptchar(struct ptree *t, size_t off)
{
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
	return p ? 1 + countpieces(p->l) + countpieces(p->r) : 0;
}

/* return the number of pieces */
size_t
ptpieces(struct ptree *t)
{
	return countpieces(t->root);
}

/* return 1 if every tree invariant holds; used by the tests */
int
ptcheck(struct ptree *t)
{
	return checkpiece(t, t->root);
}
