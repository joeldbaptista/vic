/*
 * color_generic.c - table-driven syntax colorizer engine.
 *
 * colorize_generic() implements the mechanical scanning shared by every
 * language colorizer: keyword lookup, identifier/number scanning, block
 * and line comments, and quoted strings (including triple-quoted and
 * cross-line variants).  Per-language quirks - C preprocessor lines,
 * Python decorators, shell variable expansion - are expressed as data
 * in struct lang_spec (see color_generic.h) rather than code here.
 */
#include "color_generic.h"
#include "color.h"
#include <ctype.h>
#include <string.h>

static void
fill_attrs(char *attrs, int from, int to, char attr)
{
	/*
	 * == Fill attrs[from..to-1] with the given ATTR_* value ==
	 *
	 * No-op when attrs is NULL (pre-scan passes that only need the
	 * returned state pass NULL).
	 */
	int i;

	if (!attrs)
		return;
	for (i = from; i < to; i++)
		attrs[i] = attr;
}

static int
is_ident_start(unsigned char c)
{
	/*
	 * == True if c can start an identifier (letter or underscore) ==
	 */
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident(unsigned char c)
{
	/*
	 * == True if c can continue an identifier (letter, digit, or _) ==
	 */
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

static int
is_bol(const char *line, int i)
{
	/*
	 * == True if line[0..i) is only spaces and tabs ==
	 */
	int j;

	for (j = 0; j < i; j++) {
		if (line[j] != ' ' && line[j] != '\t')
			return 0;
	}
	return 1;
}

static int
starts_with(const char *line, int len, int i, const char *s)
{
	/*
	 * == True if s occurs at line[i] ==
	 */
	int n = (int)strlen(s);

	if (i + n > len)
		return 0;
	return memcmp(line + i, s, (size_t)n) == 0;
}

static int
keyword_lookup(const char *s, int len, const char *const *kw, int ci)
{
	/*
	 * == Check if the token [s, s+len) is in the NULL-terminated table kw ==
	 *
	 * When ci is set, the token is folded to lower-case into a bounded
	 * stack buffer first (tokens too long to fit can't be keywords, so
	 * they're rejected outright - keyword tables never hold entries
	 * anywhere near that long).
	 */
	char buf[32];
	const char *tok = s;
	int i;

	if (ci) {
		if (len >= (int)sizeof(buf))
			return 0;
		for (i = 0; i < len; i++)
			buf[i] = (char)tolower((unsigned char)s[i]);
		tok = buf;
	}
	for (i = 0; kw[i]; i++) {
		if ((int)strlen(kw[i]) == len && memcmp(kw[i], tok, (size_t)len) == 0)
			return 1;
	}
	return 0;
}

static int
scan_block_comment(int state, const char *line, int len, int i, char *attrs,
                    const char *close, int *out_state)
{
	/*
	 * == Scan forward through an open block comment ==
	 *
	 * state is CS_BLOCK_CMT or CS_BLOCK_CMT_STAR on entry (STAR meaning
	 * the previously scanned byte equals close[0]).  Colors every byte
	 * scanned ATTR_COMMENT.  Returns the index just past the close
	 * sequence, or len if still open at end of line; *out_state is the
	 * resulting CS_NORMAL/CS_BLOCK_CMT/CS_BLOCK_CMT_STAR.
	 */
	while (i < len) {
		if (state == CS_BLOCK_CMT_STAR && line[i] == close[1]) {
			fill_attrs(attrs, i, i + 1, ATTR_COMMENT);
			i++;
			state = CS_NORMAL;
			break;
		}
		state = (line[i] == close[0]) ? CS_BLOCK_CMT_STAR : CS_BLOCK_CMT;
		fill_attrs(attrs, i, i + 1, ATTR_COMMENT);
		i++;
	}
	*out_state = state;
	return i;
}

static int
scan_sigil(const char *line, int len, int i, char *attrs)
{
	/*
	 * == Scan a sigil token: bare word, {...}, or (...) ==
	 *
	 * line[i] is the sigil byte itself.  Colors the whole token
	 * ATTR_PREPROC.  Returns the index just past it.
	 */
	int start = i++;

	if (i < len && (line[i] == '{' || line[i] == '(')) {
		char close = (line[i] == '{') ? '}' : ')';
		i++;
		while (i < len && line[i] != close)
			i++;
		if (i < len)
			i++;
	} else {
		while (i < len && is_ident((unsigned char)line[i]))
			i++;
	}
	fill_attrs(attrs, start, i, ATTR_PREPROC);
	return i;
}

static int
scan_number(const char *line, int len, int i, const struct num_spec *num)
{
	/*
	 * == Scan a numeric literal starting at line[i] (a decimal digit) ==
	 *
	 * Returns the index just past the literal, including any recognized
	 * suffix.  Does not color attrs - callers decide that, since
	 * word_boundary_guard needs to see one byte past the match first.
	 */
	int prefixed = 0;

	if (line[i] == '0' && i + 1 < len) {
		char nx = line[i + 1];
		if (num->hex && (nx == 'x' || nx == 'X')) {
			i += 2;
			while (i < len && (isxdigit((unsigned char)line[i]) ||
			                    (num->underscore_sep && line[i] == '_')))
				i++;
			prefixed = 1;
		} else if (num->octal && (nx == 'o' || nx == 'O')) {
			i += 2;
			while (i < len && ((line[i] >= '0' && line[i] <= '7') ||
			                    (num->underscore_sep && line[i] == '_')))
				i++;
			prefixed = 1;
		} else if (num->binary && (nx == 'b' || nx == 'B')) {
			i += 2;
			while (i < len && (line[i] == '0' || line[i] == '1' ||
			                    (num->underscore_sep && line[i] == '_')))
				i++;
			prefixed = 1;
		}
	}

	if (!prefixed) {
		while (i < len && (isdigit((unsigned char)line[i]) ||
		                    (num->underscore_sep && line[i] == '_')))
			i++;
		if (i < len && line[i] == '.') {
			i++;
			while (i < len && (isdigit((unsigned char)line[i]) ||
			                    (num->underscore_sep && line[i] == '_')))
				i++;
		}
		if (i < len && (line[i] == 'e' || line[i] == 'E')) {
			i++;
			if (i < len && (line[i] == '+' || line[i] == '-'))
				i++;
			while (i < len && isdigit((unsigned char)line[i]))
				i++;
		}
	}

	if (num->int_suffixes) {
		while (i < len && line[i] &&
		       strchr(num->int_suffixes, line[i]))
			i++;
	}
	if (num->complex_suffix && i < len && (line[i] == 'j' || line[i] == 'J'))
		i++;

	return i;
}

static int
find_triple_close(const char *line, int len, int j, char q, int escape)
{
	/*
	 * == Find the closing qqq of a triple-quoted string ==
	 *
	 * j is the index to start searching from.  The close sequence must
	 * appear entirely within this line - it never spans lines.  Returns
	 * the index just past the close, or -1 if not found on this line.
	 */
	while (j < len) {
		if (escape && line[j] == '\\') {
			j += 2;
			continue;
		}
		if (line[j] == q && j + 2 < len && line[j + 1] == q &&
		    line[j + 2] == q)
			return j + 3;
		j++;
	}
	return -1;
}

static int
scan_string_simple(int spec_idx, const struct str_spec *ss,
                    const struct lang_spec *spec, const char *line, int len,
                    int color_from, int i, char *attrs, int *out_state)
{
	/*
	 * == Scan the body of a simple (non-triple) string ==
	 *
	 * i is the index to resume byte-by-byte scanning from (just past the
	 * opening delimiter when opening fresh, or 0 when resuming a
	 * cross_line string from the previous line).  color_from..i is
	 * colored first (the opening delimiter and any prefix letters; a
	 * no-op when resuming, since color_from == i == 0 there).
	 *
	 * Returns the index just past the string.  *out_state is CS_NORMAL
	 * if it closed on this line, or CS_STRING_BASE+spec_idx if ss->cross_line
	 * and it did not.
	 */
	char q = ss->open;

	*out_state = CS_NORMAL;
	fill_attrs(attrs, color_from, i, ATTR_STRING);

	while (i < len) {
		if (ss->escape && line[i] == '\\') {
			int end = (i + 1 < len) ? i + 2 : i + 1;
			fill_attrs(attrs, i, end, ATTR_STRING);
			i = end;
			continue;
		}
		if (ss->doubled_escape && line[i] == q && i + 1 < len &&
		    line[i + 1] == q) {
			fill_attrs(attrs, i, i + 2, ATTR_STRING);
			i += 2;
			continue;
		}
		if (ss->expand_sigil && spec->sigil_char &&
		    line[i] == spec->sigil_char) {
			i = scan_sigil(line, len, i, attrs);
			continue;
		}
		fill_attrs(attrs, i, i + 1, ATTR_STRING);
		if (line[i] == q) {
			i++;
			return i;
		}
		i++;
	}
	if (ss->cross_line)
		*out_state = CS_STRING_BASE + spec_idx;
	return i;
}

static int
scan_string_open(int spec_idx, const struct lang_spec *spec, const char *line,
                  int len, int start, int quote_pos, char *attrs,
                  int *out_state)
{
	/*
	 * == Open and scan a string first encountered mid-line ==
	 *
	 * start is where coloring should begin (the opening delimiter, or
	 * any prefix letters before it); quote_pos is the index of the
	 * delimiter byte itself.  Tries the triple-quoted form first if the
	 * spec allows it and three delimiter bytes are actually present.
	 */
	const struct str_spec *ss = &spec->strings[spec_idx];
	char q = ss->open;

	*out_state = CS_NORMAL;

	if (ss->triple && quote_pos + 2 < len && line[quote_pos + 1] == q &&
	    line[quote_pos + 2] == q) {
		int end = find_triple_close(line, len, quote_pos + 3, q, ss->escape);
		if (end < 0) {
			fill_attrs(attrs, start, len, ATTR_STRING);
			*out_state = CS_STRING_BASE + spec_idx;
			return len;
		}
		fill_attrs(attrs, start, end, ATTR_STRING);
		return end;
	}

	return scan_string_simple(spec_idx, ss, spec, line, len, start,
	                           quote_pos + 1, attrs, out_state);
}

static int
try_open_string(const char *line, int len, int i, const struct lang_spec *spec,
                 int *out_spec_idx)
{
	/*
	 * == Check whether a string opens at line[i] ==
	 *
	 * Tries a direct delimiter match first; if none and line[i] can
	 * start an identifier, peeks ahead through b/f/r/u prefix letters
	 * for a delimiter belonging to a prefixable spec.  The peek never
	 * mutates state, so a failed peek leaves the caller free to fall
	 * back to plain identifier scanning from i.  Returns the index of
	 * the delimiter byte (spec index via *out_spec_idx), or -1.
	 */
	int j, k;

	for (k = 0; spec->strings[k].open; k++) {
		if (line[i] == spec->strings[k].open) {
			*out_spec_idx = k;
			return i;
		}
	}

	if (!is_ident_start((unsigned char)line[i]))
		return -1;

	j = i;
	while (j < len) {
		unsigned char p = (unsigned char)line[j];
		if (p == 'b' || p == 'B' || p == 'f' || p == 'F' ||
		    p == 'r' || p == 'R' || p == 'u' || p == 'U')
			j++;
		else
			break;
	}
	if (j == i || j >= len)
		return -1;
	for (k = 0; spec->strings[k].open; k++) {
		if (spec->strings[k].prefixable && line[j] == spec->strings[k].open) {
			*out_spec_idx = k;
			return j;
		}
	}
	return -1;
}

int
colorize_generic(int state, const char *line, int len, char *attrs,
                  const struct lang_spec *spec)
{
	/*
	 * == Colorize one line per spec and return the new cross-line state ==
	 *
	 * See color_generic.h for the cross-line state encoding and the
	 * struct lang_spec fields.
	 */
	int i = 0;

	fill_attrs(attrs, 0, len, ATTR_NORMAL);

	if (spec->block_open && (state == CS_BLOCK_CMT || state == CS_BLOCK_CMT_STAR)) {
		int new_state;

		i = scan_block_comment(state, line, len, 0, attrs,
		                        spec->block_close, &new_state);
		if (new_state != CS_NORMAL)
			return new_state;
		goto normal;
	}

	if (spec->bol_continuation && state == CS_BOL_CONT) {
		fill_attrs(attrs, 0, len, ATTR_PREPROC);
		return (len > 0 && line[len - 1] == '\\') ? CS_BOL_CONT : CS_NORMAL;
	}

	if (spec->strings && state >= CS_STRING_BASE) {
		int idx = state - CS_STRING_BASE;
		const struct str_spec *ss = &spec->strings[idx];

		if (ss->triple) {
			char q = ss->open;
			int end = find_triple_close(line, len, 0, q, ss->escape);

			if (end < 0) {
				fill_attrs(attrs, 0, len, ATTR_STRING);
				return state;
			}
			fill_attrs(attrs, 0, end, ATTR_STRING);
			i = end;
		} else {
			int new_state;

			i = scan_string_simple(idx, ss, spec, line, len, 0, 0,
			                        attrs, &new_state);
			if (new_state != CS_NORMAL)
				return new_state;
		}
		goto normal;
	}

normal:
	while (i < len) {
		unsigned char c = (unsigned char)line[i];
		int spec_idx, quote_pos;

		if (spec->bol_char && c == (unsigned char)spec->bol_char &&
		    is_bol(line, i)) {
			fill_attrs(attrs, i, len, ATTR_PREPROC);
			return (spec->bol_continuation && len > 0 &&
			        line[len - 1] == '\\')
			           ? CS_BOL_CONT
			           : CS_NORMAL;
		}

		if (spec->block_open && starts_with(line, len, i, spec->block_open)) {
			int openlen = (int)strlen(spec->block_open);
			int new_state;
			int start = i;

			fill_attrs(attrs, start, start + openlen, ATTR_COMMENT);
			i = scan_block_comment(CS_BLOCK_CMT, line, len,
			                        start + openlen, attrs,
			                        spec->block_close, &new_state);
			if (new_state != CS_NORMAL)
				return new_state;
			continue;
		}

		if (spec->line_comment && starts_with(line, len, i, spec->line_comment)) {
			fill_attrs(attrs, i, len, ATTR_COMMENT);
			return CS_NORMAL;
		}

		if (spec->decorator_char && c == (unsigned char)spec->decorator_char) {
			i = scan_sigil(line, len, i, attrs);
			continue;
		}

		if (spec->sigil_char && c == (unsigned char)spec->sigil_char) {
			i = scan_sigil(line, len, i, attrs);
			continue;
		}

		quote_pos = try_open_string(line, len, i, spec, &spec_idx);
		if (quote_pos >= 0) {
			int new_state;

			i = scan_string_open(spec_idx, spec, line, len, i,
			                      quote_pos, attrs, &new_state);
			if (new_state != CS_NORMAL)
				return new_state;
			continue;
		}

		if (c >= '0' && c <= '9') {
			int start = i;
			int end = scan_number(line, len, i, &spec->num);

			if (!spec->num.word_boundary_guard || end >= len ||
			    !is_ident((unsigned char)line[end]))
				fill_attrs(attrs, start, end, ATTR_NUMBER);
			i = end;
			continue;
		}

		if (is_ident_start(c)) {
			int start = i;

			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			if (keyword_lookup(line + start, i - start, spec->keywords,
			                    spec->keywords_ci))
				fill_attrs(attrs, start, i, ATTR_KEYWORD);
			continue;
		}

		i++;
	}

	return CS_NORMAL;
}
