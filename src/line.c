/*
 * line.c - line and tabstop navigation.
 *
 * Pure buffer arithmetic; takes struct editor * for text/end bounds only.
 *
 *   begin_line   — offset of the first byte of the line containing p
 *   end_line     — offset of the newline terminating the line
 *   dollar_line  — offset of the last non-newline byte of the line
 *   next_line    — offset of the start of the following line
 *   prev_line    — offset of the start of the preceding line
 *   find_line    — offset of the start of line number li (1-based)
 *   count_lines  — number of newlines in [start, stop]
 *   total_line_count — total lines in the buffer (cached)
 *   next_tabstop — next tab column >= col given g->tabstop
 *   prev_tabstop — previous tab column <= col
 *   at_eof    — true when s points at the sentinel newline at text_len() - 1
 *
 * No hooks.  Safe to call from any module.
 */
#define _GNU_SOURCE
#include "line.h"

#include "text.h"

int
begin_line(struct editor *g, int p)
{
	/*
	 * == Pointer to the first byte of the line containing p ==
	 *
	 * Searches backward from p for the preceding '\n' and returns the byte
	 * immediately after it.  Returns offset 0 when p is on the first line.
	 */
	if (p > text_len(g))
		p = text_len(g);
	if (p > 0)
		return text_rfind(g, p, '\n') + 1;
	return p;
}

int
end_line(struct editor *g, int p)
{
	/*
	 * == Pointer to the newline that terminates the line at p ==
	 *
	 * Searches forward from p for '\n'.  Returns the sentinel newline at
	 * text_len() - 1 when the line has no earlier terminator.
	 */
	int end;
	int q;

	end = text_len(g);
	if (p < end - 1) {
		q = text_find(g, p, '\n');
		if (q < 0 || q >= end - 1)
			return end - 1;
		return q;
	}
	return p;
}

int
dollar_line(struct editor *g, int p)
{
	/*
	 * == Pointer to the last non-newline byte of the line ($ position) ==
	 *
	 * On a non-empty line steps back one byte from the trailing newline.
	 * On an empty line (content is just '\n') stays at the newline itself
	 * so the cursor has somewhere to land.
	 */
	p = end_line(g, p);
	if (text_char(g, p) == '\n' && (p - begin_line(g, p)) > 0)
		p--;
	return p;
}

int
prev_line(struct editor *g, int p)
{
	/*
	 * == Pointer to the start of the preceding line ==
	 *
	 * Snaps to begin_line of the current line, steps back one byte into
	 * the preceding newline, then calls begin_line again.
	 * Returns offset 0 when already on the first line.
	 */
	p = begin_line(g, p);
	if (p > 0 && text_char(g, p - 1) == '\n')
		p--;
	p = begin_line(g, p);
	return p;
}

int
next_line(struct editor *g, int p)
{
	/*
	 * == Pointer to the start of the following line ==
	 *
	 * Moves to end_line and then steps past the newline.
	 * Returns end_line unchanged when already on the last line.
	 */
	p = end_line(g, p);
	if (p < text_len(g) - 1 && text_char(g, p) == '\n')
		p++;
	return p;
}

int
end_screen(struct editor *g)
{
	/*
	 * == Pointer to the end of the last visible screen line ==
	 *
	 * Walks forward (g->rows - 2) lines from g->screenbegin, then returns
	 * end_line of that position.  This is the bottom boundary of the
	 * visible window, used to clamp g->dot after a scroll.
	 */
	int q;
	int cnt;

	q = g->screenbegin;
	for (cnt = 0; cnt < (int)g->rows - 2; cnt++)
		q = next_line(g, q);
	q = end_line(g, q);
	return q;
}

int
count_lines(struct editor *g, int start, int stop)
{
	/*
	 * == Count newlines in a buffer range ==
	 *
	 * Returns the number of lines spanned by [start, stop] by counting
	 * '\n' bytes.  Handles reversed start/stop automatically.
	 * Used for line-number computation, status bar, and range sizing.
	 */
	int q;

	if (stop < start) {
		q = start;
		start = stop;
		stop = q;
	}
	if (start > text_len(g) - 1)
		return 0;
	/*
	 * The old loop counted the newlines in [start, end_line(stop)];
	 * the line index of the piece tree gives that count directly.
	 */
	stop = end_line(g, stop);
	return text_line_of(g, stop + 1) - text_line_of(g, start);
}

int
total_line_count(struct editor *g)
{
	/*
	 * == Total line count for the buffer (cached) ==
	 *
	 * Recomputes only when g->modified_count has changed since the last
	 * call.  Used by the status bar and screen.c to avoid a full-buffer
	 * scan on every redraw.
	 */
	if (g->line_count_cache_stamp != g->modified_count) {
		if (text_len(g) > 0)
			g->line_count_cache = count_lines(g, 0, text_len(g) - 1);
		else
			g->line_count_cache = 0;
		g->line_count_cache_stamp = g->modified_count;
	}

	return g->line_count_cache;
}

int
find_line(struct editor *g, int li)
{
	/*
	 * == Offset of the start of line number li (1-based) ==
	 *
	 * A line past the end gives the line that holds the last byte, as
	 * stepping next_line() li - 1 times from offset 0 would; the line
	 * index of the piece tree jumps there directly.  Used by the nG
	 * motion and undo restore to jump to an absolute line.
	 */
	int last;

	if (li <= 1 || text_len(g) == 0)
		return 0;
	last = text_line_of(g, text_len(g) - 1);
	if (li - 1 > last)
		li = last + 1;
	return text_line_start(g, li - 1);
}

int
next_tabstop(struct editor *g, int col)
{
	/*
	 * == Column of the next tab stop after col ==
	 *
	 * Result is always strictly greater than col.  Used when rendering
	 * tab characters and when expanding tabs during insert mode.
	 */
	return col + ((g->tabstop - 1) - (col % g->tabstop));
}

int
prev_tabstop(struct editor *g, int col)
{
	/*
	 * == Column of the previous tab stop before col ==
	 *
	 * Returns the largest tab-stop column strictly less than col.
	 * Returns 0 when col is already at or before the first tab stop.
	 * Used by Ctrl-D de-indent in insert mode.
	 */
	return col - ((col % g->tabstop) ?: g->tabstop);
}

int
at_eof(struct editor *g, int s)
{
	/*
	 * == Test whether s is at the sentinel EOF newline ==
	 *
	 * The buffer always ends with a '\n' at text_len() - 1.  Returns non-zero
	 * when s is that sentinel or the byte just before it when the file ends
	 * with a real newline.  Used by motions to avoid moving past the last
	 * writable position.
	 */
	int end;

	end = text_len(g);
	return ((s == end - 2 && text_char(g, s + 1) == '\n') || s == end - 1);
}

size_t
indent_len(struct editor *g, int p)
{
	/*
	 * == Number of leading whitespace bytes on the line at p ==
	 *
	 * Counts blanks (space and tab) from p until the first non-blank or
	 * newline.  Used by autoindent, Ctrl-D de-indent, and the > / <
	 * operators to locate where visible content begins.
	 */
	int r = p;

	while (r < (text_len(g) - 1) && isblank(text_char(g, r)))
		r++;
	return (size_t)(r - p);
}
