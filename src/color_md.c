/*
 * color_md.c - syntax colorizer for Markdown.
 *
 * Token mapping:
 *   ATTR_KEYWORD  — headings (#, ##, ...), bold (**...**), horiz. rules
 *   ATTR_COMMENT  — blockquotes (>) and HTML comments (<!-- ... -->)
 *   ATTR_STRING   — inline code (`...`) and fenced code block content
 *   ATTR_PREPROC  — italic (*...* / _..._) and link label ([...])
 *   ATTR_NUMBER   — link URL ((http://...))
 *
 * Cross-line state:
 *   MD_NORMAL     - ordinary text
 *   MD_CODE_BLOCK - inside a fenced code block (``` or ~~~)
 *   MD_HTML_CMT   - inside an HTML comment (<!-- ... -->)
 */
#include "color.h"
#include <string.h>

enum {
	MD_NORMAL = 0,
	MD_CODE_BLOCK = 1,
	MD_HTML_CMT = 2,
};

static void
fill_attrs(char *attrs, int from, int to, char attr)
{
	int i;

	for (i = from; i < to; i++)
		attrs[i] = attr;
}

static int
is_fence(const char *line, int len)
{
	return len >= 3 &&
	       ((line[0] == '`' && line[1] == '`' && line[2] == '`') ||
	        (line[0] == '~' && line[1] == '~' && line[2] == '~'));
}

static int
md_colorize(int state, const char *line, int len, char *attrs)
{
	int i;

#define SET(from, to, a)                                      \
	do {                                                  \
		if (attrs)                                    \
			fill_attrs(attrs, (from), (to), (a)); \
	} while (0)

	if (attrs)
		fill_attrs(attrs, 0, len, ATTR_NORMAL);

	/* Fenced code block body (and closing fence). */
	if (state == MD_CODE_BLOCK) {
		SET(0, len, ATTR_STRING);
		return is_fence(line, len) ? MD_NORMAL : MD_CODE_BLOCK;
	}

	/* HTML comment continuation. */
	if (state == MD_HTML_CMT) {
		for (i = 0; i + 2 < len; i++) {
			if (line[i] == '-' && line[i + 1] == '-' &&
			    line[i + 2] == '>') {
				SET(0, i + 3, ATTR_COMMENT);
				/* fall through to normal scan at i+3 */
				i += 3;
				goto inline_scan;
			}
		}
		SET(0, len, ATTR_COMMENT);
		return MD_HTML_CMT;
	}

	/* Opening fence. */
	if (is_fence(line, len)) {
		SET(0, len, ATTR_STRING);
		return MD_CODE_BLOCK;
	}

	/* Heading: one or more '#' followed by ' '. */
	if (line[0] == '#') {
		i = 0;
		while (i < len && line[i] == '#')
			i++;
		if (i < len && line[i] == ' ') {
			SET(0, len, ATTR_KEYWORD);
			return MD_NORMAL;
		}
	}

	/* Blockquote: line begins with '>'. */
	if (line[0] == '>') {
		SET(0, len, ATTR_COMMENT);
		return MD_NORMAL;
	}

	/* Horizontal rule: 3+ of the same char (-, *, _), spaces allowed. */
	if (len >= 3 &&
	    (line[0] == '-' || line[0] == '*' || line[0] == '_')) {
		char ch = line[0];
		int all = 1;
		for (i = 0; i < len; i++) {
			if (line[i] != ch && line[i] != ' ') {
				all = 0;
				break;
			}
		}
		if (all) {
			SET(0, len, ATTR_KEYWORD);
			return MD_NORMAL;
		}
	}

	i = 0;
inline_scan:
	while (i < len) {
		unsigned char c = (unsigned char)line[i];

		/* HTML comment open <!-- ... --> */
		if (c == '<' && i + 3 < len && line[i + 1] == '!' &&
		    line[i + 2] == '-' && line[i + 3] == '-') {
			int start = i;
			i += 4;
			while (i + 2 < len) {
				if (line[i] == '-' && line[i + 1] == '-' &&
				    line[i + 2] == '>') {
					i += 3;
					SET(start, i, ATTR_COMMENT);
					goto inline_scan; /* re-enter outer loop */
				}
				i++;
			}
			/* comment not closed on this line */
			SET(start, len, ATTR_COMMENT);
			return MD_HTML_CMT;
		}

		/* Inline code: `...` (greedy to matching backtick). */
		if (c == '`') {
			int start = i++;
			while (i < len && line[i] != '`')
				i++;
			if (i < len)
				i++;
			SET(start, i, ATTR_STRING);
			continue;
		}

		/* Bold **...** or __...__ */
		if ((c == '*' || c == '_') && i + 1 < len &&
		    line[i + 1] == (char)c) {
			int start = i;
			i += 2;
			while (i + 1 < len &&
			       !(line[i] == (char)c && line[i + 1] == (char)c))
				i++;
			if (i + 1 < len)
				i += 2;
			SET(start, i, ATTR_KEYWORD);
			continue;
		}

		/* Italic *...* or _..._ */
		if (c == '*' || (c == '_' && (i == 0 || line[i - 1] == ' '))) {
			int start = i++;
			while (i < len && line[i] != (char)c)
				i++;
			if (i < len)
				i++;
			SET(start, i, ATTR_PREPROC);
			continue;
		}

		/* Link: [label](url) */
		if (c == '[') {
			int start = i++;
			while (i < len && line[i] != ']')
				i++;
			if (i < len)
				i++; /* consume ']' */
			SET(start, i, ATTR_PREPROC);
			if (i < len && line[i] == '(') {
				int url_start = i++;
				while (i < len && line[i] != ')')
					i++;
				if (i < len)
					i++;
				SET(url_start, i, ATTR_NUMBER);
			}
			continue;
		}

		i++;
	}

	return MD_NORMAL;
#undef SET
}

static const char *const md_extensions[] = {
    ".md", ".markdown", ".mkd", ".mdwn", ".mdown", NULL};

const struct colorizer colorizer_md = {"md", md_extensions, md_colorize};
