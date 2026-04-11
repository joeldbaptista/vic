/*
 * screen.c - screen rendering and refresh.
 *
 * Maintains a shadow screen buffer (g->screen) — one string per row —
 * and diffs it against the newly formatted content on each refresh to
 * minimise terminal writes.
 *
 * Key entry points:
 *   refresh()          — full or partial redraw; repositions screenbegin if
 *                        the cursor has scrolled off, then redraws changed
 *                        lines and moves the hardware cursor
 *   screen_erase()     — blank the shadow buffer (forces full repaint)
 *   new_screen()       — (re)allocate the shadow buffer after a resize
 *
 * Line formatting (format_line) renders one buffer line into a string,
 * inserting line numbers, visual-mode highlighting, tab expansion, and
 * non-printable escapes.  skip_line_to_offset handles horizontal scrolling.
 *
 * All upward calls (cursor sync, option queries, visual range, terminal
 * writes) go through screen_hooks.
 */
#include "screen.h"
#include "color.h"
#include <regex.h>

#include "codepoint.h"
#include "line.h"
#include "status.h"
#include "term.h"
#include "visual.h"

#define ESC "\033"
#define ESC_CLEAR2EOS ESC "[J"
#define ESC_SET_CURSOR_TOPLEFT ESC "[H"
#define ESC_BOLD_TEXT ESC "[7m"
#define ESC_NORM_TEXT ESC "[m"

/*
 * SGR escape sequences for each (in_visual, attr) state combination.
 * Every entry is a complete self-contained SGR sequence.
 * COMMENT = cyan (36), STRING = yellow (33), PREPROC = magenta (35),
 * KEYWORD = bold (1), NUMBER = green (32).
 */
static const char *const sgr_table[2][ATTR_COUNT] = {
    /* not in visual selection */
    {"\033[m", "\033[36m", "\033[33m", "\033[35m", "\033[1m", "\033[32m"},
    /* inside visual selection (reverse video + color) */
    {"\033[7m", "\033[7;36m", "\033[7;33m", "\033[7;35m", "\033[1;7m", "\033[7;32m"},
};

void
screen_erase(struct editor *g)
{
	/*
	 * == Clear the shadow screen buffer, forcing a full repaint next refresh ==
	 *
	 * Sets the first byte of every row in g->screen to '\0'.  On the next
	 * call to refresh(), every line will compare as changed and be redrawn.
	 */
	char *sp;
	int li;

	sp = g->screen;
	for (li = 0; li < (int)g->rows; li++) {
		sp[0] = '\0';
		sp += g->screen_line_size;
	}
}

void
new_screen(struct editor *g, int ro, int co)
{
	/*
	 * == Allocate or reallocate the shadow screen buffer ==
	 *
	 * Called at startup and on SIGWINCH.  Allocates ro rows * screen_line_size
	 * bytes (sized to hold co columns of expanded UTF-8 plus SCREEN_LINE_SLACK)
	 * and clears it.
	 */
	free(g->screen);
	g->screen_line_size = co * 4 + SCREEN_LINE_SLACK;
	g->screensize = ro * g->screen_line_size;
	g->screen = xmalloc(g->screensize);
	screen_erase(g);
}

unsigned
screen_line_number_width(struct editor *g)
{
	/*
	 * == Return the width in columns reserved for the line-number gutter ==
	 *
	 * Returns 0 when 'number' and 'relativenumber' are both off.  Otherwise
	 * computes the number of digits needed for the highest line number plus
	 * one column for the trailing space separator.
	 */
	int max_line;
	unsigned width;

	if (!(IS_NUMBER(g) || IS_RELATIVENUMBER(g)))
		return 0;

	max_line = total_line_count(g);
	for (width = 1; max_line >= 10; max_line /= 10)
		width++;

	return width + 1;
}

int
screen_text_columns_on_screen(struct editor *g)
{
	/*
	 * == Return the number of columns available for text content ==
	 *
	 * Subtracts the line-number gutter width from g->columns.  Returns at
	 * least 1 so there is always space for text.
	 */
	int text_cols = (int)g->columns - (int)screen_line_number_width(g);

	if (text_cols < 1)
		text_cols = 1;
	return text_cols;
}

static char *
skip_line_to_offset(struct editor *g, char *src, int ofs,
                    int *co)
{
	/*
	 * == Advance src past the first ofs display columns of the current line ==
	 *
	 * Used for horizontal scrolling (g->offset).  Updates *co with the
	 * actual column reached (may exceed ofs when a wide character straddles
	 * the boundary).  Tabs and multi-byte UTF-8 sequences are accounted for.
	 * Stops at the end of the buffer or at the line's newline.
	 */
	while (src < g->end && *src != '\n' && *co < ofs) {
		unsigned char c = (unsigned char)*src;

		if (c >= ' ' && c < ASCII_DEL && c != '\t') {
			char *run = src;
			int need = ofs - *co;

			while (run < g->end && need > 0) {
				unsigned char rc = (unsigned char)*run;
				if (rc == '\n' || rc == '\t' || rc < ' ' || rc >= ASCII_DEL)
					break;
				run++;
				need--;
			}

			*co += run - src;
			src = run;
			continue;
		}

		if (c == '\t') {
			*co = next_tabstop(g, *co) + 1;
			src = cp_next(g, src);
			continue;
		}

		if (c < ' ' || c == ASCII_DEL) {
			*co += 2;
			src = cp_next(g, src);
			continue;
		}

		{
			char *next = cp_next(g, src);
			int width = utf8_cell_width(src, next);
			if (width <= 0)
				width = 1;
			*co += width;
			src = next;
		}
	}

	return src;
}

static char *
render_lnum(struct editor *g, char *dest, char *src,
            int line_no, int cur_line, unsigned lnum_width)
{
	/*
	 * == Write the line-number gutter into dest[0..lnum_width-1] ==
	 *
	 * Renders the absolute line number (or relative distance when
	 * 'relativenumber' is set), right-justified in a field of lnum_width-1
	 * followed by a space.  Past-end rows (src >= g->end) are left blank.
	 * Returns dest + lnum_width.
	 */
	if (lnum_width == 0)
		return dest;
	memset(dest, ' ', lnum_width);
	if (src < g->end) {
		int shown = line_no;

		if (IS_RELATIVENUMBER(g)) {
			int rel = cur_line - line_no;
			if (rel < 0)
				rel = -rel;
			if (!IS_NUMBER(g) || rel != 0)
				shown = rel;
		}
		snprintf(dest, (size_t)lnum_width + 1, "%*d ", (int)lnum_width - 1,
		         shown);
	}
	return dest + lnum_width;
}

static int
scan_line_colors(const struct colorizer *colorizer, char *src, char *end,
                 char *attrs, int *color_state)
{
	/*
	 * == Colorize one source line and populate attrs[] ==
	 *
	 * Calls the colorizer's colorize() function on the slice [src, end).
	 * Each byte in attrs[] receives one of the ATTR_* constants.  Updates
	 * *color_state with the end-of-line colorizer state (needed because
	 * multi-line constructs such as block comments span line boundaries).
	 * Returns the number of bytes scanned (up to VI_MAX_LINE).
	 */
	int len = 0;
	char *p = src;

	while (p < end && *p != '\n' && len < VI_MAX_LINE) {
		p++;
		len++;
	}
	*color_state = colorizer->colorize(*color_state, src, len, attrs);
	return len;
}

static void
scan_line_highlight_attrs(const regex_t *re, const char *src, int len,
                          char *attrs)
{
	/*
	 * == Mark bytes inside /hlsearch matches on this line ==
	 *
	 * Copies [src, src+len) to a local NUL-terminated buffer, then runs
	 * regexec in a forward loop, setting attrs[i]=1 for every byte that
	 * falls within a match.  Zero-length matches advance by one byte to
	 * avoid infinite loops.  Results overlay syntax-color attrs so
	 * highlights take priority in the SGR selection logic.
	 */
	char buf[VI_MAX_LINE + 1];
	regmatch_t m;
	int pos = 0;

	if (len > VI_MAX_LINE)
		len = VI_MAX_LINE;
	memcpy(buf, src, (size_t)len);
	buf[len] = '\0';
	memset(attrs, 0, (size_t)len);

	while (pos < len) {
		int flags = pos > 0 ? REG_NOTBOL : 0;
		if (regexec(re, buf + pos, 1, &m, flags) != 0)
			break;
		int start = pos + (int)m.rm_so;
		int end = pos + (int)m.rm_eo;
		if (end <= start) {
			pos = start + 1;
			continue;
		}
		if (end > len)
			end = len;
		memset(attrs + start, 1, (size_t)(end - start));
		pos = end;
	}
}

static char *
format_line(struct editor *g, char *src, int line_no, int cur_line,
            unsigned lnum_width, int text_cols,
            const struct colorizer *colorizer,
            int *color_state, const regex_t *hl_re)
{
	/*
	 * == Format one buffer line into the screen output buffer ==
	 *
	 * Renders the line at src into g->scr_out_buf as a displayable string
	 * (NUL-terminated, at most text_cols display columns wide) with:
	 *   - line-number gutter via render_lnum
	 *   - horizontal scroll via skip_line_to_offset
	 *   - tab expansion (each tab fills to the next tabstop)
	 *   - control-character escaping (^X notation)
	 *   - visual-selection, search-highlight, and syntax-color SGR sequences
	 *   - trailing space padding to text_cols
	 *
	 * Past-end rows render as a single '~' in the gutter column.
	 * Returns g->scr_out_buf.
	 */
	unsigned char c;
	int co;
	int shown_cols;
	int ofs = g->offset;
	char *vstart = NULL;
	char *vstop = NULL;
	int vbuftype;
	int cur_in_visual = 0;
	int cur_in_hl = 0;
	int cur_attr = ATTR_NORMAL;
	int have_visual = visual_get_range(g, &vstart, &vstop, &vbuftype);
	(void)vbuftype;
	char *dest = g->scr_out_buf;
	char *dest_end = dest + sizeof(g->scr_out_buf) - 1;
	char *line_start = src;
	char line_attrs[VI_MAX_LINE];
	char hl_attrs[VI_MAX_LINE];
	int line_len = 0;

	dest = render_lnum(g, dest, src, line_no, cur_line, lnum_width);

	if (src >= g->end) {
		if (dest < dest_end)
			*dest++ = '~';
		*dest = '\0';
		return g->scr_out_buf;
	}

	if (colorizer) {
		line_len = scan_line_colors(colorizer, line_start, g->end,
		                            line_attrs, color_state);
		/* Emit a reset at the start of each colored line. */
		if (dest + 3 <= dest_end) {
			memcpy(dest, "\033[m", 3);
			dest += 3;
		}
	}
	if (hl_re) {
		if (line_len == 0) {
			const char *p = line_start;
			while (p < g->end && *p != '\n' && line_len < VI_MAX_LINE) {
				p++;
				line_len++;
			}
		}
		if (line_len > 0)
			scan_line_highlight_attrs(hl_re, line_start, line_len, hl_attrs);
	}

	co = 0;
	src = skip_line_to_offset(g, src, ofs, &co);

	shown_cols = 0;
	while (shown_cols < text_cols) {
		char *next;
		int width;
		int new_visual;
		int new_hl;
		int new_attr;
		int byte_off;

		if (src >= g->end)
			break;
		next = cp_next(g, src);
		c = (unsigned char)*src;
		if (c == '\n')
			break;

		new_visual = (have_visual && src >= vstart && src <= vstop) ? 1 : 0;
		byte_off = (int)(src - line_start);
		new_hl = (!new_visual && hl_re && byte_off < line_len &&
		          hl_attrs[byte_off]) ? 1 : 0;
		new_attr =
		    (colorizer && byte_off < line_len) ? line_attrs[byte_off] : ATTR_NORMAL;

		if (new_visual != cur_in_visual || new_hl != cur_in_hl ||
		    new_attr != cur_attr) {
			const char *sgr;
			if (new_visual)
				sgr = sgr_table[1][new_attr];
			else if (new_hl)
				sgr = "\033[43m";
			else
				sgr = sgr_table[0][new_attr];
			size_t n = strlen(sgr);
			if (dest + n <= dest_end) {
				memcpy(dest, sgr, n);
				dest += n;
			}
			cur_in_visual = new_visual;
			cur_in_hl = new_hl;
			cur_attr = new_attr;
		}

		if (c == '\t') {
			int tabw = next_tabstop(g, co) - co + 1;
			int i;
			for (i = 0; i < tabw && shown_cols < text_cols; i++, co++) {
				if (co < ofs)
					continue;
				if (dest < dest_end)
					*dest++ = ' ';
				shown_cols++;
			}
			src = next;
			continue;
		}

		if (c < ' ' || c == ASCII_DEL) {
			char ctrl[2];
			int i;
			ctrl[0] = '^';
			ctrl[1] = (c == ASCII_DEL) ? '?' : (char)(c + '@');
			for (i = 0; i < 2 && shown_cols < text_cols; i++, co++) {
				if (co < ofs)
					continue;
				if (dest < dest_end)
					*dest++ = ctrl[i];
				shown_cols++;
			}
			src = next;
			continue;
		}

		width = utf8_cell_width(src, next);
		if (width <= 0) {
			if (co > ofs && shown_cols > 0) {
				size_t blen = (size_t)(next - src);
				if (dest + blen <= dest_end) {
					memcpy(dest, src, blen);
					dest += blen;
				}
			}
			src = next;
			continue;
		}

		if (co + width <= ofs) {
			co += width;
			src = next;
			continue;
		}
		if (co < ofs) {
			co += width;
			src = next;
			continue;
		}
		if (shown_cols + width > text_cols)
			break;

		if (dest + (next - src) <= dest_end) {
			memcpy(dest, src, (size_t)(next - src));
			dest += (next - src);
		}
		co += width;
		shown_cols += width;
		src = next;
	}

	/* Reset any active SGR before padding. */
	if (cur_in_visual || cur_in_hl || cur_attr != ATTR_NORMAL || colorizer) {
		if (dest + 3 <= dest_end) {
			memcpy(dest, "\033[m", 3);
			dest += 3;
		}
	}

	if (shown_cols < text_cols) {
		int spaces = text_cols - shown_cols;
		while (spaces-- > 0 && dest < dest_end)
			*dest++ = ' ';
	}

	*dest = '\0';
	return g->scr_out_buf;
}

static int
highlight_pattern_hash(const char *pat)
{
	/*
	 * == Cheap hash of the highlight pattern string ==
	 *
	 * Used to detect when the pattern changes so the screen is force-redrawn.
	 * djb2 variant; returns 0 for NULL.
	 */
	int h = 5381;
	if (!pat)
		return 0;
	for (; *pat; pat++)
		h = h * 31 + (unsigned char)*pat;
	return h;
}

void
refresh(struct editor *g, int full_screen)
{
	/*
	 * == Redraw the screen, writing only changed lines ==
	 *
	 * Queries terminal dimensions, syncs the cursor row/column, and formats
	 * each visible line via format_line.  Lines are compared to the shadow
	 * buffer (g->screen); only differing lines are emitted to stdout.
	 *
	 * - full_screen=TRUE forces all lines to be redrawn (used after :e,
	 *   window resize, or Ctrl-L).
	 * - Returns early (cursor move only) when nothing has changed and
	 *   visual/relative-number modes are off.
	 * - Compiles the highlight regex once per refresh for hlsearch.
	 * - Pre-scans lines above screenbegin to seed the colorizer state.
	 */
	int li;
	int text_cols;
	int line_no = 0;
	int cur_line = 0;
	unsigned lnum_width;
	char *tp;
	char *sp;
	const struct colorizer *colorizer;
	int color_state;
	regex_t hl_re;
	regex_t *hl_rep = NULL;
	int hl_hash;

	if (!g->get_rowcol_error) {
		unsigned c = g->columns;
		unsigned r = g->rows;
		query_screen_dimensions(g);
		full_screen |= (c - g->columns) | (r - g->rows);
	}
	sync_cursor(g, g->dot, &g->crow, &g->ccol);
	lnum_width = screen_line_number_width(g);
	text_cols = screen_text_columns_on_screen(g);

	hl_hash = highlight_pattern_hash(g->highlight_pattern);
	if (hl_hash != g->refresh_last_highlight_hash)
		full_screen = TRUE;

	if (!full_screen && g->screenbegin == g->refresh_last_screenbegin &&
	    g->offset == g->scr_old_offset &&
	    g->modified_count == g->refresh_last_modified_count && g->undo_q == 0 &&
	    g->visual_mode == 0 && !IS_RELATIVENUMBER(g)) {
		place_cursor(g, g->crow, g->ccol + (int)lnum_width);
		if (!g->keep_index)
			g->cindex = g->ccol + g->offset;
		return;
	}

	/* Compile highlight pattern if one is active. */
	if (g->highlight_pattern && g->highlight_pattern[0] != '\0') {
		int cflags = REG_NEWLINE;
		if (IS_IGNORECASE(g))
			cflags |= REG_ICASE;
		if (regcomp(&hl_re, g->highlight_pattern, cflags) == 0)
			hl_rep = &hl_re;
	}

	if (lnum_width != 0) {
		line_no = count_lines(g, g->text, g->screenbegin);
		cur_line = count_lines(g, g->text, g->dot);
	}
	tp = g->screenbegin;

	/* Set up syntax colorizer; pre-scan to get state at the first visible line.
	 * Call colorize per line with NULL attrs — we only need the returned state.
	 */
	colorizer = colorizer_find(g->current_filename);
	color_state = 0;
	if (colorizer) {
		char *p = g->text;
		while (p < g->screenbegin && p < g->end) {
			char *eol = memchr(p, '\n', (size_t)(g->end - p));
			int llen;
			if (eol == NULL || eol >= g->screenbegin)
				eol = g->screenbegin;
			llen = (int)(eol - p);
			if (llen > VI_MAX_LINE)
				llen = VI_MAX_LINE;
			color_state = colorizer->colorize(color_state, p, llen, NULL);
			p = eol + 1;
		}
	}

	for (li = 0; li < (int)g->rows - 1; li++) {
		char *out_buf;

		out_buf = format_line(g, tp, line_no, cur_line, lnum_width, text_cols,
		                      colorizer, &color_state, hl_rep);

		if (tp < g->end) {
			char *t = memchr(tp, '\n', g->end - tp);
			if (!t)
				t = g->end - 1;
			tp = t + 1;
			line_no++;
		}

		sp = &g->screen[li * g->screen_line_size];
		if (full_screen || g->offset != g->scr_old_offset || strcmp(sp, out_buf) != 0) {
			strncpy(sp, out_buf, g->screen_line_size - 1);
			sp[g->screen_line_size - 1] = '\0';
			place_cursor(g, li, 0);
			fputs(sp, stdout);
			clear_to_eol();
		}
	}

	if (hl_rep)
		regfree(&hl_re);

	place_cursor(g, g->crow, g->ccol + (int)lnum_width);

	if (!g->keep_index)
		g->cindex = g->ccol + g->offset;

	g->refresh_last_screenbegin = g->screenbegin;
	g->refresh_last_modified_count = g->modified_count;
	g->refresh_last_highlight_hash = hl_hash;
	g->scr_old_offset = g->offset;
}

void
redraw(struct editor *g, int full_screen)
{
	/*
	 * == Clear the terminal and redraw everything ==
	 *
	 * Moves to the top-left, clears to end-of-screen, invalidates the
	 * shadow buffer, and calls refresh + show_status_line.  Called for
	 * Ctrl-L (hard refresh) and after shell commands.
	 */
	fputs(ESC_SET_CURSOR_TOPLEFT ESC_CLEAR2EOS, stdout);
	screen_erase(g);
	g->last_status_cksum = 0;
	refresh(g, full_screen);
	show_status_line(g);
}