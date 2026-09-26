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

static char *
esc_prev_esc(char *p, char *bol)
{
	/*
	 * == Nearest ESC byte at or before p, within the lookback window ==
	 *
	 * Escape sequences never nest, so only the closest preceding ESC can
	 * begin a sequence that covers p.  The search stops at the start of the
	 * line and after ESC_SGR_MAX bytes, which bounds the cost for ordinary
	 * text that contains no escapes at all.  Returns NULL when none is
	 * found.
	 */
	char *q;
	char *lim = (p - bol > ESC_SGR_MAX) ? p - ESC_SGR_MAX : bol;

	for (q = p;; q--) {
		if ((unsigned char)*q == ASCII_ESC)
			return q;
		if (q == lim)
			return NULL;
	}
}

static int
esc_run(struct editor *g, char *p, char **start, char **end)
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
	char *bol, *s, *q;
	int n;

	if (!g->color_escape || p >= g->end)
		return 0;

	bol = begin_line(g, p);

	/* Does a sequence cover p? */
	s = NULL;
	q = esc_prev_esc(p, bol);
	if (q != NULL) {
		n = csi_len(q, g->end, NULL);
		if (n > 0 && p < q + n)
			s = q;
	}
	if (s == NULL)
		return 0;

	/* Absorb any sequence that ends exactly where this run begins. */
	while (s > bol) {
		q = esc_prev_esc(s - 1, bol);
		if (q == NULL)
			break;
		n = csi_len(q, g->end, NULL);
		if (n <= 0 || q + n != s)
			break;
		s = q;
	}

	/* Absorb any sequence that begins where the previous one ends. */
	for (q = s; (n = csi_len(q, g->end, NULL)) > 0;)
		q += n;

	*start = s;
	*end = q;
	return 1;
}

char *
cp_next(struct editor *g, char *p)
{
	/*
	 * == Advance one UTF-8 codepoint ==
	 *
	 * Snaps p to its codepoint start, then steps forward via stepfwd.
	 * Clamps at g->end.
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
	char *orig = p;
	char *n;

	p = cp_start(g, p);
	if (p >= g->end)
		return g->end;
	n = (char *)stepfwd(p, g->end);
	if (n <= orig)
		return orig < g->end ? orig + 1 : g->end;
	return n;
}

char *
cp_prev(struct editor *g, char *p)
{
	/*
	 * == Retreat one UTF-8 codepoint ==
	 *
	 * Steps p back to the lead byte of the preceding codepoint via stepbwd.
	 * Clamps at g->text.  As with cp_next, escape runs are not treated
	 * specially here — see esc_snap_bwd.
	 */
	if (p <= g->text)
		return g->text;
	return (char *)stepbwd(p, g->text);
}

char *
esc_snap_fwd(struct editor *g, char *p)
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
	char *rs, *re;

	return esc_run(g, p, &rs, &re) ? re : p;
}

char *
esc_snap_bwd(struct editor *g, char *p)
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
	char *rs, *re;

	if (!esc_run(g, p, &rs, &re))
		return p;
	if (rs <= g->text || rs[-1] == '\n')
		return rs;
	return (char *)stepbwd(rs, g->text);
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

char *
esc_skip(struct editor *g, char *p)
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
	while ((n = csi_len(p, g->end, NULL)) > 0)
		p += n;
	return p;
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
	for (r = begin_line(g, limit); r < limit;) {
		char *sk = esc_skip(g, r);

		if (sk != r) {
			r = (sk > limit) ? limit : sk;
			continue;
		}
		co = next_column(g, r, co);
		r = cp_next(g, r);
	}
	return co;
}
