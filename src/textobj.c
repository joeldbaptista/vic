/*
 * textobj.c - text object range resolution.
 *
 * textobj_find_range() is called by range.c when the motion character is
 * 'a' or 'i'.  It dispatches on the object character to one of:
 *
 *   find_delimited_text_object_range — (, ), [, ], {, }, <, >  (matched pairs)
 *   find_quote_text_object_range     — ', ", `  (same-character pairs)
 *   find_tag_text_object_range       — t  (XML/HTML <tag>...</tag>)
 *   find_word_text_object_range      — w, W  (inner/around word or WORD)
 *
 * Each returns start/stop buffer pointers.  "inner" (i) excludes the
 * delimiter/surrounding whitespace; "around" (a) includes it.
 *
 * cmdcnt (e.g. 2iw) causes the range to be extended that many times.
 *
 * Upward calls go through textobj_hooks (cp_prev, cp_next,
 * begin_line, end_line, find_pair, indicate_error).
 */
#include "textobj.h"

#include "codepoint.h"
#include "line.h"

enum word_obj_class {
	WOBJ_NONE = 0,
	WOBJ_WORD,
	WOBJ_PUNCT,
	WOBJ_NONSPACE,
};

static int
is_ascii_space(unsigned char c)
{
	return c < UTF8_MULTIBYTE_MIN && isspace(c);
}

static int
is_word_byte(unsigned char c)
{
	return c >= UTF8_MULTIBYTE_MIN || isalnum(c) || c == '_';
}

static int
is_ascii_punct(unsigned char c)
{
	return c < UTF8_MULTIBYTE_MIN && ispunct(c);
}

static int
normalize_text_object_delimiter(int obj, char *open)
{
	/*
	 * == Map both halves of a delimiter pair to the open character ==
	 *
	 * ( and ) → '(',  [ and ] → '[',  { and } → '{',  < and > → '<'.
	 * Sets *open to the canonical opener and returns 1.  Returns 0 if obj
	 * is not a recognised bracket character.
	 */
	switch (obj) {
	case '(':
	case ')':
		*open = '(';
		return 1;
	case '[':
	case ']':
		*open = '[';
		return 1;
	case '{':
	case '}':
		*open = '{';
		return 1;
	case '<':
	case '>':
		*open = '<';
		return 1;
	}

	return 0;
}

static int
find_delimited_text_object_range(struct editor *g, char open,
                                 int inner, char **start,
                                 char **stop)
{
	/*
	 * == Find the innermost balanced pair enclosing dot ==
	 *
	 * Scans left from dot for an open delimiter whose paired closer lies at
	 * or after dot, using find_pair() to locate the matching closer.
	 * Repeats g->cmdcnt times (e.g. 2i( steps up two nesting levels).
	 * inner=1 excludes the delimiters themselves; inner=0 includes them.
	 * Returns 0 on success with start/stop set; -1 if no enclosing pair
	 * exists.
	 */
	char *scan;
	char *left = NULL;
	char *right = NULL;
	int repeat = (g->cmdcnt ?: 1);

	scan = g->dot;
	while (repeat-- > 0) {
		left = NULL;
		right = NULL;
		for (;;) {
			if (*scan == open) {
				char *match = find_pair(g, scan, open);
				if (match != NULL && match >= g->dot) {
					left = scan;
					right = match;
					break;
				}
			}
			if (scan == g->text)
				break;
			scan = cp_prev(g, scan);
		}

		if (left == NULL || right == NULL)
			return -1;

		if (repeat > 0) {
			if (left == g->text)
				return -1;
			scan = cp_prev(g, left);
		}
	}

	if (inner) {
		char *inner_start = cp_next(g, left);
		char *inner_stop = cp_prev(g, right);

		if (inner_start > inner_stop)
			return -1;
		left = inner_start;
		right = inner_stop;
	}

	*start = left;
	*stop = right;
	return 0;
}

static int
is_escaped_quote(struct editor *g, char *p)
{
	/*
	 * == True if the quote character at p is preceded by an odd number of backslashes ==
	 *
	 * Used to skip over escaped quotes (\" \' \`) when searching for
	 * quote text object boundaries.
	 */
	int backslashes = 0;

	while (p > g->text) {
		char *prev = cp_prev(g, p);
		if (*prev != '\\')
			break;
		backslashes++;
		p = prev;
	}

	return backslashes & 1;
}

static int
find_quoted_text_object_range(struct editor *g, char quote,
                              int inner, char **start, char **stop)
{
	/*
	 * == Find the innermost quoted string enclosing or starting at dot ==
	 *
	 * Scans left to find the nearest unescaped quote character, then
	 * forward for the matching close quote.  Supports ', ", and `.
	 * inner=1 gives the content between the quotes; inner=0 includes them.
	 * Returns 0 on success, -1 if no enclosing pair is found.
	 */
	char *scan;
	char *left = NULL;
	char *right = NULL;
	int repeat = (g->cmdcnt ?: 1);

	scan = g->dot;
	while (repeat-- > 0) {
		left = NULL;
		right = NULL;

		for (;;) {
			if (*scan == quote && !is_escaped_quote(g, scan)) {
				left = scan;
				break;
			}
			if (scan == g->text)
				break;
			scan = cp_prev(g, scan);
		}

		if (left == NULL)
			return -1;

		scan = cp_next(g, left);
		while (scan < g->end) {
			if (*scan == quote && !is_escaped_quote(g, scan)) {
				right = scan;
				break;
			}
			scan = cp_next(g, scan);
		}

		if (right == NULL)
			return -1;

		if (repeat > 0) {
			if (left == g->text)
				return -1;
			scan = cp_prev(g, left);
		}
	}

	if (inner) {
		char *inner_start = cp_next(g, left);
		char *inner_stop = cp_prev(g, right);

		if (inner_start > inner_stop)
			return -1;
		left = inner_start;
		right = inner_stop;
	}

	*start = left;
	*stop = right;
	return 0;
}

static int
is_tag_name_char(unsigned char ch)
{
	/*
	 * == True if ch is a valid XML/HTML tag name character ==
	 */
	return isalnum(ch) || ch == '_' || ch == '-' || ch == ':';
}

static int
tag_name_equal(const char *a, size_t alen, const char *b,
               size_t blen)
{
	/*
	 * == Case-insensitive comparison of two tag name byte slices ==
	 *
	 * Returns 1 if the names are the same length and equal under
	 * tolower(); 0 otherwise.
	 */
	size_t i;

	if (alen != blen)
		return 0;

	for (i = 0; i < alen; i++) {
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
			return 0;
	}

	return 1;
}

static int
parse_tag_at(struct editor *g, char *lt, char **gt,
             char **name_start, size_t *name_len, int *is_closing,
             int *is_self_closing)
{
	/*
	 * == Parse an HTML/XML tag starting at lt ==
	 *
	 * lt must point to a '<' byte.  On success:
	 *   *gt          — pointer to the closing '>'
	 *   *name_start  — start of the tag name (past '<' or '</')
	 *   *name_len    — byte length of the tag name
	 *   *is_closing  — 1 if this is a </tag>, 0 for <tag>
	 *   *is_self_closing — 1 if the tag ends with '/>' (e.g. <br/>)
	 * Returns 1 on success, 0 if lt does not start a valid element tag
	 * (e.g. declarations, processing instructions, malformed markup).
	 */
	char *p;
	int in_quote = 0;
	char quote = '\0';

	if (lt >= g->end || *lt != '<')
		return 0;

	p = lt + 1;
	if (p >= g->end)
		return 0;

	*is_closing = 0;
	if (*p == '/') {
		*is_closing = 1;
		p++;
		if (p >= g->end)
			return 0;
	}

	if (*p == '!' || *p == '?')
		return 0;

	if (!is_tag_name_char((unsigned char)*p))
		return 0;

	*name_start = p;
	while (p < g->end && is_tag_name_char((unsigned char)*p))
		p++;
	*name_len = p - *name_start;

	while (p < g->end) {
		if (in_quote) {
			if (*p == quote && !is_escaped_quote(g, p))
				in_quote = 0;
		} else {
			if (*p == '"' || *p == '\'') {
				in_quote = 1;
				quote = *p;
			} else if (*p == '>') {
				break;
			}
		}
		p++;
	}

	if (p >= g->end || *p != '>')
		return 0;

	*gt = p;
	*is_self_closing = 0;
	if (!*is_closing) {
		char *q = p;
		while (q > lt + 1 && is_ascii_space((unsigned char)q[-1]))
			q--;
		if (q > lt + 1 && q[-1] == '/')
			*is_self_closing = 1;
	}

	return 1;
}

static int
find_matching_closing_tag(struct editor *g, char *from,
                          const char *name, size_t name_len,
                          char **close_lt, char **close_gt)
{
	/*
	 * == Find the matching closing tag for an already-found opening tag ==
	 *
	 * Scans forward from `from`, tracking nesting depth.  Each matching
	 * open tag increments depth; each matching close tag decrements.  When
	 * depth reaches 0 the corresponding closer has been found.  Sets
	 * *close_lt and *close_gt to the '<' and '>' of the closing tag.
	 * Returns 1 on success, 0 if no match is found.
	 */
	char *p;
	int depth = 1;

	for (p = from; p < g->end; p++) {
		if (*p == '<') {
			char *gt;
			char *tag_name;
			size_t tag_name_len;
			int is_closing;
			int is_self_closing;

			if (!parse_tag_at(g, p, &gt, &tag_name, &tag_name_len, &is_closing,
			                  &is_self_closing))
				continue;

			if (tag_name_equal(name, name_len, tag_name, tag_name_len)) {
				if (is_closing) {
					depth--;
					if (depth == 0) {
						*close_lt = p;
						*close_gt = gt;
						return 1;
					}
				} else if (!is_self_closing) {
					depth++;
				}
			}

			p = gt;
		}
	}

	return 0;
}

static int
find_tag_text_object_range(struct editor *g, int inner, char **start,
                           char **stop)
{
	/*
	 * == Find the enclosing HTML/XML tag pair for the 't' text object ==
	 *
	 * Scans left from dot to find the nearest non-self-closing, non-closing
	 * open tag whose close tag covers dot.  Repeats g->cmdcnt times.
	 * inner=1 gives the content between the tags; inner=0 includes both
	 * the open and close tags.  Returns 0 on success, -1 on failure.
	 */
	char *anchor = g->dot;
	char *cursor = g->dot;
	char *open_lt = NULL;
	char *open_gt = NULL;
	char *close_lt = NULL;
	char *close_gt = NULL;
	int repeat = (g->cmdcnt ?: 1);

	while (repeat-- > 0) {
		char *p;
		int found = 0;

		for (p = cursor;; p = cp_prev(g, p)) {
			if (*p == '<') {
				char *gt;
				char *name;
				size_t name_len;
				int is_closing;
				int is_self_closing;

				if (parse_tag_at(g, p, &gt, &name, &name_len, &is_closing,
				                 &is_self_closing) &&
				    !is_closing && !is_self_closing &&
				    find_matching_closing_tag(g, gt + 1, name, name_len, &close_lt,
				                              &close_gt) &&
				    p <= anchor && close_gt >= anchor) {
					open_lt = p;
					open_gt = gt;
					found = 1;
					break;
				}
			}

			if (p == g->text)
				break;
		}

		if (!found)
			return -1;

		if (repeat > 0) {
			if (open_lt == g->text)
				return -1;
			cursor = cp_prev(g, open_lt);
		}
	}

	if (inner) {
		char *inner_start = open_gt + 1;
		char *inner_stop = close_lt - 1;

		if (inner_start > inner_stop)
			return -1;
		*start = inner_start;
		*stop = inner_stop;
	} else {
		*start = open_lt;
		*stop = close_gt;
	}

	return 0;
}

static int
word_obj_classify(unsigned char ch, int bigword)
{
	/*
	 * == Classify a byte as WORD, PUNCT, NONSPACE, or NONE for word objects ==
	 *
	 * bigword=1 treats any non-whitespace as the same class (WOBJ_NONSPACE).
	 * bigword=0 distinguishes alnum+underscore (WOBJ_WORD) from punctuation
	 * (WOBJ_PUNCT).  Newlines and spaces are WOBJ_NONE (not part of any word).
	 */
	if (ch == '\n' || is_ascii_space(ch))
		return WOBJ_NONE;
	if (bigword)
		return WOBJ_NONSPACE;
	if (is_word_byte(ch))
		return WOBJ_WORD;
	if (is_ascii_punct(ch))
		return WOBJ_PUNCT;
	return WOBJ_NONE;
}

static int
word_obj_member(unsigned char ch, int cls, int bigword)
{
	/*
	 * == True if ch belongs to the same word-object class as cls ==
	 *
	 * Used when extending the word boundary left/right from the initial
	 * scan point.
	 */
	if (bigword)
		return ch != '\n' && !is_ascii_space(ch);
	if (cls == WOBJ_WORD)
		return is_word_byte(ch);
	if (cls == WOBJ_PUNCT)
		return is_ascii_punct(ch);
	return 0;
}

static int
find_word_text_object_range(struct editor *g, int inner, int bigword,
                            char **start, char **stop)
{
	/*
	 * == Find the word or WORD text object range enclosing dot ==
	 *
	 * Determines the character class at dot, then extends left and right
	 * to the full run of that class.  For "around" (inner=0), also
	 * includes trailing whitespace (or, if there is none, leading
	 * whitespace).  Repeats g->cmdcnt times.
	 * bigword=1 treats any non-whitespace run as one token (W/B/E-style).
	 * Returns 0 on success, -1 if dot is on whitespace or the edge.
	 */
	char *scan = g->dot;
	char *left = NULL;
	char *right = NULL;
	int repeat = (g->cmdcnt ?: 1);

	while (repeat-- > 0) {
		char *line_start;
		char *line_end;
		int cls;

		if (scan >= g->end || *scan == '\n') {
			if (scan == g->text)
				return -1;
			scan = cp_prev(g, scan);
		}

		line_start = begin_line(g, scan);
		line_end = end_line(g, scan);

		if (is_ascii_space((unsigned char)*scan)) {
			char *tmp = scan;
			while (tmp < line_end && is_ascii_space((unsigned char)*tmp))
				tmp = cp_next(g, tmp);
			if (tmp < line_end) {
				scan = tmp;
			} else {
				tmp = scan;
				while (tmp > line_start) {
					char *prev = cp_prev(g, tmp);
					if (!is_ascii_space((unsigned char)*prev)) {
						tmp = prev;
						break;
					}
					tmp = prev;
				}
				if (is_ascii_space((unsigned char)*tmp) || *tmp == '\n')
					return -1;
				scan = tmp;
			}
		}

		cls = word_obj_classify((unsigned char)*scan, bigword);
		if (cls == WOBJ_NONE)
			return -1;

		left = scan;
		while (left > line_start) {
			char *prev = cp_prev(g, left);
			if (!word_obj_member((unsigned char)*prev, cls, bigword))
				break;
			left = prev;
		}

		right = scan;
		while (right < line_end) {
			char *next = cp_next(g, right);
			if (next >= line_end)
				break;
			if (!word_obj_member((unsigned char)*next, cls, bigword))
				break;
			right = next;
		}

		if (!inner) {
			char *next = cp_next(g, right);
			int had_trailing = 0;

			while (next < line_end && is_ascii_space((unsigned char)*next)) {
				right = next;
				had_trailing = 1;
				next = cp_next(g, next);
			}

			if (!had_trailing) {
				while (left > line_start) {
					char *prev = cp_prev(g, left);
					if (!is_ascii_space((unsigned char)*prev))
						break;
					left = prev;
				}
			}
		}

		if (repeat > 0) {
			char *next = cp_next(g, right);
			if (next >= g->end || *next == '\n')
				return -1;
			scan = next;
		}
	}

	*start = left;
	*stop = right;
	return 0;
}

int
textobj_find_range(struct editor *g, int ai_cmd, int obj, char **start,
                   char **stop, int *buftype)
{
	/*
	 * == Resolve a text-object range for use by operators ==
	 *
	 * Dispatches on obj to find the appropriate start/stop buffer pointers
	 * and sets *buftype (PARTIAL, MULTI, or WHOLE) to tell the operator
	 * whether the selection spans partial lines, multiple lines, or whole
	 * lines.
	 *
	 * ai_cmd == 'i' → inner; ai_cmd == 'a' → around.
	 *
	 * Supported objects:
	 *   (, ), [, ], {, }, <, >  — balanced delimiter pairs
	 *   ', ", `                 — quoted string pairs
	 *   w, W                    — word / WORD
	 *   t                       — XML/HTML tag pair
	 *
	 * Returns 0 on success, -1 (and calls indicate_error) on failure.
	 */
	char *p;
	char *q;
	char open;

	p = q = g->dot;

	if (normalize_text_object_delimiter(obj, &open)) {
		if (find_delimited_text_object_range(g, open, ai_cmd == 'i', &p, &q) == 0) {
			q = cp_end(g, q) - 1;
			*buftype = memchr(p, '\n', (size_t)(q - p + 1)) ? MULTI
			                                                : PARTIAL;
			*start = p;
			*stop = q;
			return 0;
		}
	} else if (obj == '"' || obj == '\'' || obj == '`') {
		if (find_quoted_text_object_range(g, obj, ai_cmd == 'i', &p, &q) == 0) {
			q = cp_end(g, q) - 1;
			*buftype = PARTIAL;
			*start = p;
			*stop = q;
			return 0;
		}
	} else if (obj == 'w' || obj == 'W') {
		if (find_word_text_object_range(g, ai_cmd == 'i', obj == 'W', &p, &q) ==
		    0) {
			q = cp_end(g, q) - 1;
			*buftype = PARTIAL;
			*start = p;
			*stop = q;
			return 0;
		}
	} else if (obj == 't') {
		if (find_tag_text_object_range(g, ai_cmd == 'i', &p, &q) == 0) {
			q = cp_end(g, q) - 1;
			*buftype = memchr(p, '\n', (size_t)(q - p + 1)) ? MULTI
			                                                : PARTIAL;
			*start = p;
			*stop = q;
			return 0;
		}
	}

	if (obj != 27)
		indicate_error(g);
	return -1;
}
