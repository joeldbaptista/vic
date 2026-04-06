/*
 * color.h - syntax highlighting interface.
 *
 * A colorizer is a state machine called once per character.  The integer
 * state persists across characters and across lines so that multi-line
 * constructs (block comments, continued preprocessor lines) are handled
 * correctly.
 *
 * Adding a new colorizer:
 *   1. Implement a colorize_fn and a struct colorizer.
 *   2. Add its pointer to the table in color.c.
 *
 * The state machine must treat '\n' as a character so that it can reset
 * line-sensitive state (line comments, BOL detection for preprocessor).
 */
#ifndef COLOR_H
#define COLOR_H

/* Text attribute codes — one per byte in the attrs[] array. */
#define ATTR_NORMAL 0
#define ATTR_COMMENT 1
#define ATTR_STRING 2
#define ATTR_PREPROC 3
#define ATTR_KEYWORD 4
#define ATTR_NUMBER 5
#define ATTR_COUNT 6

/*
 * colorize_fn - scan one line and fill a per-byte attribute array.
 *
 * state  incoming state (0 = start-of-file)
 * line   pointer to first byte of the line (not null-terminated)
 * len    number of bytes in the line (excluding the newline)
 * attrs  OUTPUT: attrs[i] = ATTR_* for line[i].  May be NULL when the
 *        caller only needs the returned state (pre-scan of invisible lines).
 *
 * Returns the new state to be passed for the next line.
 */
typedef int (*colorize_fn)(int state, const char *line, int len,
                           char *attrs);

struct colorizer {
	const char *name;
	const char *const
	    *extensions; /* NULL-terminated list, e.g. {".c",".h",NULL} */
	colorize_fn colorize;
};

/*
 * colorizer_find - return the colorizer for filename, or NULL.
 * Matches on the final '.' extension, case-insensitively.
 */
const struct colorizer *colorizer_find(const char *filename);

#endif /* COLOR_H */
