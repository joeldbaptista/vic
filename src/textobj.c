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
	return isalnum(ch) || ch == '_' || ch == '-' || ch == ':';
}

static int
tag_name_equal(const char *a, size_t alen, const char *b,
               size_t blen)
{
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
