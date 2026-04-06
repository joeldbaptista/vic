/*
 * color_sql.c - syntax colorizer for SQL.
 *
 * Recognised tokens:
 *   Keywords      — standard SQL DML/DDL/TCL keywords (case-insensitive)
 *   Numbers       — integer, float, hex (0x)
 *   Line comments — -- ...
 *   Block comments  (slash-star ... star-slash), spanning lines
 *   String literals '...' (standard) and "..." (quoted identifiers)
 *
 * Cross-line state:
 *   SQL_NORMAL         - ordinary code
 *   SQL_BLOCK_CMT      - inside a block comment
 *   SQL_BLOCK_CMT_STAR - inside a block comment, last byte was '*'
 */
#include "color.h"
#include <ctype.h>
#include <stddef.h>
#include <string.h>

enum {
	SQL_NORMAL = 0,
	SQL_BLOCK_CMT = 1,
	SQL_BLOCK_CMT_STAR = 2,
};

/*
 * SQL is case-insensitive so keywords are stored lower-case and matched
 * after folding the source token to lower-case.
 */
static const char *const sql_keywords[] = {
    "abort",
    "action",
    "add",
    "after",
    "all",
    "alter",
    "analyze",
    "and",
    "as",
    "asc",
    "attach",
    "autoincrement",
    "before",
    "begin",
    "between",
    "by",
    "cascade",
    "case",
    "cast",
    "check",
    "collate",
    "column",
    "commit",
    "conflict",
    "constraint",
    "create",
    "cross",
    "current_date",
    "current_time",
    "current_timestamp",
    "database",
    "default",
    "deferrable",
    "deferred",
    "delete",
    "desc",
    "detach",
    "distinct",
    "drop",
    "each",
    "else",
    "end",
    "escape",
    "except",
    "exclusive",
    "exists",
    "explain",
    "fail",
    "filter",
    "following",
    "for",
    "foreign",
    "from",
    "full",
    "glob",
    "group",
    "groups",
    "having",
    "if",
    "ignore",
    "immediate",
    "in",
    "index",
    "indexed",
    "initially",
    "inner",
    "insert",
    "instead",
    "intersect",
    "into",
    "is",
    "isnull",
    "join",
    "key",
    "left",
    "like",
    "limit",
    "match",
    "materialized",
    "natural",
    "no",
    "not",
    "nothing",
    "notnull",
    "null",
    "nulls",
    "of",
    "offset",
    "on",
    "or",
    "order",
    "others",
    "outer",
    "over",
    "partition",
    "plan",
    "pragma",
    "preceding",
    "primary",
    "query",
    "raise",
    "range",
    "recursive",
    "references",
    "regexp",
    "reindex",
    "release",
    "rename",
    "replace",
    "restrict",
    "returning",
    "right",
    "rollback",
    "row",
    "rows",
    "savepoint",
    "select",
    "set",
    "table",
    "temp",
    "temporary",
    "then",
    "ties",
    "to",
    "transaction",
    "trigger",
    "unbounded",
    "union",
    "unique",
    "update",
    "using",
    "vacuum",
    "values",
    "view",
    "virtual",
    "when",
    "where",
    "window",
    "with",
    "without",
    NULL,
};

static int
sql_is_keyword(const char *s, int len)
{
	char buf[32];
	int i;

	if (len >= (int)sizeof(buf))
		return 0;
	for (i = 0; i < len; i++)
		buf[i] = (char)tolower((unsigned char)s[i]);
	buf[len] = '\0';
	for (i = 0; sql_keywords[i]; i++) {
		if ((int)strlen(sql_keywords[i]) == len &&
		    memcmp(sql_keywords[i], buf, (size_t)len) == 0)
			return 1;
	}
	return 0;
}

static int
is_ident_start(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int
is_ident(unsigned char c)
{
	return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void
fill_attrs(char *attrs, int from, int to, char attr)
{
	int i;

	for (i = from; i < to; i++)
		attrs[i] = attr;
}

static int
sql_colorize(int state, const char *line, int len, char *attrs)
{
	int i = 0;

#define SET(from, to, a)                                      \
	do {                                                  \
		if (attrs)                                    \
			fill_attrs(attrs, (from), (to), (a)); \
	} while (0)

	if (attrs)
		fill_attrs(attrs, 0, len, ATTR_NORMAL);

	/* Resume block comment from previous line. */
	if (state == SQL_BLOCK_CMT || state == SQL_BLOCK_CMT_STAR) {
		while (i < len) {
			if (state == SQL_BLOCK_CMT_STAR && line[i] == '/') {
				SET(i, i + 1, ATTR_COMMENT);
				i++;
				state = SQL_NORMAL;
				goto normal;
			}
			state = (line[i] == '*') ? SQL_BLOCK_CMT_STAR : SQL_BLOCK_CMT;
			SET(i, i + 1, ATTR_COMMENT);
			i++;
		}
		return state;
	}

normal:
	while (i < len) {
		unsigned char c = (unsigned char)line[i];

		/* Line comment: -- */
		if (c == '-' && i + 1 < len && line[i + 1] == '-') {
			SET(i, len, ATTR_COMMENT);
			goto done;
		}

		/* Block comment: / * ... */
		if (c == '/' && i + 1 < len && line[i + 1] == '*') {
			int start = i;
			i += 2;
			state = SQL_BLOCK_CMT;
			while (i < len) {
				if (state == SQL_BLOCK_CMT_STAR && line[i] == '/') {
					i++;
					state = SQL_NORMAL;
					break;
				}
				state =
				    (line[i] == '*') ? SQL_BLOCK_CMT_STAR : SQL_BLOCK_CMT;
				i++;
			}
			SET(start, i, ATTR_COMMENT);
			if (state != SQL_NORMAL)
				goto done;
			continue;
		}

		/* Single-quoted string literal. */
		if (c == '\'') {
			int start = i++;
			while (i < len) {
				/* '' is an escaped quote inside a string */
				if (line[i] == '\'' && i + 1 < len &&
				    line[i + 1] == '\'') {
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

		/* Double-quoted identifier. */
		if (c == '"') {
			int start = i++;
			while (i < len) {
				if (line[i] == '"' && i + 1 < len &&
				    line[i + 1] == '"') {
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

		/* Backtick-quoted identifier (MySQL). */
		if (c == '`') {
			int start = i++;
			while (i < len && line[i] != '`')
				i++;
			if (i < len)
				i++;
			SET(start, i, ATTR_STRING);
			continue;
		}

		/* Number: hex or decimal/float. */
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
			SET(start, i, ATTR_NUMBER);
			continue;
		}

		/* Keyword or identifier. */
		if (is_ident_start(c)) {
			int start = i;
			while (i < len && is_ident((unsigned char)line[i]))
				i++;
			if (sql_is_keyword(line + start, i - start))
				SET(start, i, ATTR_KEYWORD);
			continue;
		}

		i++;
	}

done:
	return state;
#undef SET
}

static const char *const sql_extensions[] = {".sql", NULL};

const struct colorizer colorizer_sql = {"sql", sql_extensions, sql_colorize};
