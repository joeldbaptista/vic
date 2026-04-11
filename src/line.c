/*
 * line.c - line and tabstop navigation.
 *
 * Pure buffer arithmetic; takes struct editor * for text/end bounds only.
 *
 *   begin_line   — pointer to the first byte of the line containing p
 *   end_line     — pointer to the newline terminating the line
 *   dollar_line  — pointer to the last non-newline byte of the line
 *   next_line    — pointer to the start of the following line
 *   prev_line    — pointer to the start of the preceding line
 *   find_line    — pointer to the start of line number li (1-based)
 *   count_lines  — number of newlines in [start, stop]
 *   total_line_count — total lines in the buffer (cached)
 *   next_tabstop — next tab column >= col given g->tabstop
 *   prev_tabstop — previous tab column <= col
 *   at_eof    — true when s points at the sentinel newline at g->end-1
 *
 * No hooks.  Safe to call from any module.
 */
#define _GNU_SOURCE
#include "line.h"

char *
begin_line(struct editor *g, char *p)
{
	/*
	 * == Pointer to the first byte of the line containing p ==
	 *
	 * Searches backward from p for the preceding '\n' and returns the byte
	 * immediately after it.  Returns g->text when p is on the first line.
	 */
	if (p > g->text) {
		p = memrchr(g->text, '\n', p - g->text);
		if (!p)
			return g->text;
		return p + 1;
	}
	return p;
}

char *
end_line(struct editor *g, char *p)
{
	/*
	 * == Pointer to the newline that terminates the line at p ==
	 *
	 * Searches forward from p for '\n'.  Returns the sentinel newline at
	 * g->end - 1 when the line has no earlier terminator.
	 */
	if (p < g->end - 1) {
		p = memchr(p, '\n', g->end - p - 1);
		if (!p)
			return g->end - 1;
	}
	return p;
}

char *
dollar_line(struct editor *g, char *p)
{
	/*
	 * == Pointer to the last non-newline byte of the line ($ position) ==
	 *
	 * On a non-empty line steps back one byte from the trailing newline.
	 * On an empty line (content is just '\n') stays at the newline itself
	 * so the cursor has somewhere to land.
	 */
	p = end_line(g, p);
	if (*p == '\n' && (p - begin_line(g, p)) > 0)
		p--;
	return p;
}

char *
prev_line(struct editor *g, char *p)
{
	/*
	 * == Pointer to the start of the preceding line ==
	 *
	 * Snaps to begin_line of the current line, steps back one byte into
	 * the preceding newline, then calls begin_line again.
	 * Returns g->text when already on the first line.
	 */
	p = begin_line(g, p);
	if (p > g->text && p[-1] == '\n')
		p--;
	p = begin_line(g, p);
	return p;
}

char *
next_line(struct editor *g, char *p)
{
	/*
	 * == Pointer to the start of the following line ==
	 *
	 * Moves to end_line and then steps past the newline.
	 * Returns end_line unchanged when already on the last line.
	 */
	p = end_line(g, p);
	if (p < g->end - 1 && *p == '\n')
		p++;
	return p;
}

char *
end_screen(struct editor *g)
{
	/*
	 * == Pointer to the end of the last visible screen line ==
	 *
	 * Walks forward (g->rows - 2) lines from g->screenbegin, then returns
	 * end_line of that position.  This is the bottom boundary of the
	 * visible window, used to clamp g->dot after a scroll.
	 */
	char *q;
	int cnt;

	q = g->screenbegin;
	for (cnt = 0; cnt < (int)g->rows - 2; cnt++)
		q = next_line(g, q);
	q = end_line(g, q);
	return q;
}

int
count_lines(struct editor *g, char *start, char *stop)
{
	/*
	 * == Count newlines in a buffer range ==
	 *
	 * Returns the number of lines spanned by [start, stop] by counting
	 * '\n' bytes.  Handles reversed start/stop automatically.
	 * Used for line-number computation, status bar, and range sizing.
	 */
	char *q;
	int cnt;

	if (stop < start) {
		q = start;
		start = stop;
		stop = q;
	}
	cnt = 0;
	stop = end_line(g, stop);
	while (start <= stop && start <= g->end - 1) {
		start = end_line(g, start);
		if (*start == '\n')
			cnt++;
		start++;
	}
	return cnt;
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
		if (g->text < g->end)
			g->line_count_cache = count_lines(g, g->text, g->end - 1);
		else
			g->line_count_cache = 0;
		g->line_count_cache_stamp = g->modified_count;
	}

	return g->line_count_cache;
}

char *
find_line(struct editor *g, int li)
{
	/*
	 * == Pointer to the start of line number li (1-based) ==
	 *
	 * Walks forward from g->text calling next_line (li - 1) times.
	 * Used by the nG motion and undo restore to jump to an absolute line.
	 */
	char *q;

	for (q = g->text; li > 1; li--)
		q = next_line(g, q);
	return q;
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
at_eof(struct editor *g, const char *s)
{
	/*
	 * == Test whether s is at the sentinel EOF newline ==
	 *
	 * The buffer always ends with a '\n' at g->end - 1.  Returns non-zero
	 * when s is that sentinel or the byte just before it when the file ends
	 * with a real newline.  Used by motions to avoid moving past the last
	 * writable position.
	 */
	return ((s == g->end - 2 && s[1] == '\n') || s == g->end - 1);
}

size_t
indent_len(struct editor *g, char *p)
{
	/*
	 * == Number of leading whitespace bytes on the line at p ==
	 *
	 * Counts blanks (space and tab) from p until the first non-blank or
	 * newline.  Used by autoindent, Ctrl-D de-indent, and the > / <
	 * operators to locate where visible content begins.
	 */
	char *r = p;

	(void)g;
	while (r < (g->end - 1) && isblank((unsigned char)*r))
		r++;
	return (size_t)(r - p);
}
