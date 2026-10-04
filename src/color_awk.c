/*
 * color_awk.c - syntax colorizer for awk scripts (POSIX awk and gawk).
 *
 * Recognised tokens:
 *   Keywords      — patterns (BEGIN, END, BEGINFILE, ENDFILE), statements
 *                   (if, while, print, getline, ...) and the built-in
 *                   function names (length, sub, gensub, ...), which awk
 *                   reserves the same way it reserves keywords
 *   Fields        — $1, $NF, $(i+1) — highlighted as ATTR_PREPROC
 *   Directives    — gawk @include, @load, @namespace — ATTR_PREPROC
 *   Comments      — '#' outside of strings and regex literals
 *   Strings       — "..." with backslash escapes
 *   Regex literals — /.../ wherever an operand is expected — ATTR_STRING
 *   Numbers       — decimal, float, exponent, and gawk 0x hex literals
 *
 * Scanning is done by the generic engine in color_generic.c; see
 * color_generic.h for the cross-line state encoding.  Nothing spans lines
 * here: an awk string or regex literal must close on the line it opens.
 *
 * A '/' is read as the start of a regex literal when the previous token is
 * not an operand, and only when the same line holds the closing '/'.  The
 * built-in variables (NR, NF, FS, ...) are ordinary names to awk, so they
 * are left uncoloured.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

static const char *const awk_keywords[] = {
    /* patterns */
    "BEGIN",
    "BEGINFILE",
    "END",
    "ENDFILE",
    /* statements and operators */
    "break",
    "case",
    "continue",
    "default",
    "delete",
    "do",
    "else",
    "exit",
    "for",
    "func",
    "function",
    "getline",
    "if",
    "in",
    "next",
    "nextfile",
    "print",
    "printf",
    "return",
    "switch",
    "while",
    /* built-in functions, POSIX */
    "atan2",
    "close",
    "cos",
    "exp",
    "fflush",
    "gsub",
    "index",
    "int",
    "length",
    "log",
    "match",
    "rand",
    "sin",
    "split",
    "sprintf",
    "sqrt",
    "srand",
    "sub",
    "substr",
    "system",
    "tolower",
    "toupper",
    /* built-in functions, gawk */
    "and",
    "asort",
    "asorti",
    "bindtextdomain",
    "compl",
    "dcgettext",
    "dcngettext",
    "gensub",
    "isarray",
    "lshift",
    "mkbool",
    "mktime",
    "or",
    "patsplit",
    "rshift",
    "strftime",
    "strtonum",
    "systime",
    "typeof",
    "xor",
    NULL,
};

static const struct str_spec awk_strings[] = {
    {.open = '"', .escape = 1},
    {0},
};

static const struct lang_spec awk_spec = {
    .keywords = awk_keywords,
    .line_comment = "#",
    .strings = awk_strings,
    .num = {.hex = 1, .word_boundary_guard = 1},
    .sigil_char = '$',
    .decorator_char = '@',
    .regex_slash = 1,
};

static int
awk_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of awk script ==
	 */
	return colorize_generic(state, line, len, attrs, &awk_spec);
}

static const char *const awk_extensions[] = {".awk", ".gawk", NULL};

const struct colorizer colorizer_awk = {"awk", awk_extensions, NULL, awk_colorize, 4};
