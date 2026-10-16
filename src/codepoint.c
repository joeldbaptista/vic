/*
 * codepoint.c - UTF-8 codepoint navigation and column arithmetic.
 *
 * Wraps the raw byte-stepping in utf8.c with buffer-aware helpers:
 *   cp_start  — snap an interior byte offset back to the start of its
 * codepoint cp_next   — advance one codepoint (clamps at text_len()) cp_prev   —
 * retreat one codepoint (clamps at offset 0) cp_end    — one past the last byte
 * of the codepoint at p
 *
 * Also provides utf8_cell_width (terminal column width of one codepoint),
 * get_column (column offset of a buffer offset), and next_column (advance
 * a column count past a character, respecting tab stops).
 *
 * csi_len, esc_skip and esc_snap_fwd/esc_snap_bwd describe terminal CSI
 * escape sequences: with the 'color-escape' display mode active such a
 * sequence occupies no column, so every loop that accumulates columns steps
 * over it via esc_skip, and the cursor motions place the cursor outside it
 * via esc_snap_fwd/esc_snap_bwd.
 *
 * No hooks — depends only on utf8.c and line.c.
 */
#define _XOPEN_SOURCE 700
#include "codepoint.h"

#include "line.h"
#include "text.h"
#include "utf8.h"

#include <wchar.h>

/* longest CSI sequence that csi_len_at() recognises inside the text */
#define CSI_WINDOW 256

static int
step_fwd(struct editor *g, int p, int e)
{
	/*
	 * == stepfwd() on the text: offset after the codepoint at p ==
	 *
	 * stepfwd() reads at most 4 bytes, so a 4-byte window that ends at
	 * e gives the same answer as the old pointer call.
	 */
	char b[4];
	int n;

	if (p >= e)
		return e;
	n = text_read(g, p, e - p < 4 ? e - p : 4, b);
	return p + (int)(stepfwd(b, b + n) - b);
}

static int
step_bwd(struct editor *g, int p, int s)
{
	/*
	 * == stepbwd() on the text: start of the codepoint before p ==
	 *
	 * Steps back over continuation bytes, without a limit, exactly as
	 * stepbwd() does, but never before s.
	 */
	if (p <= s)
		return s;
	for (--p; p > s && (text_char(g, p) & 0xC0) == 0x80; --p)
		;
	return p;
}

static int
csi_len_at(struct editor *g, int p, int *is_sgr)
{
	/*
	 * == csi_len() of the CSI sequence starting at offset p ==
	 *
	 * Copies up to CSI_WINDOW bytes, so a sequence longer than that is
	 * not recognised; no colourising tool emits one.
	 */
	char b[CSI_WINDOW];
	int n;

	if (text_char(g, p) != ASCII_ESC)
		return 0;
	n = text_read(g, p, CSI_WINDOW, b);
	return csi_len(b, b + n, is_sgr);
}

int
cp_start(struct editor *g, int p)
{
	/*
	 * == Snap an offset to the lead byte of its UTF-8 codepoint ==
	 *
	 * If p lands inside a multi-byte sequence (continuation byte 0x80–0xBF),
	 * walks backward to the lead byte.  Clamps to [0, text_len()].
	 */
	if (p <= 0)
		return 0;
	if (p >= text_len(g))
		return text_len(g);
	if ((text_char(g, p) & 0xC0) != 0x80)
		return p;
	return step_bwd(g, p + 1, 0);
}

static int
esc_prev_esc(struct editor *g, int p, int bol)
{
	/*
	 * == Nearest ESC byte at or before p, within the lookback window ==
	 *
	 * Escape sequences never nest, so only the closest preceding ESC can
	 * begin a sequence that covers p.  The search stops at the start of the
	 * line and after ESC_SGR_MAX bytes, which bounds the cost for ordinary
	 * text that contains no escapes at all.  Returns -1 when none is
	 * found.
	 */
	int q;
	int lim = (p - bol > ESC_SGR_MAX) ? p - ESC_SGR_MAX : bol;

	for (q = p;; q--) {
		if (text_char(g, q) == ASCII_ESC)
			return q;
		if (q == lim)
			return -1;
	}
}

static int
esc_run(struct editor *g, int p, int *start, int *end)
{
	/*
	 * == Bounds of the escape-sequence run covering p ==
	 *
	 * With 'color-escape' active a CSI sequence occupies no display column,
	 * so the cursor must never rest inside one: stepping onto it would leave
	 * the cursor visibly frozen for as many keystrokes as the sequence has
	 * bytes.  The cursor motions therefore treat a run of adjacent sequences
	 * as one indivisible unit, which this function delimits.
	 *
	 * Returns 1 when p is at or inside such a run, writing its half-open
	 * bounds to *start and *end; returns 0 when p is ordinary text, and
	 * always when the mode is off.
	 *
	 * The lookback is capped at ESC_SGR_MAX bytes, so a single sequence
	 * longer than that is not recognised from its interior.  No sequence a
	 * colourising tool emits comes close to the cap.
	 */
	int bol, s, q;
	int n;

	if (!g->color_escape || p >= text_len(g))
		return 0;

	bol = begin_line(g, p);

	/* Does a sequence cover p? */
	s = -1;
	q = esc_prev_esc(g, p, bol);
	if (q >= 0) {
		n = csi_len_at(g, q, NULL);
		if (n > 0 && p < q + n)
			s = q;
	}
	if (s < 0)
		return 0;

	/* Extend backward over sequences that end exactly where s begins. */
	while (s > bol) {
		q = esc_prev_esc(g, s - 1, bol);
		if (q < 0)
			break;
		n = csi_len_at(g, q, NULL);
		if (n <= 0 || q + n != s)
			break;
		s = q;
	}

	/* Extend forward over the sequences that follow. */
	for (q = s; (n = csi_len_at(g, q, NULL)) > 0;)
		q += n;

	*start = s;
	*end = q;
	return 1;
}

int
cp_next(struct editor *g, int p)
{
	/*
	 * == Advance one UTF-8 codepoint ==
	 *
	 * Snaps p to its codepoint start, then steps forward via stepfwd.
	 * Clamps at text_len().
	 *
	 * When p is a continuation byte inside an incomplete or invalid sequence,
	 * cp_start() retreats to the lead byte and stepfwd() may return a position
	 * at or before the original p.  Guard against that to ensure callers
	 * always make progress.
	 *
	 * Callers rely on cp_next(p) - p being the byte length of the codepoint
	 * at p, so this steps exactly one codepoint even in 'color-escape'
	 * mode.  Skipping a whole escape run is the cursor's concern, and
	 * belongs to esc_snap_fwd / esc_snap_bwd.
	 */
	int orig = p;
	int n;
	int end = text_len(g);

	p = cp_start(g, p);
	if (p >= end)
		return end;
	n = step_fwd(g, p, end);
	if (n <= orig)
		return orig < end ? orig + 1 : end;
	return n;
}

int
cp_prev(struct editor *g, int p)
{
	/*
	 * == Retreat one UTF-8 codepoint ==
	 *
	 * Steps p back to the lead byte of the preceding codepoint via stepbwd.
	 * Clamps at offset 0.  As with cp_next, escape runs are not treated
	 * specially here — see esc_snap_bwd.
	 */
	if (p <= 0)
		return 0;
	return step_bwd(g, p, 0);
}

int
esc_snap_fwd(struct editor *g, int p)
{
	/*
	 * == Move p out of any escape run it lies in, forwards ==
	 *
	 * Unlike esc_skip, this recognises a position anywhere inside a run,
	 * not only its first byte, so it is the one to use when placing the
	 * cursor.  It costs a bounded backward scan, which is why the per-byte
	 * column loops use esc_skip instead.
	 *
	 * Returns p unchanged when the mode is off or p is ordinary text.
	 */
	int rs, re;

	return esc_run(g, p, &rs, &re) ? re : p;
}

int
esc_snap_bwd(struct editor *g, int p)
{
	/*
	 * == Move p out of any escape run it lies in, backwards ==
	 *
	 * Returns the previous visible codepoint, so a leftward motion that
	 * lands in a run carries on past it instead of stalling for as many
	 * keystrokes as the run has bytes.  When the run begins the line there
	 * is nothing visible before it, so its own start is returned — the
	 * column the following character occupies.
	 *
	 * Returns p unchanged when the mode is off or p is ordinary text.
	 */
	int rs, re;

	if (!esc_run(g, p, &rs, &re))
		return p;
	if (rs <= 0 || text_char(g, rs - 1) == '\n')
		return rs;
	return step_bwd(g, rs, 0);
}

int
cp_end(struct editor *g, int p)
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
csi_len(const char *p, const char *end, int *is_sgr)
{
	/*
	 * == Byte length of the CSI escape sequence starting at p ==
	 *
	 * A CSI sequence is ESC '[', then parameter bytes (0x30-0x3f), then
	 * intermediate bytes (0x20-0x2f), then one final byte (0x40-0x7e).
	 * Returns 0 when p does not begin a complete sequence within [p, end),
	 * so a truncated tail at the end of the buffer stays visible as text.
	 *
	 * A newline belongs to none of the three byte classes, so the scan
	 * always stops at end of line and a sequence never spans lines.
	 *
	 * *is_sgr receives 1 when the final byte is 'm' — the Select Graphic
	 * Rendition sequence, the only kind that carries colour.  It is set on
	 * every non-zero return, and left untouched on a zero return.
	 */
	const char *q;

	if (p + 1 >= end || (unsigned char)*p != ASCII_ESC || p[1] != '[')
		return 0;
	q = p + 2;
	while (q < end && (unsigned char)*q >= CSI_PARAM_BYTE_MIN &&
	       (unsigned char)*q <= CSI_PARAM_BYTE_MAX)
		q++;
	while (q < end && (unsigned char)*q >= CSI_INTER_BYTE_MIN &&
	       (unsigned char)*q <= CSI_INTER_BYTE_MAX)
		q++;
	if (q >= end || (unsigned char)*q < CSI_FINAL_BYTE_MIN ||
	    (unsigned char)*q > CSI_FINAL_BYTE_MAX)
		return 0;
	if (is_sgr)
		*is_sgr = (*q == 'm');
	return (int)(q + 1 - p);
}

int
esc_skip(struct editor *g, int p)
{
	/*
	 * == Advance past a zero-width escape sequence at p ==
	 *
	 * With 'color-escape' active a CSI sequence occupies no display column,
	 * so every loop that accumulates columns must step over it rather than
	 * count its bytes.  Returns the position just past the run of sequences
	 * beginning at p, or p unchanged when the mode is off or p starts no
	 * complete sequence.
	 *
	 * Only p as a run start is recognised — cheap enough for the per-byte
	 * column loops, and sufficient there because cp_next never leaves a
	 * position inside a run.
	 */
	int n;

	if (!g->color_escape)
		return p;
	while ((n = csi_len_at(g, p, NULL)) > 0)
		p += n;
	return p;
}

int
next_column(struct editor *g, int p, int co)
{
	/*
	 * == Column number after advancing past the character at p ==
	 *
	 * Given the current column co, returns the column that follows the
	 * character at p.  Tabs snap to the next tab stop; control characters
	 * consume two columns ('^' + char); other codepoints use
	 * utf8_cell_width.
	 */
	int c = text_char(g, p);
	int next;
	char b[4];
	int n;

	if (c == '\t')
		co = next_tabstop(g, co);
	else if ((unsigned char)c < ' ' || c == ASCII_DEL)
		co++;
	else {
		next = cp_next(g, p);
		n = text_read(g, p, next - p < 4 ? next - p : 4, b);
		co += utf8_cell_width(b, b + n) - 1;
	}
	return co + 1;
}

int
get_column(struct editor *g, int p)
{
	/*
	 * == Column offset of buffer offset p ==
	 *
	 * Walks from begin_line to p, accumulating column widths via
	 * next_column.  Returns 0 for the first column.
	 * Used by autoindent, the | motion, and status-line rendering.
	 */
	int r;
	int limit;
	int co = 0;

	limit = cp_start(g, p);
	for (r = begin_line(g, limit); r < limit;) {
		int sk = esc_skip(g, r);

		if (sk != r) {
			r = (sk > limit) ? limit : sk;
			continue;
		}
		co = next_column(g, r, co);
		r = cp_next(g, r);
	}
	return co;
}
