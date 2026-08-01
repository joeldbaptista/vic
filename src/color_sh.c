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
 * Scanning is done by the generic engine in color_generic.c; see
 * color_generic.h for the cross-line state encoding.  An open double-
 * quoted string is the only thing that spans lines here (here-documents
 * are not tracked; too complex).
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

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

/*
 * "..." spans lines when unterminated and recognizes $-expansion inside;
 * '...' is a literal with no escapes and no expansion.  "..." must come
 * first: its cross-line resume state (CS_STRING_BASE + 0) is what the
 * test suite / callers know as SH_DQUOTE.
 */
static const struct str_spec sh_strings[] = {
    {.open = '"', .escape = 1, .cross_line = 1, .expand_sigil = 1},
    {.open = '\''},
    {0},
};

static const struct lang_spec sh_spec = {
    .keywords = sh_keywords,
    .line_comment = "#",
    .strings = sh_strings,
    .num = {.word_boundary_guard = 1},
    .sigil_char = '$',
};

static int
sh_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of shell script ==
	 */
	return colorize_generic(state, line, len, attrs, &sh_spec);
}

static const char *const sh_extensions[] = {
    ".sh", ".bash", ".zsh", ".ksh", NULL};

const struct colorizer colorizer_sh = {"sh", sh_extensions, sh_colorize};
