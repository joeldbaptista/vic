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
	p = end_line(g, p);
	if (*p == '\n' && (p - begin_line(g, p)) > 0)
		p--;
	return p;
}

char *
prev_line(struct editor *g, char *p)
{
	p = begin_line(g, p);
	if (p > g->text && p[-1] == '\n')
		p--;
	p = begin_line(g, p);
	return p;
}

char *
next_line(struct editor *g, char *p)
{
	p = end_line(g, p);
	if (p < g->end - 1 && *p == '\n')
		p++;
	return p;
}

char *
end_screen(struct editor *g)
{
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
	char *q;

	for (q = g->text; li > 1; li--)
		q = next_line(g, q);
	return q;
}

int
next_tabstop(struct editor *g, int col)
{
	return col + ((g->tabstop - 1) - (col % g->tabstop));
}

int
prev_tabstop(struct editor *g, int col)
{
	return col - ((col % g->tabstop) ?: g->tabstop);
}

int
at_eof(struct editor *g, const char *s)
{
	return ((s == g->end - 2 && s[1] == '\n') || s == g->end - 1);
}

size_t
indent_len(struct editor *g, char *p)
{
	char *r = p;

	(void)g;
	while (r < (g->end - 1) && isblank((unsigned char)*r))
		r++;
	return (size_t)(r - p);
}
