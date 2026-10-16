/*
 * pt-test-run.c - a guided tour of the piece tree API.
 *
 * Each example calls one group of the functions declared in pt.h and
 * prints both what it did and what came back.  This program is a manual
 * driver, not a test: it checks nothing, so read its output.  The tests
 * live in tests/test_pt.c.
 *
 * Build and run with:  make pt-test-run && ./pt-test-run
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pt.h"

static const char TEXT[] =
	"1. Hello there. This is a line.\n"
	"2. And this is another line.\n"
	"3. And this is the last line.\n";

/* print n bytes of s, showing a newline as the two characters \n */
static void
putvis(const char *s, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (s[i] == '\n')
			fputs("\\n", stdout);
		else
			putchar(s[i]);
	}
}

/* print the whole text of t on one line, through ptchunk() */
static void
show(struct ptree *t)
{
	const char *s;
	size_t off, n;

	for (off = 0; (s = ptchunk(t, off, &n)) != NULL; off += n)
		putvis(s, n);
}

/* print every counter that describes t, and the text itself */
static void
stats(struct ptree *t, const char *label)
{
	printf("%-26s len=%-3zu nls=%zu pieces=%zu check=%d\n",
	    label, ptlen(t), ptnls(t), ptpieces(t), ptcheck(t));
	printf("%-26s [", "");
	show(t);
	printf("]\n");
}

static void
head(const char *title)
{
	printf("\n=== %s ===\n", title);
}

/*
 * ptnew() builds a tree over a buffer and takes ownership of it, so the
 * buffer must come from the allocator: ptfree() calls free() on it.
 */
static struct ptree *
ex_new(void)
{
	struct ptree *t;
	char *s;

	head("ptnew, ptlen, ptnls, ptpieces, ptcheck");
	if ((s = strdup(TEXT)) == NULL) {
		perror("strdup");
		exit(EXIT_FAILURE);
	}
	t = ptnew(s, strlen(TEXT));
	stats(t, "after ptnew");
	printf("ptnew cuts the text into pieces of at most 4096 bytes, so\n"
	    "this short text is one piece.  ptcheck returns 1 while every\n"
	    "tree invariant holds.\n");
	return t;
}

/* ptchar() reads one byte, and reports the end of the text as -1 */
static void
ex_char(struct ptree *t)
{
	size_t off, n, nl1;

	head("ptchar: one byte at a time");
	n = ptlen(t);
	nl1 = (size_t)(strchr(TEXT, '\n') - TEXT);
	printf("the first 15 bytes: ");
	for (off = 0; off < 15; off++)
		putchar(ptchar(t, off));
	printf("\n");
	printf("ptchar(t, 0) = %d, the code of '%c'\n",
	    ptchar(t, 0), ptchar(t, 0));
	printf("ptchar(t, %zu) = %d, the first newline\n",
	    nl1, ptchar(t, nl1));
	printf("ptchar(t, %zu) = %d, one past the end\n", n, ptchar(t, n));
}

/*
 * ptchunk() returns the bytes from an offset to the end of their piece, so
 * a loop over it walks the text one piece per step and copies nothing.
 */
static void
ex_chunk(struct ptree *t)
{
	const char *s;
	size_t off, n, k;

	head("ptchunk: walk the text piece by piece");
	k = 0;
	for (off = 0; (s = ptchunk(t, off, &n)) != NULL; off += n) {
		printf("chunk %zu: off=%-3zu n=%-3zu [", k++, off, n);
		putvis(s, n);
		printf("]\n");
	}
	printf("ptchunk returns NULL at the end.  The pointer it returns\n"
	    "stays valid only until the next ptinsert, which may move the\n"
	    "add buffer.\n");
}

/* ptget() copies a byte range into the caller's buffer */
static void
ex_get(struct ptree *t)
{
	char buf[16];
	size_t n;

	head("ptget: copy a byte range out");
	n = ptget(t, 3, 11, buf);
	printf("ptget(t, 3, 11, buf) copied %zu bytes: [", n);
	putvis(buf, n);
	printf("]\n");
	n = ptget(t, ptlen(t) - 5, sizeof(buf), buf);
	printf("a request past the end is clipped to %zu bytes: [", n);
	putvis(buf, n);
	printf("]\n");
	printf("ptget returns the number of bytes copied, so compare it\n"
	    "with the number asked for to detect the clip.\n");
}

/*
 * ptinsert() splits the tree at the offset, appends the new bytes to the
 * add buffer, and merges the three parts back.  Every insertion raises the
 * piece count, except an insertion that continues the last typed piece.
 */
static void
ex_insert(struct ptree *t)
{
	const char *s;

	head("ptinsert: insert at an offset");
	s = "0. A line before the first.\n";
	ptinsert(t, 0, s, strlen(s));
	stats(t, "insert at offset 0");
	s = "(inserted) ";
	ptinsert(t, ptlinestart(t, 2), s, strlen(s));
	stats(t, "insert at the line 2 start");
	s = "4. A line after the last.\n";
	ptinsert(t, ptlen(t), s, strlen(s));
	stats(t, "insert at ptlen, the end");
	printf("an offset past the end is clipped to the end, so an insert\n"
	    "at ptlen and an insert at a larger offset do the same thing.\n");
}

/* ptdelete() removes n bytes at an offset, and ignores an empty range */
static void
ex_delete(struct ptree *t)
{
	head("ptdelete: remove a byte range");
	ptdelete(t, 0, ptlinestart(t, 1));
	stats(t, "delete the first line");
	ptdelete(t, 5, 7);
	stats(t, "delete 7 bytes at offset 5");
	ptdelete(t, ptlen(t) + 100, 3);
	stats(t, "delete past the end: no-op");
	printf("ptdelete clips a range that runs past the end, and returns\n"
	    "without touching the tree when the offset itself is past it.\n");
}

/*
 * The two line functions are inverses of each other.  Line 0 starts at
 * offset 0, and line n starts after the n-th newline.
 */
static void
ex_lines(struct ptree *t)
{
	char buf[24];
	size_t line, off, nls, n, k;

	head("ptlinestart and ptlineof: lines and offsets");
	nls = ptnls(t);
	printf("ptnls(t) = %zu, the number of newlines\n", nls);
	for (line = 0; line <= nls; line++) {
		off = ptlinestart(t, line);
		printf("ptlinestart(t, %zu) = %-3zu ", line, off);
		if (off == ptlen(t)) {
			printf("the end: the text ends with a newline, so\n"
			    "%34sthis last line is empty\n", "");
			continue;
		}
		n = ptget(t, off, sizeof(buf), buf);
		printf("[");
		putvis(buf, n);
		printf("...]\n");
	}
	printf("ptlinestart(t, %zu) = %zu, which equals ptlen: there is no\n"
	    "such line\n", nls + 5, ptlinestart(t, nls + 5));
	printf("\n");
	for (k = 0; k <= 4; k++) {
		off = k * ptlen(t) / 4;
		printf("ptlineof(t, %-3zu) = %zu, the newlines before that"
		    " offset\n", off, ptlineof(t, off));
	}
}

/*
 * A tree may start empty.  Typing at the end then extends the last piece
 * instead of adding one, because the new bytes continue the add buffer.
 */
static void
ex_typing(void)
{
	struct ptree *t;
	const char *s;
	size_t i;

	head("an empty tree, then typing into it");
	t = ptnew(NULL, 0);
	stats(t, "ptnew(NULL, 0)");
	s = "typed at the end\n";
	for (i = 0; s[i] != '\0'; i++)
		ptinsert(t, ptlen(t), s + i, 1);
	stats(t, "after one insert per byte");
	printf("the %zu inserts share one piece.  An insert that appends to\n"
	    "the end of the add buffer grows the last piece, so typing does\n"
	    "not fragment the tree.\n", strlen(s));
	ptfree(t);
}

int
main(void)
{
	struct ptree *t;

	t = ex_new();
	ex_char(t);
	ex_chunk(t);
	ex_get(t);
	ex_insert(t);
	ex_chunk(t);
	ex_delete(t);
	ex_lines(t);
	ptfree(t);
	ex_typing();
	head("ptfree");
	printf("ptfree releases the pieces, the original buffer, the add\n"
	    "buffer, and the tree itself.  It accepts NULL.\n");
	ptfree(NULL);
	return 0;
}
