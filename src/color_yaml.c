/*
 * color_yaml.c - syntax colorizer for YAML.
 *
 * Token mapping:
 *   ATTR_KEYWORD  — mapping keys (plain or quoted) and the plain constants
 *                   true/false/null/yes/no/on/off and '~'
 *   ATTR_PREPROC  — document markers (---, ...), directives (%YAML 1.2),
 *                   block sequence and explicit key indicators (-, ?),
 *                   anchors (&a), aliases (*a), tags (!!str), and the block
 *                   scalar indicators (|, >, with their modifiers)
 *   ATTR_STRING   — quoted scalars and the body of a block scalar
 *   ATTR_COMMENT  — '#' comments
 *   ATTR_NUMBER   — integer, float, hex, octal and binary scalars
 *
 * Cross-line state:
 *   YAML_NORMAL                  - ordinary text
 *   YAML_BLOCK_BASE + indent     - inside a block scalar whose introducing
 *                                  line was indented `indent` bytes; the
 *                                  block holds every following line that is
 *                                  blank or indented further than that.
 *
 * Indentation is counted in bytes, which is exact for YAML: the format
 * forbids a tab in indentation, so leading whitespace is spaces only.
 *
 * Two constructs are deliberately not tracked across lines, because doing so
 * needs a parser rather than a line-at-a-time state machine: a multi-line
 * plain scalar, and a flow collection split over several lines.  Both are
 * scanned as if each of their lines stood alone.  For the same reason, a key
 * is only recognised at the head of a line, so the keys of an inline flow
 * mapping ({cpu: 2}) and the node after an explicit key indicator ('?') are
 * left as ordinary text.
 */
#include "color.h"
#include <ctype.h>
#include <string.h>

enum {
	YAML_NORMAL = 0,
	YAML_BLOCK_BASE = 1,   /* + indent of the line carrying | or > */
	YAML_INDENT_MAX = 250, /* indent clamp, so the state stays small */
};

/*
 * Plain scalars spelled like these are constants, not text.  The table is
 * lower-case and matched case-insensitively, which covers the YAML 1.1
 * spellings (True, TRUE, Null, ...) that most parsers still accept.  This
 * is the complete list; '~' is handled separately, being punctuation.
 */
static const char *const yaml_consts[] = {
    "true", "false", "null", "yes", "no", "on", "off", NULL};

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
blank(char c)
{
	/*
	 * == True if c is a space or a tab ==
	 */
	return c == ' ' || c == '\t';
}

static int
delim(char c)
{
	/*
	 * == True if c ends a plain token ==
	 *
	 * Blanks separate tokens everywhere; the flow indicators separate them
	 * inside [a, b] and {a: 1}.
	 */
	return blank(c) || c == ',' || c == '[' || c == ']' || c == '{' ||
	       c == '}';
}

static int
indent(const char *line, int len)
{
	/*
	 * == Number of leading whitespace bytes on the line ==
	 *
	 * Equals len when the line is blank.
	 */
	int i = 0;

	while (i < len && blank(line[i]))
		i++;
	return i;
}

static int
numeric(const char *s, int len)
{
	/*
	 * == True if the token [s, s+len) is a YAML number ==
	 *
	 * Accepts an optional sign, then either a 0x/0o/0b prefixed literal or
	 * a decimal with an optional fraction and exponent.  Anything else is
	 * text, so a version (1.2.3) and a date (2026-09-12) are not numbers.
	 */
	int i = 0, dig = 0, dot = 0, exp = 0;

	if (i < len && (s[i] == '-' || s[i] == '+'))
		i++;
	if (len - i > 2 && s[i] == '0') {
		char base = (char)tolower((unsigned char)s[i + 1]);

		if (base == 'x' || base == 'o' || base == 'b') {
			for (i += 2; i < len; i++) {
				if (base == 'x' && !isxdigit((unsigned char)s[i]))
					return 0;
				if (base == 'o' && (s[i] < '0' || s[i] > '7'))
					return 0;
				if (base == 'b' && s[i] != '0' && s[i] != '1')
					return 0;
			}
			return 1;
		}
	}
	for (; i < len; i++) {
		if (isdigit((unsigned char)s[i])) {
			dig++;
			continue;
		}
		if (s[i] == '.' && !dot && !exp) {
			dot = 1;
			continue;
		}
		if ((s[i] == 'e' || s[i] == 'E') && dig && !exp) {
			exp = 1;
			if (i + 1 < len && (s[i + 1] == '-' || s[i + 1] == '+'))
				i++;
			continue;
		}
		return 0;
	}
	return dig > 0;
}

static int
constant(const char *s, int len)
{
	/*
	 * == True if the token [s, s+len) is a YAML constant ==
	 *
	 * Matches yaml_consts[] case-insensitively, and '~' (null).
	 */
	char buf[8];
	int i;

	if (len == 1 && s[0] == '~')
		return 1;
	if (len >= (int)sizeof(buf))
		return 0;
	for (i = 0; i < len; i++)
		buf[i] = (char)tolower((unsigned char)s[i]);
	buf[len] = '\0';
	for (i = 0; yaml_consts[i]; i++) {
		if (strcmp(yaml_consts[i], buf) == 0)
			return 1;
	}
	return 0;
}

static int
quoted_end(const char *line, int len, int i)
{
	/*
	 * == Index just past the quoted scalar opening at line[i] ==
	 *
	 * A backslash escapes the next byte inside "...", and a doubled quote
	 * is a literal quote inside '...'.  Returns len when the scalar is not
	 * closed on this line; an unterminated quote is not carried over,
	 * since a stray quote in one line would then discolour the whole file.
	 */
	char q = line[i];

	for (i++; i < len; i++) {
		if (q == '"' && line[i] == '\\' && i + 1 < len) {
			i++;
			continue;
		}
		if (line[i] == q) {
			if (q == '\'' && i + 1 < len && line[i + 1] == q) {
				i++;
				continue;
			}
			return i + 1;
		}
	}
	return len;
}

static int
node_end(const char *line, int len, int i)
{
	/*
	 * == Index just past an anchor, alias or tag starting at line[i] ==
	 *
	 * line[i] is the '&', '*' or '!' itself.  The name runs to the next
	 * token delimiter.
	 */
	for (i++; i < len && !delim(line[i]); i++)
		;
	return i;
}

static int
block_intro(const char *line, int len, int i)
{
	/*
	 * == True if the '|' or '>' at line[i] introduces a block scalar ==
	 *
	 * It does when only its modifiers (an explicit indent digit, a chomping
	 * '+' or '-') and an optional comment follow it on the line.
	 */
	int j;

	for (j = i + 1; j < len; j++) {
		if (line[j] != '+' && line[j] != '-' &&
		    !isdigit((unsigned char)line[j]))
			break;
	}
	while (j < len && blank(line[j]))
		j++;
	return j >= len || line[j] == '#';
}

static int
scan_key(const char *line, int len, int i, char *attrs)
{
	/*
	 * == Colour the mapping key at line[i], if one starts there ==
	 *
	 * A key ends at a ':' that is followed by whitespace or by the end of
	 * the line; the key itself is either a quoted scalar or a plain one,
	 * and a plain key may contain blanks ("a b: c").  Returns the index
	 * just past the ':' when a key was found, and i when none was.
	 */
	int j = i, end;

	if (line[i] == '"' || line[i] == '\'') {
		j = quoted_end(line, len, i);
	} else {
		while (j < len) {
			if (line[j] == ':' &&
			    (j + 1 >= len || blank(line[j + 1])))
				break;
			if (line[j] == '#' && (j == i || blank(line[j - 1])))
				return i;
			j++;
		}
	}
	end = j;
	while (j < len && blank(line[j]))
		j++;
	if (j >= len || line[j] != ':')
		return i;
	if (j + 1 < len && !blank(line[j + 1]))
		return i;
	fill_attrs(attrs, i, end, ATTR_KEYWORD);
	return j + 1;
}

static int
scan_value(const char *line, int len, int i, int ind, char *attrs)
{
	/*
	 * == Colour the line from line[i] on, and return the new state ==
	 *
	 * ind is the indent of this line, needed to record how far a block
	 * scalar opened here reaches.  Everything the value region can hold is
	 * handled: comments, quoted scalars, anchors/aliases/tags, the block
	 * scalar indicators, and plain tokens (numbers, constants, text).
	 */
	while (i < len) {
		char c = line[i];
		int start;

		if (delim(c) || c == ':') {
			i++;
			continue;
		}
		if (c == '#' && (i == 0 || blank(line[i - 1]))) {
			fill_attrs(attrs, i, len, ATTR_COMMENT);
			return YAML_NORMAL;
		}
		if (c == '"' || c == '\'') {
			start = i;
			i = quoted_end(line, len, i);
			fill_attrs(attrs, start, i, ATTR_STRING);
			continue;
		}
		if (c == '&' || c == '*' || c == '!') {
			start = i;
			i = node_end(line, len, i);
			fill_attrs(attrs, start, i, ATTR_PREPROC);
			continue;
		}
		if ((c == '|' || c == '>') && block_intro(line, len, i)) {
			start = i;
			for (i++; i < len && !blank(line[i]); i++)
				;
			fill_attrs(attrs, start, i, ATTR_PREPROC);
			while (i < len && blank(line[i]))
				i++;
			if (i < len)
				fill_attrs(attrs, i, len, ATTR_COMMENT);
			return YAML_BLOCK_BASE +
			       (ind < YAML_INDENT_MAX ? ind : YAML_INDENT_MAX);
		}
		start = i;
		while (i < len && !delim(line[i]))
			i++;
		if (numeric(line + start, i - start))
			fill_attrs(attrs, start, i, ATTR_NUMBER);
		else if (constant(line + start, i - start))
			fill_attrs(attrs, start, i, ATTR_KEYWORD);
		continue;
	}
	return YAML_NORMAL;
}

static int
yaml_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of YAML and return the new cross-line state ==
	 *
	 * A line is read as: indentation, then any block sequence or explicit
	 * key indicators, then an optional mapping key, then the value.  A
	 * document marker or a directive replaces that shape and is taken at
	 * column 0 only, as the format requires.
	 */
	int i, ind;

	fill_attrs(attrs, 0, len, ATTR_NORMAL);

	if (state >= YAML_BLOCK_BASE) {
		int owner = state - YAML_BLOCK_BASE;

		if (indent(line, len) == len || indent(line, len) > owner) {
			fill_attrs(attrs, 0, len, ATTR_STRING);
			return state;
		}
		/* Less indented: the block ended, so scan this line normally. */
	}

	ind = indent(line, len);
	if (ind == len)
		return YAML_NORMAL;
	i = ind;

	if (ind == 0 && line[0] == '%') {
		fill_attrs(attrs, 0, len, ATTR_PREPROC);
		return YAML_NORMAL;
	}

	if (ind == 0 && len >= 3 && (memcmp(line, "---", 3) == 0 ||
	                              memcmp(line, "...", 3) == 0) &&
	    (len == 3 || blank(line[3]))) {
		fill_attrs(attrs, 0, 3, ATTR_PREPROC);
		for (i = 3; i < len && blank(line[i]); i++)
			;
	}

	while (i < len && (line[i] == '-' || line[i] == '?') &&
	       (i + 1 >= len || blank(line[i + 1]))) {
		fill_attrs(attrs, i, i + 1, ATTR_PREPROC);
		for (i++; i < len && blank(line[i]); i++)
			;
	}

	if (i < len)
		i = scan_key(line, len, i, attrs);

	return scan_value(line, len, i, ind, attrs);
}

static const char *const yaml_extensions[] = {".yaml", ".yml", NULL};

/*
 * YAML is written two spaces to the level, and a tab is illegal in its
 * indentation, so the tabstop only governs how a stray tab inside a scalar
 * is displayed.  Two keeps that consistent with the surrounding text.
 */
const struct colorizer colorizer_yaml = {"yaml", yaml_extensions, NULL,
                                          yaml_colorize, 2};
