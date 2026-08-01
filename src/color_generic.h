/*
 * color_generic.h - table-driven syntax colorizer engine.
 *
 * Shared by color_c.c, color_py.c, color_sh.c, and color_sql.c.  Each of
 * those files owns only its keyword table(s) and a static struct lang_spec
 * describing its syntax; colorize_generic() does the actual scanning.
 *
 * Cross-line state is a plain int, same as the colorize_fn contract in
 * color.h.  Each language's state values are only ever fed back into its
 * own colorizer, so the numeric space is reused across mechanisms below -
 * a given language only ever exercises one of them (colorize_generic
 * dispatches on which lang_spec fields are populated, not on the raw
 * state value alone, so this is unambiguous per language).
 */
#ifndef COLOR_GENERIC_H
#define COLOR_GENERIC_H

enum {
	CS_NORMAL = 0,
	CS_BLOCK_CMT = 1,      /* inside a block comment (block_open languages) */
	CS_BLOCK_CMT_STAR = 2,  /* inside a block comment, last byte was close[0] */
	CS_BOL_CONT = 3,         /* bol_char line, continued via trailing backslash */
	CS_STRING_BASE = 1,       /* + index into lang_spec.strings[]: resuming that string */
};

struct num_spec {
	int hex;                  /* 0x/0X prefixed hex literals */
	int octal;                /* 0o/0O prefixed octal literals */
	int binary;                /* 0b/0B prefixed binary literals */
	int underscore_sep;         /* '_' allowed as a digit-group separator */
	int complex_suffix;          /* trailing j/J marks a complex literal */
	const char *int_suffixes;     /* chars allowed to repeat after the digits, or NULL */
	int word_boundary_guard;       /* only emit ATTR_NUMBER if not followed by an ident char */
};

struct str_spec {
	char open;         /* opening delimiter; open == 0 terminates the array */
	int escape;          /* backslash escapes the following byte */
	int doubled_escape;   /* open,open inside the string is an escaped delimiter */
	int triple;             /* try a qqq...qqq multi-line form before the simple form */
	int cross_line;           /* unterminated simple form persists state to the next line */
	int prefixable;             /* allow b/f/r/u (any case) prefix letters before the quote */
	int expand_sigil;             /* recognize lang_spec.sigil_char inside the string body */
};

struct lang_spec {
	const char *const *keywords; /* NULL-terminated */
	int keywords_ci;               /* case-insensitive keyword match */

	const char *line_comment;       /* e.g. "//", "#", "--"; NULL = none */
	const char *block_open;         /* opening delimiter, e.g. slash-star; NULL = none */
	const char *block_close;        /* closing delimiter, e.g. star-slash; required if block_open is set */

	const struct str_spec *strings; /* array, terminated by a zero .open entry */
	struct num_spec num;

	char sigil_char;     /* e.g. '$'; 0 = none. sigil word/{...}/(...) -> ATTR_PREPROC */
	char decorator_char; /* e.g. '@'; 0 = none. decorator ident -> ATTR_PREPROC, any position */
	char bol_char;        /* e.g. '#'; 0 = none. Only fires as the first non-blank byte on the line */
	int bol_continuation;   /* trailing '\' keeps bol coloring and state alive across lines */
};

/*
 * colorize_generic - scan one line per a struct lang_spec.
 *
 * Parameters:
 *   state  incoming cross-line state (0 = start of file)
 *   line   pointer to the first byte of the line (not null-terminated)
 *   len    number of bytes in the line
 *   attrs  OUTPUT: attrs[i] = ATTR_* for line[i]; may be NULL
 *   spec   the language description to scan against
 *
 * Returns the new state to pass in for the next line.
 */
int colorize_generic(int state, const char *line, int len, char *attrs,
                      const struct lang_spec *spec);

#endif /* COLOR_GENERIC_H */
