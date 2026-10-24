/*
 * color_generic.c - table-driven syntax colorizer engine.
 *
 * colorize_generic() implements the mechanical scanning shared by every
 * language colorizer: keyword lookup, identifier/number scanning, block
 * and line comments, and quoted strings (including triple-quoted and
 * cross-line variants).  Per-language quirks - C preprocessor lines,
 * Python decorators, shell variable expansion, awk regex literals, Rust
 * attributes and lifetimes - are expressed as data in struct lang_spec
 * (see color_generic.h) rather than code here.
 */
#include "color_generic.h"
#include "color.h"
#include "utf8.h"
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
line_cont_state(const struct lang_spec *spec, const char *line, int len)
{
	/*
	 * == State to carry into the next line after a plain line ==
	 *
	 * Only keywords_bol languages track this: a trailing backslash makes
	 * the next line a continuation of this one, and a keyword may not open
	 * a continuation line.  Every other language returns CS_NORMAL.
	 */
	if (spec->keywords_bol && len > 0 && line[len - 1] == '\\')
		return CS_LINE_CONT;
	return CS_NORMAL;
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
scan_sigil(const char *line, int len, int i, char *attrs, int bracket)
{
	/*
	 * == Scan a sigil token: bare word, {...}, or (...) ==
	 *
	 * line[i] is the sigil byte itself.  Colors the whole token
	 * ATTR_PREPROC.  Returns the index just past it.
	 *
	 * With bracket set the token is a Rust attribute instead, which is a
	 * '[...]' list with an optional '!' before it (#[derive(..)],
	 * #![allow(..)]).  Nothing else is an attribute, so a sigil byte that
	 * no such list follows is left uncoloured and the index comes back
	 * unchanged - that keeps the '#' of a raw identifier or of a macro
	 * interpolation from reading as an attribute.  A '"' inside the list
	 * is skipped over, since the ']' that ends the list is the first one
	 * outside a quoted argument, as in #[doc = "see [Foo]"].
	 *
	 * The list ends at the end of the line when it holds no ']', because
	 * no state is carried across lines: the rest of an attribute split
	 * over several lines is scanned as ordinary code.
	 */
	int start = i++;
	char close = 0;

	if (bracket) {
		if (i < len && line[i] == '!')
			i++;
		if (i >= len || line[i] != '[')
			return start;
		close = ']';
	} else if (i < len) {
		if (line[i] == '{')
			close = '}';
		else if (line[i] == '(')
			close = ')';
	}
	if (close) {
		i++;
		while (i < len && line[i] != close) {
			if (bracket && line[i] == '"') {
				for (i++; i < len && line[i] != '"'; i++) {
					if (line[i] == '\\' && i + 1 < len)
						i++;
				}
				if (i >= len)
					break;
			}
			i++;
		}
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
		if (i < len && line[i] == '.' &&
		    (!num->dot_needs_digit ||
		     (i + 1 < len && isdigit((unsigned char)line[i + 1])))) {
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
scan_regex(const char *line, int len, int i)
{
	/*
	 * == Find the end of a /regex/ literal that opens at line[i] ==
	 *
	 * line[i] is the opening '/'.  A backslash escapes the following
	 * byte, and a '/' inside a bracket expression ([/], []/], [^]/])
	 * does not close the literal.  The literal never spans lines.
	 * Returns the index just past the closing '/', or -1 when this line
	 * holds no close, in which case the '/' is a division operator.
	 */
	int bracket = 0;

	for (i++; i < len; i++) {
		if (line[i] == '\\') {
			i++;
			continue;
		}
		if (bracket) {
			if (line[i] == ']')
				bracket = 0;
			continue;
		}
		if (line[i] == '[') {
			bracket = 1;
			if (i + 1 < len && line[i + 1] == '^')
				i++;
			if (i + 1 < len && line[i + 1] == ']')
				i++;
			continue;
		}
		if (line[i] == '/')
			return i + 1;
	}
	return -1;
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
			i = scan_sigil(line, len, i, attrs, 0);
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
is_char_literal(const char *line, int len, int i)
{
	/*
	 * == True if a one-character literal opens at line[i] ==
	 *
	 * line[i] is the quote.  Two shapes count: an escape sequence
	 * ('\n', '\x41', '\u{1F600}'), which is recognized by the
	 * backslash and bounded by the closing quote on the same line; and a
	 * single character, whose closing quote must follow it immediately,
	 * one byte later for ASCII and up to four for UTF-8.  The width of
	 * that one character comes from stepfwd(), so it is the width the
	 * editor's cursor motion uses.  Everything else is a Rust lifetime or
	 * loop label ('a, 'static, 'outer), which the caller leaves to
	 * lang_spec.label_char.
	 */
	const char *end = line + len;
	const char *next;
	int j;

	if (i + 1 >= len)
		return 0;
	if (line[i + 1] == '\\') {
		for (j = i + 2; j < len && j <= i + 12; j++) {
			if (line[j] == '\'')
				return 1;
		}
		return 0;
	}
	next = stepfwd(line + i + 1, end);
	return next < end && *next == '\'';
}

static int
raw_spec_index(const struct lang_spec *spec)
{
	/*
	 * == Index of the string spec that has a raw form, or -1 ==
	 *
	 * The cross-line state of a raw string counts the hashes of its open
	 * delimiter and not the spec it belongs to, so at most one spec per
	 * language may set .raw, and this lookup recovers it when a raw
	 * string resumes on the next line.
	 */
	int k;

	if (!spec->strings || !spec->raw_prefix)
		return -1;
	for (k = 0; spec->strings[k].open; k++) {
		if (spec->strings[k].raw)
			return k;
	}
	return -1;
}

static int
scan_raw_string(const struct str_spec *ss, const struct lang_spec *spec,
                 const char *line, int len, int color_from, int i, int hashes,
                 char *attrs, int *out_state)
{
	/*
	 * == Scan the body of a raw string ==
	 *
	 * A raw string has no byte escapes, so it closes at the first
	 * delimiter followed by exactly the hashes the open delimiter
	 * carried, and nothing else ends it: that is what keeps r"C:\dir\"
	 * closed and the hashed form r#"say "hi""# open across its inner
	 * quotes.  i is where the body starts, just past the open delimiter,
	 * or 0 when resuming from the previous line, and color_from..i is
	 * colored first.
	 *
	 * Returns the index just past the string.  *out_state is CS_NORMAL if
	 * it closed on this line, or CS_RAW_BASE + hashes if ss->cross_line
	 * and it did not.  A close delimiter split over a line boundary (the
	 * quote last on one line and its hashes first on the next) does not
	 * close the string, since the state counts hashes and not a partial
	 * match.
	 */
	int k;

	fill_attrs(attrs, color_from, i, ATTR_STRING);
	while (i < len) {
		if (line[i] == ss->open) {
			for (k = 0; k < hashes; k++) {
				if (i + 1 + k >= len ||
				    line[i + 1 + k] != spec->raw_hash)
					break;
			}
			if (k == hashes) {
				fill_attrs(attrs, i, i + 1 + hashes, ATTR_STRING);
				*out_state = CS_NORMAL;
				return i + 1 + hashes;
			}
		}
		fill_attrs(attrs, i, i + 1, ATTR_STRING);
		i++;
	}
	*out_state = ss->cross_line ? CS_RAW_BASE + hashes : CS_NORMAL;
	return i;
}

static int
scan_raw_ident(const struct lang_spec *spec, const char *line, int len, int i)
{
	/*
	 * == Step over a raw identifier, as in the Rust r#type or r#match ==
	 *
	 * line[i] is the raw_prefix letter.  A raw identifier is that letter,
	 * one raw_hash, and a name the language would otherwise read as a
	 * keyword, so the whole token stays ATTR_NORMAL and the name is kept
	 * out of the keyword lookup.  Returns the index just past the token,
	 * or i when no raw identifier starts there, which is also the case
	 * for the raw string r#"..."#, whose delimiter follows the hashes.
	 */
	int j;

	if (!spec->raw_prefix || !spec->raw_hash)
		return i;
	if (line[i] != spec->raw_prefix)
		return i;
	if (i + 2 >= len || line[i + 1] != spec->raw_hash ||
	    !is_ident_start((unsigned char)line[i + 2]))
		return i;
	for (j = i + 2; j < len && is_ident((unsigned char)line[j]); j++)
		;
	return j;
}

static int
try_open_string(const char *line, int len, int i, const struct lang_spec *spec,
                 int *out_spec_idx, int *out_hashes)
{
	/*
	 * == Check whether a string opens at line[i] ==
	 *
	 * Tries a direct delimiter match first; if none and line[i] can
	 * start an identifier, peeks ahead through b/f/r/u prefix letters
	 * for a delimiter belonging to a prefixable spec.  The peek never
	 * mutates state, so a failed peek leaves the caller free to fall
	 * back to plain identifier scanning from i.  A char_literal spec is
	 * skipped unless a one-character literal really opens there, which is
	 * how a Rust lifetime is told from a character.  Returns the index of
	 * the delimiter byte (spec index via *out_spec_idx), or -1.
	 *
	 * *out_hashes is the number of raw_hash bytes between the prefix
	 * letters and the delimiter, or -1 when the literal is not raw.  It
	 * is 0 for a raw form carrying no hashes, as in r"...", which still
	 * has no escapes, so the caller tells the two apart by the sign.
	 */
	int j, k, raw = 0;

	*out_hashes = -1;

	for (k = 0; spec->strings[k].open; k++) {
		if (line[i] == spec->strings[k].open) {
			if (spec->strings[k].char_literal &&
			    !is_char_literal(line, len, i))
				continue;
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
		    p == 'r' || p == 'R' || p == 'u' || p == 'U') {
			if (spec->raw_prefix &&
			    tolower(p) == tolower((unsigned char)spec->raw_prefix))
				raw = 1;
			j++;
		} else {
			break;
		}
	}
	if (j == i)
		return -1;
	if (raw && spec->raw_hash) {
		int hashes = 0;

		while (j + hashes < len && line[j + hashes] == spec->raw_hash)
			hashes++;
		j += hashes;
		*out_hashes = hashes;
	} else if (raw) {
		*out_hashes = 0;
	}
	if (j >= len)
		return -1;
	for (k = 0; spec->strings[k].open; k++) {
		if (spec->strings[k].prefixable && line[j] == spec->strings[k].open) {
			if (spec->strings[k].char_literal &&
			    !is_char_literal(line, len, j))
				continue;
			if (*out_hashes >= 0 && !spec->strings[k].raw)
				continue;
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
	int kw_ok = 1;
	int raw_idx;
	int operand = 0; /* regex_slash: last token was an operand, so '/' divides */

	fill_attrs(attrs, 0, len, ATTR_NORMAL);

	if (spec->keywords_bol && state == CS_LINE_CONT) {
		kw_ok = 0;
		state = CS_NORMAL;
	}

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

	if (state >= CS_RAW_BASE && (raw_idx = raw_spec_index(spec)) >= 0) {
		int new_state;

		i = scan_raw_string(&spec->strings[raw_idx], spec, line, len, 0,
		                     0, state - CS_RAW_BASE, attrs, &new_state);
		if (new_state != CS_NORMAL)
			return new_state;
		goto normal;
	}

	if (spec->strings && state >= CS_STRING_BASE && state < CS_RAW_BASE) {
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
		int spec_idx, quote_pos, hashes, raw_end;

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

		if ((spec->line_comment &&
		     starts_with(line, len, i, spec->line_comment)) ||
		    (spec->line_comment2 &&
		     starts_with(line, len, i, spec->line_comment2))) {
			fill_attrs(attrs, i, len, ATTR_COMMENT);
			return CS_NORMAL;
		}

		if (spec->decorator_char && c == (unsigned char)spec->decorator_char) {
			int end = scan_sigil(line, len, i, attrs,
			                      spec->decorator_bracket);

			if (end > i) {
				i = end;
				operand = 0;
				continue;
			}
			/*
			 * Bracket mode and no [...] list follows, so this is no
			 * attribute: scan on, and the byte falls to the
			 * punctuation tail below as the operator it is.
			 */
		}

		if (spec->sigil_char && c == (unsigned char)spec->sigil_char) {
			i = scan_sigil(line, len, i, attrs, 0);
			operand = 1;
			continue;
		}

		if (spec->regex_slash && c == '/' && !operand) {
			int end = scan_regex(line, len, i);

			if (end >= 0) {
				fill_attrs(attrs, i, end, ATTR_STRING);
				i = end;
				operand = 1;
				continue;
			}
		}

		/*
		 * A raw identifier is stepped over whole, before the string
		 * attempt sees its prefix letter and before its name reaches
		 * the keyword lookup.
		 */
		raw_end = scan_raw_ident(spec, line, len, i);
		if (raw_end > i) {
			i = raw_end;
			operand = 1;
			continue;
		}

		quote_pos = try_open_string(line, len, i, spec, &spec_idx,
		                             &hashes);
		if (quote_pos >= 0) {
			int new_state;

			if (hashes >= 0)
				i = scan_raw_string(&spec->strings[spec_idx],
				                     spec, line, len, i,
				                     quote_pos + 1, hashes,
				                     attrs, &new_state);
			else
				i = scan_string_open(spec_idx, spec, line, len,
				                      i, quote_pos, attrs,
				                      &new_state);
			if (new_state != CS_NORMAL)
				return new_state;
			operand = 1;
			continue;
		}

		/*
		 * A label_char quote that opened no literal above introduces a
		 * lifetime or a loop label, so the quote and the name after it
		 * stay ATTR_NORMAL and are stepped over as one token - that
		 * keeps a name such as 'static out of the keyword lookup.
		 */
		if (spec->label_char && c == (unsigned char)spec->label_char) {
			i++;
			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			operand = 1;
			continue;
		}

		if (c >= '0' && c <= '9') {
			int start = i;
			int end = scan_number(line, len, i, &spec->num);

			if (!spec->num.word_boundary_guard || end >= len ||
			    !is_ident((unsigned char)line[end]))
				fill_attrs(attrs, start, end, ATTR_NUMBER);
			i = end;
			operand = 1;
			continue;
		}

		if (is_ident_start(c)) {
			int start = i;

			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			if ((!spec->keywords_bol || (kw_ok && is_bol(line, start))) &&
			    keyword_lookup(line + start, i - start, spec->keywords,
			                    spec->keywords_ci)) {
				fill_attrs(attrs, start, i, ATTR_KEYWORD);
				operand = 0;
			} else {
				operand = 1;
			}
			continue;
		}

		/*
		 * A closing bracket ends an operand, and a postfix ++ or --
		 * leaves the operand in place; blanks change nothing.  Any
		 * other punctuation expects an operand next.
		 */
		if (c == ')' || c == ']')
			operand = 1;
		else if ((c == '+' || c == '-') && i + 1 < len && line[i + 1] == c)
			i++;
		else if (c != ' ' && c != '\t')
			operand = 0;
		i++;
	}

	return line_cont_state(spec, line, len);
}
