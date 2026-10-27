/*
 * text.c - read access to the text, which lives in a piece tree (pt.c).
 */
#include "text.h"

int
text_len(const struct editor *g)
{
	/*
	 * == Number of bytes in the text ==
	 */
	return (int)ptlen(g->pt);
}

int
text_char(const struct editor *g, int p)
{
	/*
	 * == The byte at offset p ==
	 *
	 * Returns the byte as an unsigned value, or 0 when p lies outside
	 * [0, text_len()).  The old slab was zero-filled past its end, so 0
	 * keeps loops that read one byte too far terminating as before.
	 * Sequential reads are cheap: the tree caches the last piece found.
	 */
	int c;

	if (p < 0)
		return 0;
	c = ptchar(g->pt, (size_t)p);
	return c < 0 ? 0 : c;
}

int
text_find(const struct editor *g, int p, int c)
{
	/*
	 * == Offset of the first byte c at or after p, or -1 ==
	 *
	 * Scans piece by piece with memchr, so long lines stay cheap.
	 */
	const char *s;
	const char *hit;
	size_t n;

	if (p < 0)
		p = 0;
	while ((s = ptchunk(g->pt, (size_t)p, &n)) != NULL) {
		hit = memchr(s, c, n);
		if (hit)
			return p + (int)(hit - s);
		p += (int)n;
	}
	return -1;
}

int
text_rfind(const struct editor *g, int p, int c)
{
	/*
	 * == Offset of the last byte c strictly before p, or -1 ==
	 *
	 * A newline search uses the line index of the tree instead of a scan.
	 */
	int line;

	if (p > text_len(g))
		p = text_len(g);
	if (p <= 0)
		return -1;
	if (c == '\n') {
		line = (int)ptlineof(g->pt, (size_t)p);
		if (line == 0)
			return -1;
		return (int)ptlinestart(g->pt, (size_t)line) - 1;
	}
	while (--p >= 0)
		if (text_char(g, p) == c)
			return p;
	return -1;
}

int
text_read(const struct editor *g, int p, int n, char *dst)
{
	/*
	 * == Copy up to n bytes from offset p into dst ==
	 *
	 * Returns the number of bytes copied; fewer than n at the end of
	 * the text.  dst is not NUL-terminated.
	 */
	if (p < 0 || n <= 0)
		return 0;
	return (int)ptget(g->pt, (size_t)p, (size_t)n, dst);
}

char *
text_copy(const struct editor *g, int p, int n)
{
	/*
	 * == A new NUL-terminated copy of n bytes at offset p ==
	 *
	 * The caller frees the result.
	 */
	char *s;

	if (n < 0)
		n = 0;
	s = xmalloc((size_t)n + 1);
	n = text_read(g, p, n, s);
	s[n] = '\0';
	return s;
}

void
text_set_char(struct editor *g, int p, char c)
{
	/*
	 * == Overwrite the byte at offset p with c ==
	 *
	 * The replacement for *p = c.  It records no undo entry and does
	 * not touch modified_count; callers do that themselves, as before.
	 */
	if (p < 0 || p >= text_len(g))
		return;
	ptdelete(g->pt, (size_t)p, 1);
	ptinsert(g->pt, (size_t)p, &c, 1);
}

void
text_set_bytes(struct editor *g, int p, const char *s, int n)
{
	/*
	 * == Overwrite the n bytes at offset p with s ==
	 *
	 * text_set_char() for a whole range, and the reason to prefer it: a
	 * loop of text_set_char() costs one tree descent and one piece split
	 * per byte, which is minutes of work over a large selection, while
	 * this costs one of each for the whole range.  A range that runs past
	 * the end of the text is clipped.  Records no undo entry and does not
	 * touch modified_count, exactly as text_set_char() does.
	 */
	int len = text_len(g);

	if (p < 0 || p >= len || n <= 0)
		return;
	if (n > len - p)
		n = len - p;
	ptdelete(g->pt, (size_t)p, (size_t)n);
	ptinsert(g->pt, (size_t)p, s, (size_t)n);
}

int
text_line_of(const struct editor *g, int p)
{
	/*
	 * == Number of newlines before offset p (its line, counted from 0) ==
	 */
	if (p <= 0)
		return 0;
	return (int)ptlineof(g->pt, (size_t)p);
}

int
text_line_start(const struct editor *g, int line)
{
	/*
	 * == Offset where line (counted from 0) starts ==
	 *
	 * Returns text_len() when the text has fewer lines.
	 */
	if (line <= 0)
		return 0;
	return (int)ptlinestart(g->pt, (size_t)line);
}

int
text_lines(const struct editor *g)
{
	/*
	 * == Number of newlines in the text ==
	 */
	return (int)ptnls(g->pt);
}
