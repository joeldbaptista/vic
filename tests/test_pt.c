/*
 * test_pt.c - piece tree tests.
 *
 * A random sequence of insertions and deletions runs against both a piece
 * tree and a plain byte array.  After every edit the two must hold the same
 * text, and the line queries must agree with a direct scan of the array.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pt.h"
#include "vic.h"

static int fails;

#define CHECK(c) do { \
	if (!(c)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", \
		    __FILE__, __LINE__, #c); \
		fails++; \
	} \
} while (0)

static size_t
refnls(const char *s, size_t n)
{
	size_t i, c;

	c = 0;
	for (i = 0; i < n; i++)
		c += s[i] == '\n';
	return c;
}

static int
same(struct ptree *t, const char *s, size_t n)
{
	char *b;
	int ok;

	if (ptlen(t) != n)
		return 0;
	b = xmalloc(n + 1);
	ok = ptget(t, 0, n, b) == n && memcmp(b, s, n) == 0;
	free(b);
	return ok;
}

static void
checklines(struct ptree *t, const char *s, size_t n)
{
	size_t i, line, start, step;

	CHECK(ptnls(t) == refnls(s, n));
	step = n > 20000 ? 997 : 1;
	line = 0;
	start = 0;
	for (i = 0; i <= n; i++) {
		if (i == start && line % step == 0)
			CHECK(ptlinestart(t, line) == start);
		if (i % step == 0)
			CHECK(ptlineof(t, i) == line);
		if (i < n && s[i] == '\n') {
			line++;
			start = i + 1;
		}
	}
	CHECK(ptlinestart(t, line + 1) == n);
}

static void
testbasic(void)
{
	struct ptree *t;

	t = ptnew(xstrdup("hello\nworld\n"), 12);
	CHECK(same(t, "hello\nworld\n", 12));
	CHECK(ptchar(t, 0) == 'h');
	CHECK(ptchar(t, 12) == -1);
	ptinsert(t, 5, ",", 1);
	CHECK(same(t, "hello,\nworld\n", 13));
	ptinsert(t, 6, " you", 4);
	CHECK(same(t, "hello, you\nworld\n", 17));
	ptdelete(t, 0, 7);
	CHECK(same(t, "you\nworld\n", 10));
	CHECK(ptlinestart(t, 1) == 4);
	CHECK(ptlineof(t, 5) == 1);
	ptdelete(t, 0, 100);
	CHECK(same(t, "", 0));
	ptinsert(t, 0, "x", 1);
	CHECK(same(t, "x", 1));
	CHECK(ptcheck(t));
	ptfree(t);
}

static void
testrandom(size_t initial, int steps)
{
	struct ptree *t;
	char *ref, *s;
	char buf[300];
	size_t n, cap, off, k, i;
	int step;

	cap = initial + 1;
	ref = xmalloc(cap);
	s = xmalloc(initial + 1);
	for (i = 0; i < initial; i++)
		s[i] = rand() % 7 == 0 ? '\n' : 'a' + rand() % 26;
	memcpy(ref, s, initial);
	n = initial;
	t = ptnew(s, initial);
	for (step = 0; step < steps; step++) {
		off = n ? (size_t)rand() % (n + 1) : 0;
		if (rand() % 3) {
			k = 1 + rand() % (rand() % 10 ? 4 : sizeof(buf));
			for (i = 0; i < k; i++)
				buf[i] = rand() % 5 == 0 ? '\n' : 'A' + rand() % 26;
			if (n + k > cap) {
				cap = (n + k) * 2;
				ref = xrealloc(ref, cap);
			}
			memmove(ref + off + k, ref + off, n - off);
			memcpy(ref + off, buf, k);
			n += k;
			ptinsert(t, off, buf, k);
		} else {
			k = 1 + rand() % 20;
			if (off + k > n)
				k = n - off;
			memmove(ref + off, ref + off + k, n - off - k);
			n -= k;
			ptdelete(t, off, k);
		}
		CHECK(same(t, ref, n));
		if (step % 97 == 0) {
			CHECK(ptcheck(t));
			checklines(t, ref, n);
		}
		if (fails)
			break;
	}
	CHECK(ptcheck(t));
	checklines(t, ref, n);
	for (i = 0; i < n; i += 13)
		CHECK(ptchar(t, i) == (unsigned char)ref[i]);
	ptfree(t);
	free(ref);
}

/* a file larger than one chunk must still cut and count correctly */
static void
testlarge(void)
{
	testrandom(300000, 400);
}

int
main(void)
{
	srand(1);
	testbasic();
	testrandom(0, 3000);
	testrandom(100, 3000);
	testlarge();
	if (fails) {
		printf("test_pt: %d failure(s)\n", fails);
		return 1;
	}
	printf("test_pt: ok\n");
	return 0;
}
