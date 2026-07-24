/*
 * gap.c - gap-buffer pointer arithmetic.
 *
 * The text store is a gap buffer (see buffer.c for the full layout
 * description). These are the small, extremely hot helpers every other
 * module uses to walk it:
 *
 *   buf_end            physical end of the allocation
 *   buf_content_size   total content bytes, gap excluded
 *   buf_next / buf_prev  step one byte forward/backward, jumping the gap
 *   buf_char_before     byte immediately before a content pointer
 *   logical_pos         physical pointer -> logical (gap-free) offset
 *   phys_ptr            logical offset -> physical pointer
 *
 * No hooks -- depends only on vic.h.
 */
#include "gap.h"

char *
buf_end(const struct editor *g)
{
	/*
	 * == Physical end of allocation == logical end of content ==
	 */
	return g->text + g->text_size;
}

int
buf_content_size(const struct editor *g)
{
	/*
	 * == Total content bytes (gap excluded) ==
	 */
	return g->text_size - (int)(g->gap_end - g->gap_start);
}

char *
buf_next(const struct editor *g, char *p)
{
	/*
	 * == Advance one byte, jumping the gap ==
	 */
	p++;
	if (p == g->gap_start)
		p = g->gap_end;
	return p;
}

char *
buf_prev(const struct editor *g, char *p)
{
	/*
	 * == Retreat one byte, jumping the gap backward ==
	 *
	 * Clamps at g->text: p can legitimately be gap_end with gap_start
	 * already at g->text (nothing before the gap), and unconditionally
	 * returning p - 1 after the jump would underflow before the
	 * allocation instead of signalling "nothing before this".
	 */
	if (p == g->gap_end)
		p = g->gap_start;
	if (p <= g->text)
		return g->text;
	return p - 1;
}

unsigned char
buf_char_before(const struct editor *g, char *p)
{
	/*
	 * == Byte value immediately before p in logical content ==
	 */
	return (unsigned char)*buf_prev(g, p);
}

int
logical_pos(const struct editor *g, const char *p)
{
	/*
	 * == Logical byte offset of content pointer p ==
	 *
	 * Number of content bytes before p, excluding gap bytes.
	 */
	if (p <= g->gap_start)
		return (int)(p - g->text);
	return (int)(p - g->text) - (int)(g->gap_end - g->gap_start);
}

char *
phys_ptr(const struct editor *g, int n)
{
	/*
	 * == Physical pointer corresponding to logical byte offset n ==
	 *
	 * gap_start and gap_end map to the same logical offset (the gap has
	 * zero logical width), so at that exact boundary this canonically
	 * picks gap_end -- the gap_start side is content-shaped only in that
	 * *reading* through gap-aware helpers like buf_next steps off it
	 * immediately; a plain dereference of a pointer left resting there
	 * exposes the gap's zeroed interior. Every stored position that
	 * survives a gap move (dot, screenbegin, marks, registers) is
	 * restored via this function, so it must never hand back gap_start.
	 */
	int pre = (int)(g->gap_start - g->text);
	if (n < pre)
		return g->text + n;
	return g->text + n + (int)(g->gap_end - g->gap_start);
}
