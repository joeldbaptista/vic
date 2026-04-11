/*
 * color_sh.c - syntax colorizer for shell scripts (sh/bash/zsh).
 *
 * Recognised tokens:
 *   Keywords      — if then else elif fi for while do done case esac in
 *                   function select until
 *   Variables     — $word, ${...}, $( — highlighted as ATTR_PREPROC
 *   Comments      — '#' outside of strings
 *   Strings       — "..." (with $-expansion inside) and '...' (literal)
 *   Numbers       — bare integer literals
 *
 * Cross-line state:
 *   SH_NORMAL     - ordinary code
 *   SH_DQUOTE     - inside a double-quoted string
 *   SH_HEREDOC    - inside a here-document (not tracked; too complex)
 */
#include "color.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

enum {
	SH_NORMAL = 0,
	SH_DQUOTE = 1,
};

static const char *const sh_keywords[] = {
    "case",
    "do",
    "done",
    "elif",
    "else",
    "esac",
    "fi",
    "for",
    "function",
    "if",
    "in",
    "local",
    "return",
    "select",
    "then",
    "time",
    "until",
    "while",
    NULL,
};

static int
sh_is_keyword(const char *s, int len)
{
	/*
	 * == Check if the shell token [s, s+len) is in sh_keywords ==
	 */
	int i;

	for (i = 0; sh_keywords[i]; i++) {
		if ((int)strlen(sh_keywords[i]) == len &&
		    memcmp(sh_keywords[i], s, (size_t)len) == 0)
			return 1;
	}
	return 0;
}

static int
is_word_start(unsigned char c)
{
	/*
	 * == True if c can start a shell word (letter or underscore) ==
	 */
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_word(unsigned char c)
{
	/*
	 * == True if c can continue a shell word (letter, digit, or underscore) ==
	 */
	return is_word_start(c) || (c >= '0' && c <= '9');
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
sh_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of shell script and return the new cross-line state ==
	 *
	 * Recognises keywords, $-variable expansions (plain, ${...}, $(...))),
	 * #-comments, single-quoted literals, double-quoted strings (with
	 * $-expansion inside), and integer number literals.
	 *
	 * Cross-line state:
	 *   SH_NORMAL — ordinary code
	 *   SH_DQUOTE — inside a double-quoted string continuing from the previous line
	 */
	int i = 0;

#define SET(from, to, a)                                      \
	do {                                                  \
		if (attrs)                                    \
			fill_attrs(attrs, (from), (to), (a)); \
	} while (0)

	if (attrs)
		fill_attrs(attrs, 0, len, ATTR_NORMAL);

	/* Resume double-quoted string from previous line. */
	if (state == SH_DQUOTE) {
		while (i < len) {
			if (line[i] == '\\') {
				SET(i, i + 1 < len ? i + 2 : i + 1, ATTR_STRING);
				i += 2;
				continue;
			}
			if (line[i] == '$') {
				int start = i++;
				if (i < len && (line[i] == '{' || line[i] == '(')) {
					char close = line[i] == '{' ? '}' : ')';
					i++;
					while (i < len && line[i] != close)
						i++;
					if (i < len)
						i++;
				} else {
					while (i < len && is_word((unsigned char)line[i]))
						i++;
				}
				SET(start, i, ATTR_PREPROC);
				continue;
			}
			SET(i, i + 1, ATTR_STRING);
			if (line[i] == '"') {
				i++;
				state = SH_NORMAL;
				goto normal;
			}
			i++;
		}
		goto done;
	}

normal:
	while (i < len) {
		unsigned char c = (unsigned char)line[i];

		/* Comment. */
		if (c == '#') {
			SET(i, len, ATTR_COMMENT);
			goto done;
		}

		/* Single-quoted literal string — no escapes, no expansion. */
		if (c == '\'') {
			int start = i++;
			while (i < len && line[i] != '\'')
				i++;
			if (i < len)
				i++;
			SET(start, i, ATTR_STRING);
			continue;
		}

		/* Double-quoted string — $-expansion applies inside. */
		if (c == '"') {
			int start = i++;
			int closed = 0;
			SET(start, i, ATTR_STRING);
			while (i < len) {
				if (line[i] == '\\') {
					SET(i, i + 1 < len ? i + 2 : i + 1,
					    ATTR_STRING);
					i += 2;
					continue;
				}
				if (line[i] == '$') {
					int vstart = i++;
					if (i < len &&
					    (line[i] == '{' || line[i] == '(')) {
						char close =
						    line[i] == '{' ? '}' : ')';
						i++;
						while (i < len && line[i] != close)
							i++;
						if (i < len)
							i++;
					} else {
						while (i < len &&
						       is_word((unsigned char)
						                   line[i]))
							i++;
					}
					SET(vstart, i, ATTR_PREPROC);
					continue;
				}
				SET(i, i + 1, ATTR_STRING);
				if (line[i] == '"') {
					i++;
					closed = 1;
					break;
				}
				i++;
			}
			if (!closed)
				state = SH_DQUOTE;
			continue;
		}

		/* Variable expansion outside a string. */
		if (c == '$') {
			int start = i++;
			if (i < len && (line[i] == '{' || line[i] == '(')) {
				char close = line[i] == '{' ? '}' : ')';
				i++;
				while (i < len && line[i] != close)
					i++;
				if (i < len)
					i++;
			} else {
				while (i < len && is_word((unsigned char)line[i]))
					i++;
			}
			SET(start, i, ATTR_PREPROC);
			continue;
		}

		/* Integer literal. */
		if (c >= '0' && c <= '9') {
			int start = i;
			while (i < len && isdigit((unsigned char)line[i]))
				i++;
			/* only colour if followed by non-word char */
			if (i >= len || !is_word((unsigned char)line[i]))
				SET(start, i, ATTR_NUMBER);
			continue;
		}

		/* Word: keyword or plain identifier. */
		if (is_word_start(c)) {
			int start = i;
			while (i < len && is_word((unsigned char)line[i]))
				i++;
			if (sh_is_keyword(line + start, i - start))
				SET(start, i, ATTR_KEYWORD);
			continue;
		}

		i++;
	}

done:
	return state;
#undef SET
}

static const char *const sh_extensions[] = {
    ".sh", ".bash", ".zsh", ".ksh", NULL};

const struct colorizer colorizer_sh = {"sh", sh_extensions, sh_colorize};
