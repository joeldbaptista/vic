/*
 * color_py.c - syntax colorizer for Python.
 *
 * Recognised tokens:
 *   Keywords      — Python 3 reserved words and built-in constants
 *   Numbers       — integer, float, hex (0x), octal (0o), binary (0b),
 *                   complex (j suffix), underscore separators
 *   Line comments — # ...
 *   String literals — '...', "...", b/f/r/u prefixed variants
 *   Triple-quoted strings — '''...''' and """...""" (spanning lines)
 *   Decorators    — @identifier (highlighted as ATTR_PREPROC)
 *
 * Cross-line state encoding (two bits):
 *   PY_NORMAL       0  - ordinary code
 *   PY_TRIPLE_DQ    1  - inside """..."""
 *   PY_TRIPLE_SQ    2  - inside '''...'''
 */
#include "color.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

enum {
	PY_NORMAL = 0,
	PY_TRIPLE_DQ = 1,
	PY_TRIPLE_SQ = 2,
};

static const char *const py_keywords[] = {
    "False",
    "None",
    "True",
    "and",
    "as",
    "assert",
    "async",
    "await",
    "break",
    "class",
    "continue",
    "def",
    "del",
    "elif",
    "else",
    "except",
    "finally",
    "for",
    "from",
    "global",
    "if",
    "import",
    "in",
    "is",
    "lambda",
    "nonlocal",
    "not",
    "or",
    "pass",
    "raise",
    "return",
    "try",
    "while",
    "with",
    "yield",
    NULL,
};

static int
py_is_keyword(const char *s, int len)
{
	/*
	 * == Check if the Python token [s, s+len) is in py_keywords ==
	 */
	int i;

	for (i = 0; py_keywords[i]; i++) {
		if ((int)strlen(py_keywords[i]) == len &&
		    memcmp(py_keywords[i], s, (size_t)len) == 0)
			return 1;
	}
	return 0;
}

static int
is_ident_start(unsigned char c)
{
	/*
	 * == True if c can start a Python identifier ==
	 */
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident(unsigned char c)
{
	/*
	 * == True if c can continue a Python identifier ==
	 */
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void
fill_attrs(char *attrs, int from, int to, char attr)
{
	/*
	 * == Fill attrs[from..to-1] with the given ATTR_* value ==
	 */
	int i;

	for (i = from; i < to; i++)
		attrs[i] = attr;
}

static int
scan_simple_string(const char *line, int len, int i, char q)
{
	/*
	 * == Scan a single-line Python string literal past the closing delimiter ==
	 *
	 * Starts at line[i], which is one past the opening delimiter q (' or ").
	 * Handles backslash escapes.  Returns the index just past the closing q,
	 * or len if the string is not closed on this line.
	 */
	while (i < len) {
		if (line[i] == '\\') {
			i += 2;
			continue;
		}
		if (line[i] == q) {
			i++;
			break;
		}
		i++;
	}
	return i;
}

static int
scan_triple_close(const char *line, int len, int i, char q)
{
	/*
	 * == Advance past the triple-quote close (qqq) in a multi-line string ==
	 *
	 * Starts at line[i] and scans forward.  Handles backslash escapes.
	 * Returns the index just past the closing triple-quote, or -1 if the
	 * triple-quote is not found (the string continues on the next line).
	 */
	while (i < len) {
		if (line[i] == '\\') {
			i += 2;
			continue;
		}
		if (line[i] == q && i + 2 < len && line[i + 1] == q &&
		    line[i + 2] == q) {
			return i + 3;
		}
		i++;
	}
	return -1; /* still open at end of line */
}

static int
py_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of Python and return the new cross-line state ==
	 *
	 * Recognises keywords, # line comments, single- and double-quoted
	 * strings (including raw/byte prefixes r"", b""), triple-quoted
	 * strings (spanning multiple lines via state), and integer/float/hex
	 * number literals.  Decorators (@word) are highlighted as ATTR_PREPROC.
	 *
	 * Cross-line state is non-zero while inside an open triple-quoted string.
	 */
	int i = 0;

#define SET(from, to, a)                                      \
	do {                                                  \
		if (attrs)                                    \
			fill_attrs(attrs, (from), (to), (a)); \
	} while (0)

	if (attrs)
		fill_attrs(attrs, 0, len, ATTR_NORMAL);

	/* Resume triple-quoted string from previous line. */
	if (state == PY_TRIPLE_DQ || state == PY_TRIPLE_SQ) {
		char q = (state == PY_TRIPLE_DQ) ? '"' : '\'';
		int end = scan_triple_close(line, len, i, q);
		if (end < 0) {
			SET(0, len, ATTR_STRING);
			return state; /* still open */
		}
		SET(0, end, ATTR_STRING);
		i = end;
		state = PY_NORMAL;
	}

	while (i < len) {
		unsigned char c = (unsigned char)line[i];

		/* Comment. */
		if (c == '#') {
			SET(i, len, ATTR_COMMENT);
			goto done;
		}

		/* Decorator: @name */
		if (c == '@') {
			int start = i++;
			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			SET(start, i, ATTR_PREPROC);
			continue;
		}

		/* String literals — optionally prefixed with b/f/r/u and their
		 * combinations (case-insensitive). */
		if (is_ident_start(c) || c == '"' || c == '\'') {
			int start = i;
			/* Consume optional string prefix (b, f, r, u combos). */
			if (c != '"' && c != '\'') {
				/* Peek ahead: is this a string prefix followed by
				 * a quote? */
				int j = i;
				while (j < len) {
					unsigned char p =
					    (unsigned char)line[j];
					if (p == 'b' || p == 'B' || p == 'f' ||
					    p == 'F' || p == 'r' || p == 'R' ||
					    p == 'u' || p == 'U')
						j++;
					else
						break;
				}
				if (j < len &&
				    (line[j] == '"' || line[j] == '\'')) {
					i = j; /* prefix consumed */
					c = (unsigned char)line[i];
				} else {
					/* Not a string prefix — fall through to
					 * keyword/identifier. */
					goto word;
				}
			}

			{
				char q = (char)c;
				i++; /* past opening quote */

				/* Triple-quoted? */
				if (i + 1 < len && line[i] == q &&
				    line[i + 1] == q) {
					int new_state = (q == '"') ? PY_TRIPLE_DQ
					                           : PY_TRIPLE_SQ;
					i += 2; /* past """ or ''' */
					int end = scan_triple_close(line, len, i,
					                            q);
					if (end < 0) {
						SET(start, len, ATTR_STRING);
						state = new_state;
						goto done;
					}
					SET(start, end, ATTR_STRING);
					i = end;
					continue;
				}

				/* Simple single-line string. */
				i = scan_simple_string(line, len, i, q);
				SET(start, i, ATTR_STRING);
				continue;
			}
		}

		/* Numbers. */
		if (c >= '0' && c <= '9') {
			int start = i;
			if (c == '0' && i + 1 < len) {
				char nx = line[i + 1];
				if (nx == 'x' || nx == 'X') {
					i += 2;
					while (i < len &&
					       (isxdigit((unsigned char)line[i]) ||
					        line[i] == '_'))
						i++;
					goto num_suffix;
				}
				if (nx == 'o' || nx == 'O') {
					i += 2;
					while (i < len &&
					       ((line[i] >= '0' &&
					         line[i] <= '7') ||
					        line[i] == '_'))
						i++;
					goto num_suffix;
				}
				if (nx == 'b' || nx == 'B') {
					i += 2;
					while (i < len &&
					       (line[i] == '0' ||
					        line[i] == '1' ||
					        line[i] == '_'))
						i++;
					goto num_suffix;
				}
			}
			while (i < len &&
			       (isdigit((unsigned char)line[i]) || line[i] == '_'))
				i++;
			if (i < len && line[i] == '.') {
				i++;
				while (i < len &&
				       (isdigit((unsigned char)line[i]) ||
				        line[i] == '_'))
					i++;
			}
			if (i < len && (line[i] == 'e' || line[i] == 'E')) {
				i++;
				if (i < len &&
				    (line[i] == '+' || line[i] == '-'))
					i++;
				while (i < len && isdigit((unsigned char)line[i]))
					i++;
			}
		num_suffix:
			/* complex suffix */
			if (i < len && (line[i] == 'j' || line[i] == 'J'))
				i++;
			SET(start, i, ATTR_NUMBER);
			continue;
		}

	word:
		/* Keyword or identifier. */
		if (is_ident_start(c)) {
			int start = i;
			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			if (py_is_keyword(line + start, i - start))
				SET(start, i, ATTR_KEYWORD);
			continue;
		}

		i++;
	}

done:
	return state;
#undef SET
}

static const char *const py_extensions[] = {".py", ".pyw", NULL};

const struct colorizer colorizer_py = {"py", py_extensions, py_colorize};
