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
 * Scanning is done by the generic engine in color_generic.c; see
 * color_generic.h for the cross-line state encoding.  An open triple-
 * quoted string is the only thing that spans lines here.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

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

/*
 * '...' and "..." — backslash-escaped, optionally b/f/r/u prefixed, and
 * each may open a triple-quoted form that spans lines.  A non-triple
 * instance never spans lines (cross_line stays unset).
 */
static const struct str_spec py_strings[] = {
    {.open = '"', .escape = 1, .triple = 1, .prefixable = 1},
    {.open = '\'', .escape = 1, .triple = 1, .prefixable = 1},
    {0},
};

static const struct lang_spec py_spec = {
    .keywords = py_keywords,
    .line_comment = "#",
    .strings = py_strings,
    .num = {.hex = 1, .octal = 1, .binary = 1, .underscore_sep = 1,
            .complex_suffix = 1},
    .decorator_char = '@',
};

static int
py_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of Python source ==
	 */
	return colorize_generic(state, line, len, attrs, &py_spec);
}

static const char *const py_extensions[] = {".py", ".pyw", NULL};

const struct colorizer colorizer_py = {"py", py_extensions, py_colorize};
