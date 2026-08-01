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
 * Scanning is done by the generic engine in color_generic.c; see
 * color_generic.h for the cross-line state encoding.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

/*
 * SQL is case-insensitive so keywords are stored lower-case and matched
 * after folding the source token to lower-case (lang_spec.keywords_ci).
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

/*
 * '...' (standard literal) and "..." (quoted identifier) both use ''/""
 * doubling to escape an embedded quote; `...` (MySQL quoted identifier)
 * has no escape mechanism.  None of the three span lines.
 */
static const struct str_spec sql_strings[] = {
    {.open = '\'', .doubled_escape = 1},
    {.open = '"', .doubled_escape = 1},
    {.open = '`'},
    {0},
};

static const struct lang_spec sql_spec = {
    .keywords = sql_keywords,
    .keywords_ci = 1,
    .line_comment = "--",
    .block_open = "/*",
    .block_close = "*/",
    .strings = sql_strings,
    .num = {.hex = 1},
};

static int
sql_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of SQL ==
	 */
	return colorize_generic(state, line, len, attrs, &sql_spec);
}

static const char *const sql_extensions[] = {".sql", NULL};

const struct colorizer colorizer_sql = {"sql", sql_extensions, sql_colorize};
