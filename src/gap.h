#ifndef SRC_GAP_H
#define SRC_GAP_H

#include "vic.h"

/* Physical end of allocation == logical end of content. */
static inline char *buf_end(const struct editor *g)
{
	return g->text + g->text_size;
}

/* Total content bytes (gap excluded). */
static inline int buf_content_size(const struct editor *g)
{
	return g->text_size - (int)(g->gap_end - g->gap_start);
}

/* Advance one byte, jumping the gap. */
static inline char *buf_next(const struct editor *g, char *p)
{
	p++;
	if (p == g->gap_start)
		p = g->gap_end;
	return p;
}

/* Retreat one byte, jumping the gap backward. */
static inline char *buf_prev(const struct editor *g, char *p)
{
	if (p == g->gap_end)
		p = g->gap_start;
	return p - 1;
}

/* Byte value immediately before p in logical content. */
static inline unsigned char buf_char_before(const struct editor *g, char *p)
{
	return (unsigned char)*buf_prev(g, p);
}

/*
 * Logical byte offset of content pointer p (number of content bytes before p,
 * excluding gap bytes).
 */
static inline int logical_pos(const struct editor *g, const char *p)
{
	if (p <= g->gap_start)
		return (int)(p - g->text);
	return (int)(p - g->text) - (int)(g->gap_end - g->gap_start);
}

/*
 * Physical pointer corresponding to logical byte offset n.
 */
static inline char *phys_ptr(const struct editor *g, int n)
{
	int pre = (int)(g->gap_start - g->text);
	if (n <= pre)
		return g->text + n;
	return g->text + n + (int)(g->gap_end - g->gap_start);
}

#endif
