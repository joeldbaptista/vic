#ifndef SRC_PARSER_H
#define SRC_PARSER_H

#include <string.h>

/*
 * parser.h - Normal-mode command parser (MONRAS model).
 *
 * Parses one logical Normal-mode command into:
 *   reg     - optional register prefix ('"' + char)
 *   m       - operation count
 *   op      - primary operation key (ASCII)
 *   op2     - second key for two-key ops (gg, zt, ZZ, ...)
 *   n       - range/motion count
 *   rg      - range key
 *   a       - anchor byte (f/t target, text-object suffix, mark name)
 *   b[]     - string payload for :, /, ?
 *   raw_key - non-ASCII key (KEYCODE_* or control char); op is '\0' when set
 *
 * parse() returns 0 while more input is needed, 1 when done.
 * Check s->ok: 1 = valid command, 0 = unrecognised sequence.
 *
 * The caller passes int c so that KEYCODE_* values (>= PARSER_RAW_KEY_MIN)
 * can be handled without changing the signature.
 */

/*
 * Keys with values >= PARSER_RAW_KEY_MIN are stored in raw_key instead of op.
 * Set to 0x100 so that all plain bytes (0x00-0xFF) and control chars
 * (0x01-0x1F) can be distinguished: control chars land in raw_key when they
 * are not already handled as named operations.
 */
#define PARSER_RAW_KEY_MIN 0x100

enum parser_stage {
	STG_START = 0,       /* initial: optional register prefix */
	STG_COUNT = 1,       /* consuming operation count digits */
	STG_OP = 2,          /* reading the operation key */
	STG_RANGE_COUNT = 3, /* consuming range count digits */
	STG_RANGE = 4,       /* reading the range key */
	STG_ANCHOR = 5,      /* reading a single anchor character */
	STG_STRING = 6,      /* accumulating a string (search / ex) */
	STG_OP2 = 7,         /* reading the second key of a two-key op */
	STG_REG = 10,        /* reading the register name after '"' */
};

struct parser {
	char reg;    /* register name, or '\0' */
	int m;       /* operation count */
	char op;     /* primary operation key (ASCII), or '\0' if raw_key set */
	char op2;    /* second key, or '\0' */
	int n;       /* range count */
	char rg;     /* range key, or '\0' */
	char a;      /* anchor byte, or '\0' */
	int stg;     /* current stage (enum parser_stage) */
	int ok;      /* 1 = valid command parsed, 0 = invalid */
	char b[128]; /* string buffer for :, /, ? */
	int k;       /* write cursor into b[] */
	int raw_key; /* KEYCODE_* or control char (0x01-0x1F), or 0 */
};

int initparser(struct parser *s);
int parse(struct parser *s, int c);

/* Returns non-zero if c is in the character-set string set, and c != '\0'.
 * Using strchr directly would match the null terminator when c=='\0'. */
static inline int
in_set(int c, const char *set)
{
	return c != '\0' && strchr(set, c) != NULL;
}

#endif /* SRC_PARSER_H */
