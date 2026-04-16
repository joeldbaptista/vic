/*
 * status.c - status-line formatting and display.
 *
 * show_status_line() — renders the bottom status line.  If an explicit
 *                          message is queued (g->have_status_msg) it is shown
 *                          as-is and Hit-Return is prompted when it overflows.
 *                          Otherwise format_edit_status() builds a summary of
 *                          mode, filename, position, and modified state.
 *
 * status_line()      — printf-style, stores into g->status_buffer and sets
 *                          g->have_status_msg.
 * status_line_bold() — same, with reverse-video highlighting.
 * status_line_bold_errno() — appends strerror(errno).
 * not_implemented()  — convenience wrapper that quotes the command name.
 * hit_return()       — shows "[Hit return to continue]" and waits.
 *
 * Calls screen, term, and input directly (no hooks).
 */
#define _GNU_SOURCE
#include "status.h"

#include "input.h"
#include "line.h"
#include "screen.h"
#include "term.h"

#include <stdarg.h>
#include <string.h>

/*
 * Default status-line layout.  Change this string to customise what is shown.
 *
 * Tokens (all begin with '%'):
 *   %M   editor mode  (-, INSERT -, REPLACE -, VISUAL -, VISUAL LINE -)
 *   %F   current filename, or "No file"
 *   %R   " [Readonly]" when the buffer is read-only, empty otherwise
 *   %m   " [Modified]" when the buffer has unsaved changes, empty otherwise
 *   %c   current line number
 *   %t   total line count
 *   %p   scroll position as a percentage
 *   %%   a literal '%'
 */
#define STATUS_LINE_FORMAT "%M %F%R%m %c/%t %p%%"

#define Isprint(c) ((unsigned char)(c) >= ' ' && (unsigned char)(c) < ASCII_DEL)
#define ESC "\033"
#define ESC_BOLD_TEXT ESC "[7m"
#define ESC_NORM_TEXT ESC "[m"

enum {
	MAX_INPUT_LEN = 128,
};

void
hit_return(struct editor *g)
{
	int c;

	standout_start();
	fputs("[Hit return to continue]", stdout);
	standout_end();
	while ((c = get_one_char(g)) != '\n' && c != '\r')
		continue;
	redraw(g, TRUE);
}

static int
format_edit_status(struct editor *g)
{
	int cur, percent, trunc_at, len;
	const char *fmt;
	char *buf;

	cur = count_lines(g, g->text, g->dot);
	if (g->modified_count != g->last_modified_count) {
		g->status_tot = total_line_count(g);
		g->last_modified_count = g->modified_count;
	}

	if (g->status_tot > 0) {
		percent = (100 * cur) / g->status_tot;
	} else {
		cur = g->status_tot = 0;
		percent = 100;
	}

	trunc_at =
	    g->columns < STATUS_BUFFER_LEN - 1 ? g->columns : STATUS_BUFFER_LEN - 1;
	buf = g->status_buffer;
	len = 0;

	for (fmt = STATUS_LINE_FORMAT; *fmt && len < trunc_at; fmt++) {
		if (*fmt != '%') {
			buf[len++] = *fmt;
			continue;
		}
		switch (*++fmt) {
		case 'M': {
			const char *mode;
			if (g->visual_mode == 1)
				mode = "VISUAL -";
			else if (g->visual_mode == 2)
				mode = "VISUAL LINE -";
			else if (g->visual_mode == 3)
				mode = "VISUAL BLOCK -";
			else
				switch (g->cmd_mode & 3) {
				case 1:
					mode = "INSERT -";
					break;
				case 2:
					mode = "REPLACE -";
					break;
				default:
					mode = "-";
					break;
				}
			len += snprintf(buf + len, trunc_at - len + 1, "%s", mode);
			break;
		}
		case 'F':
			len += snprintf(buf + len, trunc_at - len + 1, "%s",
			                g->current_filename ? g->current_filename
			                                    : "No file");
			break;
		case 'R':
			if (g->readonly_mode)
				len += snprintf(buf + len, trunc_at - len + 1,
				                " [Readonly]");
			break;
		case 'm':
			if (g->modified_count)
				len += snprintf(buf + len, trunc_at - len + 1,
				                " [Modified]");
			break;
		case 'c':
			len += snprintf(buf + len, trunc_at - len + 1, "%d", cur);
			break;
		case 't':
			len += snprintf(buf + len, trunc_at - len + 1, "%d", g->status_tot);
			break;
		case 'p':
			len += snprintf(buf + len, trunc_at - len + 1, "%d", percent);
			break;
		case '%':
			buf[len++] = '%';
			break;
		default:
			buf[len++] = '%';
			if (len < trunc_at)
				buf[len++] = *fmt;
			break;
		}
	}
	buf[len] = '\0';
	return len < trunc_at ? len : trunc_at;
}

static int
bufsum(char *buf, int count)
{
	int sum = 0;
	char *e = buf + count;
	while (buf < e)
		sum += (unsigned char)*buf++;
	return sum;
}

void
show_status_line(struct editor *g)
{
	int cnt = 0, cksum = 0;

	if (!g->have_status_msg) {
		cnt = format_edit_status(g);
		cksum = bufsum(g->status_buffer, cnt);
	}
	if (g->have_status_msg || ((cnt > 0 && g->last_status_cksum != cksum))) {
		g->last_status_cksum = cksum;
		go_bottom_and_clear_to_eol(g);
		fputs(g->status_buffer, stdout);
		if (g->have_status_msg) {
			int n = (int)strlen(g->status_buffer) - (g->have_status_msg - 1);
			g->have_status_msg = 0;
			if (n >= 0 && n >= (int)g->columns)
				hit_return(g);
		}
		place_cursor(g, g->crow, g->ccol + (int)screen_line_number_width(g));
	}
	fflush(NULL);
}

void
status_line(struct editor *g, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintf(g->status_buffer, STATUS_BUFFER_LEN, format, args);
	va_end(args);

	g->have_status_msg = 1;
}

void
status_line_bold(struct editor *g, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	strcpy(g->status_buffer, ESC_BOLD_TEXT);
	vsnprintf(g->status_buffer + (sizeof(ESC_BOLD_TEXT) - 1),
	          STATUS_BUFFER_LEN - sizeof(ESC_BOLD_TEXT) - sizeof(ESC_NORM_TEXT),
	          format, args);
	strcat(g->status_buffer, ESC_NORM_TEXT);
	va_end(args);

	g->have_status_msg =
	    1 + (sizeof(ESC_BOLD_TEXT) - 1) + (sizeof(ESC_NORM_TEXT) - 1);
}

void
status_line_bold_errno(struct editor *g, const char *fn)
{
	status_line_bold(g, "'%s': %s", fn, strerror(errno));
}

static void
print_literal(char *buf, const char *s)
{
	char *d;
	unsigned char c;

	if (!s[0])
		s = "(NULL)";

	d = buf;
	for (; *s; s++) {
		c = *s;
		if ((c & UTF8_MULTIBYTE_MIN) && !Isprint(c))
			c = '?';
		if (c < ' ' || c == ASCII_DEL) {
			*d++ = '^';
			c |= '@';
			if (c == ASCII_DEL)
				c = '?';
		}
		*d++ = c;
		*d = '\0';
		if (d - buf > MAX_INPUT_LEN - 10)
			break;
	}
}

void
not_implemented(struct editor *g, const char *s)
{
	char buf[MAX_INPUT_LEN];
	print_literal(buf, s);
	status_line_bold(g, "'%s' is not implemented", buf);
}
