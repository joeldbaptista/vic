/*
 * codepoint.c - UTF-8 codepoint navigation and column arithmetic.
 *
 * Wraps the raw byte-stepping in utf8.c with buffer-aware helpers:
 *   cp_start  — snap an interior byte pointer back to the start of its
 * codepoint cp_next   — advance one codepoint (clamps at g->end) cp_prev   —
 * retreat one codepoint (clamps at g->text) cp_end    — one past the last byte
 * of the codepoint at p
 *
 * Also provides utf8_cell_width (terminal column width of one codepoint),
 * get_column (column offset of a buffer pointer), and next_column (advance
 * a column count past a character, respecting tab stops).
 *
 * No hooks — depends only on utf8.c and line.c.
 */
#define _XOPEN_SOURCE 700
#include "codepoint.h"

#include "line.h"
#include "utf8.h"

#include <wchar.h>

char *
cp_start(struct editor *g, char *p)
{
	/*
	 * == Snap pointer to the lead byte of its UTF-8 codepoint ==
	 *
	 * If p lands inside a multi-byte sequence (continuation byte 0x80–0xBF),
	 * walks backward to the lead byte.  Clamps to [g->text, g->end].
	 */
	if (p <= g->text)
		return g->text;
	if (p >= g->end)
		return g->end;
	if (((unsigned char)*p & 0xC0) != 0x80)
		return p;
	return (char *)stepbwd(p + 1, g->text);
}

char *
cp_next(struct editor *g, char *p)
{
	/*
	 * == Advance one UTF-8 codepoint ==
	 *
	 * Snaps p to its codepoint start, then steps forward via stepfwd.
	 * Clamps at g->end.
	 */
	p = cp_start(g, p);
	if (p >= g->end)
		return g->end;
	return (char *)stepfwd(p, g->end);
}

char *
cp_prev(struct editor *g, char *p)
{
	/*
	 * == Retreat one UTF-8 codepoint ==
	 *
	 * Steps p back to the lead byte of the preceding codepoint via stepbwd.
	 * Clamps at g->text.
	 */
	if (p <= g->text)
		return g->text;
	return (char *)stepbwd(p, g->text);
}

char *
cp_end(struct editor *g, char *p)
{
	/*
	 * == One past the last byte of the codepoint at p ==
	 *
	 * Thin wrapper around cp_next.  Callers that need the half-open
	 * endpoint [p, cp_end(p)) use this instead of cp_next directly for
	 * clarity.
	 */
	return cp_next(g, p);
}

int
utf8_cell_width(const char *p, const char *e)
{
	/*
	 * == Terminal column width of one UTF-8 codepoint ==
	 *
	 * Decodes the codepoint in [p, e) and queries wcwidth().  Returns 1
	 * for ASCII, control characters, surrogates, out-of-range values, and
	 * any codepoint wcwidth() deems non-printable.
	 * Used by screen.c and next_column to compute column positions.
	 */
	const unsigned char *s = (const unsigned char *)p;
	size_t len = (size_t)(e - p);
	uint32_t cp;
	int w;

	if (len == 0)
		return 1;

	if (s[0] < 0x80)
		cp = s[0];
	else if (len == 2 && (s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80)
		cp = (uint32_t)(s[0] & 0x1F) << 6 | (uint32_t)(s[1] & 0x3F);
	else if (len == 3 && (s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 &&
	         (s[2] & 0xC0) == 0x80)
		cp = (uint32_t)(s[0] & 0x0F) << 12 | (uint32_t)(s[1] & 0x3F) << 6 |
		     (uint32_t)(s[2] & 0x3F);
	else if (len == 4 && (s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
	         (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80)
		cp = (uint32_t)(s[0] & 0x07) << 18 | (uint32_t)(s[1] & 0x3F) << 12 |
		     (uint32_t)(s[2] & 0x3F) << 6 | (uint32_t)(s[3] & 0x3F);
	else
		return 1;

	if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return 1;

	w = wcwidth((wchar_t)cp);
	if (w < 0)
		return 1;
	return w;
}

int
next_column(struct editor *g, const char *p, int co)
{
	/*
	 * == Column number after advancing past the character at p ==
	 *
	 * Given the current column co, returns the column that follows the
	 * character at p.  Tabs snap to the next tab stop; control characters
	 * consume two columns ('^' + char); other codepoints use
	 * utf8_cell_width.
	 */
	unsigned char c = (unsigned char)*p;
	char *next;

	if (c == '\t')
		co = next_tabstop(g, co);
	else if ((unsigned char)c < ' ' || c == ASCII_DEL)
		co++;
	else {
		next = cp_next(g, (char *)p);
		co += utf8_cell_width(p, next) - 1;
	}
	return co + 1;
}

int
get_column(struct editor *g, char *p)
{
	/*
	 * == Column offset of buffer pointer p ==
	 *
	 * Walks from begin_line to p, accumulating column widths via
	 * next_column.  Returns 0 for the first column.
	 * Used by autoindent, the | motion, and status-line rendering.
	 */
	char *r;
	char *limit;
	int co = 0;

	limit = cp_start(g, p);
	for (r = begin_line(g, limit); r < limit; r = cp_next(g, r))
		co = next_column(g, r, co);
	return co;
}
