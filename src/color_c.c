/*
 * color_c.c - syntax colorizer for C and C++.
 *
 * Two colorizers are exported: colorizer_c (.c/.h) and colorizer_cpp
 * (.cc/.cpp/.cxx/.hpp/...).  They share one implementation; the only
 * difference is the keyword table.
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
 * Cross-line state:
 *   CS_NORMAL         - ordinary code / start of file
 *   CS_BLOCK_CMT      - inside a block comment
 *   CS_BLOCK_CMT_STAR - inside a block comment, last byte was '*'
 *   CS_PREPROC_CONT   - preprocessor line continuation (trailing '\')
 */
#include "color.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

enum {
	CS_NORMAL = 0,
	CS_BLOCK_CMT = 1,
	CS_BLOCK_CMT_STAR = 2,
	CS_PREPROC_CONT = 3,
};

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

static int
is_keyword(const char *s, int len, const char *const *kw)
{
	/*
	 * == Check if the token [s, s+len) is in the keyword table kw ==
	 *
	 * Linear scan through the NULL-terminated kw array.  Returns 1 on
	 * match, 0 otherwise.
	 */
	int i;

	for (i = 0; kw[i]; i++) {
		if ((int)strlen(kw[i]) == len &&
		    memcmp(kw[i], s, (size_t)len) == 0)
			return 1;
	}
	return 0;
}

static int
is_ident_start(unsigned char c)
{
	/*
	 * == True if c can start a C/C++ identifier ==
	 */
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident(unsigned char c)
{
	/*
	 * == True if c can continue a C/C++ identifier (letter, digit, or _) ==
	 */
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void
fill_attrs(char *attrs, int from, int to, char attr)
{
	/*
	 * == Fill attrs[from..to-1] with the given ATTR_* value ==
	 *
	 * No-op when attrs is NULL (used for pre-scan passes that only need
	 * the returned state).
	 */
	int i;

	for (i = from; i < to; i++)
		attrs[i] = attr;
}

static int
colorize_impl(int state, const char *line, int len, char *attrs,
              const char *const *kw)
{
	/*
	 * == Colorize one line of C/C++ source and return the new cross-line state ==
	 *
	 * Applies syntax highlighting for a single line [line, line+len) using
	 * the given keyword table kw.  Fills attrs[] with ATTR_* values for each
	 * byte.  If attrs is NULL, only the returned state value is meaningful
	 * (used for pre-scan by screen.c to seed color_state at screenbegin).
	 *
	 * Cross-line state:
	 *   CS_NORMAL           — start of line in normal mode
	 *   CS_BLOCK_CMT        — inside a block comment (slash-star)
	 *   CS_BLOCK_CMT_STAR   — inside block comment, last byte was '*'
	 *   CS_PREPROC_CONT     — preprocessor continuation (trailing '\')
	 */
	int i = 0;
	int new_state = CS_NORMAL;

#define SET(from, to, a)                                      \
	do {                                                  \
		if (attrs)                                    \
			fill_attrs(attrs, (from), (to), (a)); \
	} while (0)

	if (attrs)
		fill_attrs(attrs, 0, len, ATTR_NORMAL);

	if (state == CS_BLOCK_CMT || state == CS_BLOCK_CMT_STAR) {
		while (i < len) {
			if (state == CS_BLOCK_CMT_STAR && line[i] == '/') {
				SET(i, i + 1, ATTR_COMMENT);
				i++;
				state = CS_NORMAL;
				goto normal;
			}
			state = (line[i] == '*') ? CS_BLOCK_CMT_STAR : CS_BLOCK_CMT;
			SET(i, i + 1, ATTR_COMMENT);
			i++;
		}
		new_state = state;
		goto done;
	}

	if (state == CS_PREPROC_CONT) {
		new_state =
		    (len > 0 && line[len - 1] == '\\') ? CS_PREPROC_CONT : CS_NORMAL;
		SET(0, len, ATTR_PREPROC);
		goto done;
	}

normal:
	while (i < len) {
		unsigned char c = (unsigned char)line[i];

		if (c == '#') {
			int bol = 1, j;
			for (j = 0; j < i; j++) {
				if (line[j] != ' ' && line[j] != '\t') {
					bol = 0;
					break;
				}
			}
			if (bol) {
				new_state = (len > 0 && line[len - 1] == '\\')
				                ? CS_PREPROC_CONT
				                : CS_NORMAL;
				SET(i, len, ATTR_PREPROC);
				goto done;
			}
		}

		if (c == '/' && i + 1 < len && line[i + 1] == '*') {
			int start = i;
			i += 2;
			state = CS_BLOCK_CMT;
			while (i < len) {
				if (state == CS_BLOCK_CMT_STAR && line[i] == '/') {
					i++;
					state = CS_NORMAL;
					break;
				}
				state = (line[i] == '*') ? CS_BLOCK_CMT_STAR : CS_BLOCK_CMT;
				i++;
			}
			SET(start, i, ATTR_COMMENT);
			if (state != CS_NORMAL) {
				new_state = state;
				goto done;
			}
			continue;
		}

		if (c == '/' && i + 1 < len && line[i + 1] == '/') {
			SET(i, len, ATTR_COMMENT);
			goto done;
		}

		if (c == '"') {
			int start = i++;
			while (i < len) {
				if (line[i] == '\\') {
					i += 2;
					continue;
				}
				if (line[i] == '"') {
					i++;
					break;
				}
				i++;
			}
			SET(start, i, ATTR_STRING);
			continue;
		}

		if (c == '\'') {
			int start = i++;
			while (i < len) {
				if (line[i] == '\\') {
					i += 2;
					continue;
				}
				if (line[i] == '\'') {
					i++;
					break;
				}
				i++;
			}
			SET(start, i, ATTR_STRING);
			continue;
		}

		/* Number literal: decimal, hex (0x/0X), float with exponent/suffix. */
		if (c >= '0' && c <= '9') {
			int start = i;
			if (c == '0' && i + 1 < len &&
			    (line[i + 1] == 'x' || line[i + 1] == 'X')) {
				i += 2;
				while (i < len && isxdigit((unsigned char)line[i]))
					i++;
			} else {
				while (i < len && isdigit((unsigned char)line[i]))
					i++;
				if (i < len && line[i] == '.') {
					i++;
					while (i < len && isdigit((unsigned char)line[i]))
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
			}
			/* integer/float suffix: u U l L f F and combinations */
			while (i < len) {
				unsigned char s = (unsigned char)line[i];
				if (s == 'u' || s == 'U' || s == 'l' || s == 'L' ||
				    s == 'f' || s == 'F')
					i++;
				else
					break;
			}
			SET(start, i, ATTR_NUMBER);
			continue;
		}

		if (is_ident_start(c)) {
			int start = i;
			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			if (is_keyword(line + start, i - start, kw))
				SET(start, i, ATTR_KEYWORD);
			continue;
		}

		i++;
	}

done:
	return new_state;
#undef SET
}

static int
c_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of C source using the C keyword table ==
	 *
	 * Thin wrapper around colorize_impl for the C colorizer entry point.
	 */
	return colorize_impl(state, line, len, attrs, c_keywords);
}

static int
cpp_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of C++ source using the C++ keyword table ==
	 *
	 * Thin wrapper around colorize_impl for the C++ colorizer entry point.
	 */
	return colorize_impl(state, line, len, attrs, cpp_keywords);
}

static const char *const c_extensions[] = {".c", ".h", NULL};

static const char *const cpp_extensions[] = {
    ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".inl", NULL};

const struct colorizer colorizer_c = {"c", c_extensions, c_colorize};
const struct colorizer colorizer_cpp = {"cpp", cpp_extensions, cpp_colorize};
