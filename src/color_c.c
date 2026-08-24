/*
 * color_c.c - syntax colorizer for C and C++.
 *
 * Two colorizers are exported: colorizer_c (.c/.h) and colorizer_cpp
 * (.cc/.cpp/.cxx/.hpp/...).  They share one struct lang_spec; the only
 * difference is the keyword table.  Scanning itself is done by the
 * generic engine in color_generic.c.
 *
 * Recognised tokens:
 *   Keywords      — language-specific tables below
 *   Numbers       — decimal, hex (0x), float with optional suffix
 *   Preprocessor  — '#' as first non-whitespace on a line
 *   Line comments — // ...
 *   Block comments  (slash-star ... star-slash), spanning lines
 *   String literals "..."
 *   Character literals '...'
 *
 * Cross-line state: see color_generic.h.  '#' lines use CS_BOL_CONT for
 * a trailing-backslash continuation; block comments use CS_BLOCK_CMT /
 * CS_BLOCK_CMT_STAR.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

/* C89/C99/C11 reserved keywords plus common macros (sorted). */
static const char *const c_keywords[] = {
    "NULL",
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_Bool",
    "_Complex",
    "_Generic",
    "_Imaginary",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",
    "auto",
    "break",
    "case",
    "char",
    "const",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "false",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "struct",
    "switch",
    "true",
    "typedef",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
    NULL,
};

/* C++ keywords (superset of C). */
static const char *const cpp_keywords[] = {
    "NULL",
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_Bool",
    "_Complex",
    "_Generic",
    "_Imaginary",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",
    "alignas",
    "alignof",
    "auto",
    "bool",
    "break",
    "case",
    "catch",
    "char",
    "char16_t",
    "char32_t",
    "char8_t",
    "class",
    "co_await",
    "co_return",
    "co_yield",
    "concept",
    "const",
    "consteval",
    "constexpr",
    "constinit",
    "const_cast",
    "continue",
    "decltype",
    "default",
    "delete",
    "do",
    "double",
    "dynamic_cast",
    "else",
    "enum",
    "explicit",
    "export",
    "extern",
    "false",
    "final",
    "float",
    "for",
    "friend",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "nullptr",
    "operator",
    "override",
    "private",
    "protected",
    "public",
    "register",
    "reinterpret_cast",
    "requires",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "static_cast",
    "struct",
    "switch",
    "template",
    "this",
    "thread_local",
    "throw",
    "true",
    "try",
    "typedef",
    "typeid",
    "typename",
    "union",
    "unsigned",
    "using",
    "virtual",
    "void",
    "volatile",
    "wchar_t",
    "while",
    NULL,
};

/* "..." and '...' — both backslash-escaped, neither spans lines. */
static const struct str_spec c_strings[] = {
    {.open = '"', .escape = 1},
    {.open = '\'', .escape = 1},
    {0},
};

static const struct lang_spec c_spec = {
    .keywords = c_keywords,
    .line_comment = "//",
    .block_open = "/*",
    .block_close = "*/",
    .strings = c_strings,
    .num = {.hex = 1, .int_suffixes = "uUlLfF"},
    .bol_char = '#',
    .bol_continuation = 1,
};

static const struct lang_spec cpp_spec = {
    .keywords = cpp_keywords,
    .line_comment = "//",
    .block_open = "/*",
    .block_close = "*/",
    .strings = c_strings,
    .num = {.hex = 1, .int_suffixes = "uUlLfF"},
    .bol_char = '#',
    .bol_continuation = 1,
};

static int
c_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of C source ==
	 */
	return colorize_generic(state, line, len, attrs, &c_spec);
}

static int
cpp_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of C++ source ==
	 */
	return colorize_generic(state, line, len, attrs, &cpp_spec);
}

static const char *const c_extensions[] = {".c", ".h", NULL};

static const char *const cpp_extensions[] = {
    ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".inl", NULL};

const struct colorizer colorizer_c = {"c", c_extensions, c_colorize, 8};
const struct colorizer colorizer_cpp = {"cpp", cpp_extensions, cpp_colorize, 8};
