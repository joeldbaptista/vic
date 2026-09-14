/*
 * color_make.c - syntax colorizer for makefiles.
 *
 * Token mapping:
 *   ATTR_KEYWORD  - directives (include, ifeq, define, ...) and the target
 *                   list of a rule
 *   ATTR_PREPROC  - variable references ($(CC), ${CC}, $@, $$), the name on
 *                   the left of an assignment, and the recipe prefixes
 *                   '@', '-' and '+'
 *   ATTR_STRING   - quoted text
 *   ATTR_COMMENT  - '#' comments
 *   ATTR_NUMBER   - bare decimal integers
 *
 * Cross-line state is a bit set, so that the flags below combine:
 *   MK_NORMAL   - no rule is in effect and the previous line was complete
 *   MK_RULE     - a rule is in effect, so a line starting with a tab is its
 *                 recipe.  Make reads a tab line as a recipe only after a
 *                 rule; before one, a tab line is an ordinary line, which is
 *                 how a tab-indented assignment inside a conditional is
 *                 coloured as an assignment rather than as a command.
 *   MK_CONT     - the previous line ended with a continuation backslash
 *   MK_COMMENT  - set beside MK_CONT when that line ended inside a comment
 *   MK_DEFINE   - inside a define ... endef body
 *
 * The colorizer is written out rather than expressed as a struct lang_spec
 * for the engine in color_generic.c, because almost nothing in a makefile
 * fits that engine: a variable reference nests ($(patsubst %.c,%.o,$(SRC))),
 * an automatic variable is punctuation rather than a word ($@, $<), the
 * shape of a line depends on whether it starts with a tab, and a target is
 * recognised by the ':' that follows it rather than by a keyword table.
 *
 * One construct is deliberately not tracked: a recipe body is scanned as
 * makefile text, not as shell, so a shell keyword inside a recipe is left as
 * ordinary text.  Make hands the recipe to a shell that the makefile itself
 * chooses (SHELL = ...), so there is no single grammar to scan it with.
 */
#include "color.h"
#include <ctype.h>
#include <string.h>

/* Cross-line state bits.  See the file comment for what each one means. */
enum {
	MK_NORMAL = 0,
	MK_RULE = 1,
	MK_CONT = 2,
	MK_COMMENT = 4,
	MK_DEFINE = 8,
};

/* Shapes a logical line can have, as classified by line_shape(). */
enum {
	MK_LINE_PLAIN,
	MK_LINE_RULE,
	MK_LINE_ASSIGN,
};

/*
 * The make directives.  This is the complete set; "-include" is an alternate
 * spelling of "sinclude" and is matched by skipping the leading '-' before
 * the lookup.
 */
static const char *const mk_directives[] = {
    "define",
    "else",
    "endef",
    "endif",
    "export",
    "ifdef",
    "ifeq",
    "ifndef",
    "ifneq",
    "include",
    "load",
    "override",
    "private",
    "sinclude",
    "undefine",
    "unexport",
    "vpath",
    NULL,
};

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
ident(char c)
{
	/*
	 * == True if c can appear in a word (letter, digit, or underscore) ==
	 */
	return isalnum((unsigned char)c) || c == '_';
}

static int
token_is(const char *s, int len, const char *w)
{
	/*
	 * == True if the token [s, s+len) is exactly w ==
	 */
	return (int)strlen(w) == len && memcmp(s, w, (size_t)len) == 0;
}

static int
directive(const char *s, int len)
{
	/*
	 * == True if the token [s, s+len) is a make directive ==
	 */
	int i;

	for (i = 0; mk_directives[i]; i++) {
		if (token_is(s, len, mk_directives[i]))
			return 1;
	}
	return 0;
}

static int
prefixing(const char *s, int len)
{
	/*
	 * == True if the directive [s, s+len) may precede another word ==
	 *
	 * "else ifeq (...)" and "override define ..." are the forms this
	 * covers.  "export", "private" and "unexport" are listed too because
	 * they precede an assignment, which the caller classifies in the same
	 * pass.  This is the complete set.
	 */
	return token_is(s, len, "else") || token_is(s, len, "export") ||
	       token_is(s, len, "override") || token_is(s, len, "private") ||
	       token_is(s, len, "unexport");
}

static int
names_var(const char *s, int len)
{
	/*
	 * == True if the directive [s, s+len) is followed by a variable name ==
	 *
	 * These are the directives whose argument is the name of a variable
	 * rather than a file name or an expression, so the word after one of
	 * them is coloured as a variable.  This is the complete set.
	 */
	return token_is(s, len, "define") || token_is(s, len, "export") ||
	       token_is(s, len, "ifdef") || token_is(s, len, "ifndef") ||
	       token_is(s, len, "override") || token_is(s, len, "private") ||
	       token_is(s, len, "undefine") || token_is(s, len, "unexport");
}

static int
continued(const char *line, int len)
{
	/*
	 * == True if the line ends with a continuation backslash ==
	 *
	 * A backslash escapes the backslash before it, so only an odd number
	 * of trailing backslashes continues the line.
	 */
	int n = 0;

	while (n < len && line[len - 1 - n] == '\\')
		n++;
	return n % 2;
}

static int
trim_end(const char *line, int from, int to)
{
	/*
	 * == Index of the first trailing blank in [from, to) ==
	 */
	while (to > from && blank(line[to - 1]))
		to--;
	return to;
}

static int
word_end(const char *line, int len, int i)
{
	/*
	 * == Index just past the run of letters at line[i] ==
	 */
	while (i < len && isalpha((unsigned char)line[i]))
		i++;
	return i;
}

static int
ref_end(const char *line, int len, int i)
{
	/*
	 * == Index just past the variable reference at line[i] ==
	 *
	 * line[i] is the '$'.  A parenthesised or braced reference runs to its
	 * matching close, counting nested opens of the same delimiter, so
	 * $(patsubst %.c,%.o,$(SRC)) is one reference.  Every other form is
	 * the '$' plus one byte: $@ and the other automatic variables, the
	 * escaped '$$', and a one-letter name.
	 */
	char open, close;
	int depth;

	i++;
	if (i >= len)
		return len;
	if (line[i] != '(' && line[i] != '{')
		return i + 1;
	open = line[i];
	close = (open == '(') ? ')' : '}';
	depth = 1;
	for (i++; i < len && depth > 0; i++) {
		if (line[i] == open)
			depth++;
		else if (line[i] == close)
			depth--;
	}
	return i;
}

static int
scan_ref(const char *line, int len, int i, char *attrs)
{
	/*
	 * == Colour the variable reference at line[i] ==
	 *
	 * Returns the index just past it.
	 */
	int end = ref_end(line, len, i);

	fill_attrs(attrs, i, end, ATTR_PREPROC);
	return end;
}

static void
overlay_refs(const char *line, int from, int to, char *attrs)
{
	/*
	 * == Recolour the variable references inside [from, to) ==
	 *
	 * Used where a region has already been filled with one attribute - a
	 * target list, the body of a quoted string - and the references it
	 * contains must still stand out from it.
	 */
	int i = from;

	while (i < to) {
		if (line[i] == '$')
			i = scan_ref(line, to, i, attrs);
		else
			i++;
	}
}

static int
quote_end(const char *line, int len, int i)
{
	/*
	 * == Index just past the quoted text opening at line[i] ==
	 *
	 * A backslash escapes the next byte.  Returns len when the quote is
	 * not closed on this line; an unterminated quote is not carried into
	 * the next line, since a lone apostrophe would then discolour the rest
	 * of the file.
	 */
	char q = line[i];

	for (i++; i < len; i++) {
		if (line[i] == '\\' && i + 1 < len) {
			i++;
			continue;
		}
		if (line[i] == q)
			return i + 1;
	}
	return len;
}

static int
scan_text(const char *line, int len, int i, char *attrs)
{
	/*
	 * == Colour the value or recipe region from line[i] on ==
	 *
	 * Returns 1 when the region ended inside a comment, so that the caller
	 * can keep a continued comment a comment on the next line.
	 *
	 * Make expands a variable reference before the shell ever sees the
	 * recipe, so a reference is coloured inside either kind of quote as
	 * well as outside.  A word is scanned whole, which keeps the digits of
	 * an identifier (-O2, c99) from being coloured as a number.
	 */
	while (i < len) {
		char c = line[i];
		int start;

		if (c == '\\' && i + 1 < len) {
			i += 2;
			continue;
		}
		if (c == '#') {
			fill_attrs(attrs, i, len, ATTR_COMMENT);
			return 1;
		}
		if (c == '$') {
			i = scan_ref(line, len, i, attrs);
			continue;
		}
		if (c == '"' || c == '\'') {
			start = i;
			i = quote_end(line, len, i);
			fill_attrs(attrs, start, i, ATTR_STRING);
			overlay_refs(line, start, i, attrs);
			continue;
		}
		if (isdigit((unsigned char)c)) {
			start = i;
			while (i < len && isdigit((unsigned char)line[i]))
				i++;
			if (i >= len || !ident(line[i]))
				fill_attrs(attrs, start, i, ATTR_NUMBER);
			continue;
		}
		if (ident(c)) {
			while (i < len && ident(line[i]))
				i++;
			continue;
		}
		i++;
	}
	return 0;
}

static int
assign_follows(const char *line, int len, int i)
{
	/*
	 * == True if a rule colon or an assignment operator starts at line[i] ==
	 *
	 * Used to veto the directive reading of a leading word: a makefile is
	 * free to name a target or a variable after a directive, so "export:"
	 * is a rule and "include = x" an assignment.
	 */
	while (i < len && blank(line[i]))
		i++;
	if (i >= len)
		return 0;
	if (line[i] == ':' || line[i] == '=')
		return 1;
	if ((line[i] == '?' || line[i] == '+' || line[i] == '!') &&
	    i + 1 < len && line[i + 1] == '=')
		return 1;
	return 0;
}

static int
directive_end(const char *line, int len, int i)
{
	/*
	 * == Index just past the directive word at line[i], or i if none ==
	 *
	 * A leading '-' is accepted, for the "-include" spelling.
	 */
	int start = i, end;

	if (i < len && line[i] == '-')
		i++;
	end = word_end(line, len, i);
	if (end == i || !directive(line + i, end - i))
		return start;
	if (assign_follows(line, len, end))
		return start;
	return end;
}

static int
scan_var_name(const char *line, int len, int i, char *attrs)
{
	/*
	 * == Colour the variable name at line[i] ==
	 *
	 * Used for the argument of a directive that names a variable, as in
	 * "ifdef VERBOSE".  Returns the index just past the name, and i itself
	 * when no name is there.
	 */
	int start;

	while (i < len && blank(line[i]))
		i++;
	start = i;
	while (i < len && ident(line[i]))
		i++;
	fill_attrs(attrs, start, i, ATTR_PREPROC);
	return i;
}

static int
scan_directives(const char *line, int len, int i, char *attrs, int *body,
                int *named)
{
	/*
	 * == Colour the directive words at the head of the line ==
	 *
	 * Returns the index of the first byte that is not part of a directive
	 * word.  *body is set when one of the words is "define", which means
	 * the lines that follow are the body of a definition.  *named reports
	 * whether the last of the words takes a variable name as its argument.
	 */
	int start, end;

	for (;;) {
		while (i < len && blank(line[i]))
			i++;
		start = i;
		end = directive_end(line, len, i);
		if (end == start)
			return start;
		fill_attrs(attrs, start, end, ATTR_KEYWORD);
		if (token_is(line + start, end - start, "define"))
			*body = 1;
		*named = names_var(line + start, end - start);
		i = end;
		if (!prefixing(line + start, end - start))
			return i;
	}
}

static int
line_shape(const char *line, int len, int i, int *name_end, int *rest)
{
	/*
	 * == Classify a logical line as a rule, an assignment, or plain text ==
	 *
	 * Scans from line[i] for whichever comes first: a ':' that opens a
	 * rule, or an assignment operator.  ":=" and "::=" are assignments
	 * even though they start with a colon, and a target-specific variable
	 * (dbg: CFLAGS = -g) is a rule, because its colon comes first.  A
	 * variable reference is stepped over whole, so the colon in
	 * $(SRC:.c=.o) does not read as a rule.
	 *
	 * *name_end is the end of the target list or of the variable name,
	 * trailing blanks removed; *rest is the first byte after the colon or
	 * the operator.  Neither is set when the result is MK_LINE_PLAIN.
	 */
	int j = i, k;

	while (j < len) {
		if (line[j] == '\\' && j + 1 < len) {
			j += 2;
			continue;
		}
		if (line[j] == '$') {
			j = ref_end(line, len, j);
			continue;
		}
		if (line[j] == '#')
			break;
		if (line[j] == ':') {
			k = j + 1;
			if (k < len && line[k] == ':')
				k++;
			*name_end = trim_end(line, i, j);
			if (k < len && line[k] == '=') {
				*rest = k + 1;
				return MK_LINE_ASSIGN;
			}
			*rest = k;
			return MK_LINE_RULE;
		}
		if (line[j] == '=') {
			k = j;
			if (k > i && (line[k - 1] == '?' || line[k - 1] == '+' ||
			              line[k - 1] == '!'))
				k--;
			*name_end = trim_end(line, i, k);
			*rest = j + 1;
			return MK_LINE_ASSIGN;
		}
		j++;
	}
	return MK_LINE_PLAIN;
}

static int
recipe_prefix(const char *line, int len, int i, char *attrs)
{
	/*
	 * == Colour the recipe prefix characters at line[i] ==
	 *
	 * '@' suppresses the echo, '-' ignores a non-zero exit and '+' runs
	 * the line even under "make -n"; any of them may repeat.  Returns the
	 * index of the first byte of the command itself.
	 */
	while (i < len &&
	       (line[i] == '@' || line[i] == '-' || line[i] == '+')) {
		fill_attrs(attrs, i, i + 1, ATTR_PREPROC);
		i++;
	}
	return i;
}

static int
next_state(const char *line, int len, int rule, int in_comment)
{
	/*
	 * == State to carry into the next line ==
	 *
	 * rule says whether a rule is still in effect; in_comment says whether
	 * this line ended inside a comment, which matters only when the line
	 * is also continued.
	 */
	int st = rule ? MK_RULE : MK_NORMAL;

	if (continued(line, len))
		st |= in_comment ? (MK_CONT | MK_COMMENT) : MK_CONT;
	return st;
}

static int
make_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of a makefile and return the new state ==
	 *
	 * A line is read as one of these, in order: the continuation of the
	 * line before it, the body of a definition, a recipe (the line starts
	 * with a tab while a rule is in effect), or a fresh logical line -
	 * which is in turn blank, a comment, one or more directives, a rule,
	 * an assignment, or plain text.  That is the complete set.
	 *
	 * A blank line, a comment and a directive all leave the rule flag as
	 * they found it; a rule sets it; an assignment and plain text clear
	 * it, which is where make itself stops reading a recipe.
	 */
	int i, end, kind, rule, head, seen;
	int body = 0, named = 0, name_end = 0, rest = 0;

	fill_attrs(attrs, 0, len, ATTR_NORMAL);
	rule = state & MK_RULE;

	if (state & MK_DEFINE) {
		for (i = 0; i < len && blank(line[i]); i++)
			;
		end = word_end(line, len, i);
		if (token_is(line + i, end - i, "endef")) {
			fill_attrs(attrs, i, end, ATTR_KEYWORD);
			scan_text(line, len, end, attrs);
			return rule ? MK_RULE : MK_NORMAL;
		}
		scan_text(line, len, 0, attrs);
		return MK_DEFINE | rule;
	}

	if (state & MK_COMMENT) {
		fill_attrs(attrs, 0, len, ATTR_COMMENT);
		return next_state(line, len, rule, 1);
	}

	if (state & MK_CONT)
		return next_state(line, len, rule,
		                  scan_text(line, len, 0, attrs));

	if (rule && len > 0 && line[0] == '\t') {
		for (i = 1; i < len && blank(line[i]); i++)
			;
		i = recipe_prefix(line, len, i, attrs);
		return next_state(line, len, rule,
		                  scan_text(line, len, i, attrs));
	}

	for (i = 0; i < len && blank(line[i]); i++)
		;
	if (i == len)
		return next_state(line, len, rule, 0);
	if (line[i] == '#') {
		fill_attrs(attrs, i, len, ATTR_COMMENT);
		return next_state(line, len, rule, 1);
	}

	head = i;
	i = scan_directives(line, len, i, attrs, &body, &named);
	seen = i > head;
	if (body) {
		i = scan_var_name(line, len, i, attrs);
		scan_text(line, len, i, attrs);
		return MK_DEFINE | rule;
	}

	kind = line_shape(line, len, i, &name_end, &rest);
	if (kind == MK_LINE_RULE) {
		fill_attrs(attrs, i, name_end, ATTR_KEYWORD);
		overlay_refs(line, i, name_end, attrs);
		rule = MK_RULE;
		for (i = rest; i < len && blank(line[i]); i++)
			;
		/* A target-specific variable: "debug: CFLAGS += -g". */
		if (line_shape(line, len, i, &name_end, &rest) == MK_LINE_ASSIGN) {
			fill_attrs(attrs, i, name_end, ATTR_PREPROC);
			i = rest;
		}
	} else if (kind == MK_LINE_ASSIGN) {
		fill_attrs(attrs, i, name_end, ATTR_PREPROC);
		if (!seen)
			rule = 0;
		i = rest;
	} else {
		if (named)
			i = scan_var_name(line, len, i, attrs);
		if (!seen)
			rule = 0;
	}
	return next_state(line, len, rule, scan_text(line, len, i, attrs));
}

/*
 * A makefile normally carries no extension, so the registry matches these
 * base names, exactly or with a suffix after a dot ("Makefile.am").  The
 * extensions cover the other convention, an included fragment named for
 * what it holds ("rules.mk").  ".inc" is the same convention named for the
 * role rather than for the language; other ecosystems spell their include
 * fragments that way too, so a file that is not a makefile fragment is
 * coloured as one here.
 */
static const char *const make_extensions[] = {".mk", ".mak", ".make", ".inc",
                                              NULL};

static const char *const make_basenames[] = {"Makefile", "GNUmakefile",
                                             "BSDmakefile", NULL};

/*
 * A recipe line must begin with a real tab, and make's own documentation and
 * every generator that writes makefiles assume the traditional eight-column
 * stop, so eight is what keeps a recipe lined up as its author saw it.
 */
const struct colorizer colorizer_make = {"make", make_extensions,
                                         make_basenames, make_colorize, 8};
