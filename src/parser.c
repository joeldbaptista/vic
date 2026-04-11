/*
 * parser.c - Normal-mode command parser (MONRAS model).
 *
 * initparser() — reset parser state.
 * parse()      — feed one character; returns 0 (need more) or 1 (done).
 *                Check s->ok after done: 1 = valid, 0 = unrecognised.
 */
#include "parser.h"

/* composite keys; e.g. `gg`, `g*`, etc */
struct op2_rule {
	char op; /* prefix */
	const char *set; /* follow up */
	int nl_ok; /* is enter accepted? */
};

static int
match_op2(const struct parser *s, int c)
{
	/*
	 * == Check whether c is a valid second key for a two-key prefix ==
	 *
	 * Looks up s->op in the rules table.  Each rule lists the set of
	 * permitted follow-up keys and whether Enter (\r / \n) is also valid.
	 *
	 * - g: followed by g, *, #
	 * - z: followed by z, t, b, ., -, and also Enter (zEnter aligns)
	 * - Z: followed by Z, Q (save-and-quit / quit-without-saving)
	 *
	 * Returns 1 if c is accepted, 0 otherwise.
	 */
	const struct op2_rule *r;
	static const struct op2_rule rules[] = {
	    {'g', "g*#", 0},
	    {'z', "ztb.-", 1},
	    {'Z', "ZQ", 0},
	    {'\0', "", 0}, /* signal end */
	};

	if (c) {
		for (r = rules; r->op != '\0'; ++r) {
			if (r->op != s->op)
				continue;
			if (strchr(r->set, c) != NULL)
				return 1;
			if (r->nl_ok && (c == '\r' || c == '\n'))
				return 1;
			return 0;
		}
	}
	return 0;
}

int
initparser(struct parser *s)
{
	/*
	 * == Reset parser state to idle ==
	 *
	 * Zero-initialises *s so it is ready to accept the first keypress of
	 * the next Normal-mode command.  Returns -1 if s is NULL.
	 */
	if (s == NULL)
		return -1;

	memset(s, 0, sizeof(*s));
	return 0;
}

int
parse(struct parser *s, int c)
{
	/*
	 * == Feed one character into the Normal-mode command parser ==
	 *
	 * Implements the MONRAS state machine:
	 *   STG_START       — optional '"' register prefix or '+' shorthand
	 *   STG_REG         — register character (a-z, 0-9, named specials)
	 *   STG_COUNT       — optional repeat count digits before the operator
	 *   STG_OP          — the operator character (dispatch point)
	 *   STG_RANGE_COUNT — optional count for the range motion (e.g. 3 in d3j)
	 *   STG_RANGE       — the motion key (w, j, G, …) or object prefix (i/a)
	 *   STG_ANCHOR      — one follow-up character (f/t target, mark letter,
	 *                     text-object character)
	 *   STG_STRING      — accumulates chars until NUL (for / ? :)
	 *   STG_OP2         — second key of a two-key sequence (gg, gU, zz, …)
	 *
	 * Returns 0 if more characters are needed, 1 when parsing is complete.
	 * Check s->ok after returning 1: 1 = valid command, 0 = unrecognised.
	 *
	 * On return 1 the populated fields are:
	 *   s->op     — primary operator
	 *   s->op2    — second key (two-key sequences)
	 *   s->rg     — range/motion key
	 *   s->a      — anchor character (f/t target, mark, text-object)
	 *   s->m      — operator count (0 = not given)
	 *   s->n      — range count (0 = not given)
	 *   s->reg    — register character ('\0' = default)
	 *   s->b[]    — string argument (: / ? commands)
	 *   s->raw_key — non-ASCII keycode (arrow keys, etc.)
	 */
	for (;;) {
		switch (s->stg) {
		case STG_START:
			/* optional register prefix */
			if (c == '"') {
				s->stg = STG_REG;
				return 0;
			}
			/* '+' is a direct shorthand for the shared register ("+).
			 * It sets reg='+' and continues to count/op, so "+yy
			 * can be written as +yy. */
			if (c == '+') {
				s->reg = '+';
				s->stg = STG_COUNT;
				return 0;
			}
			/* complete prefix-less ops */
			/* '0' alone means "go to beginning of line"; do not set m
			 * so cmdcnt stays 0 and run_digit_cmd takes the dot_begin path */
			if (c == '0') {
				s->op = '0';
				s->ok = 1;
				return 1;
			}
			if (in_set(c, "^iaAC")) {
				s->m = 1;
				s->op = (char)c;
				s->ok = 1;
				return 1;
			}
			/* ex command */
			if (c == ':') {
				s->m = 1;
				s->op = (char)c;
				s->stg = STG_STRING;
				return 0;
			}
			/* search */
			if (in_set(c, "/?")) {
				s->m = 1;
				s->op = (char)c;
				s->stg = STG_STRING;
				return 0;
			}
			s->stg = STG_COUNT;
			continue;
		case STG_COUNT:
			if ('0' <= c && c <= '9') {
				s->m = s->m * 10 + (c - '0');
				return 0;
			}
			s->stg = STG_OP;
			continue;
		case STG_OP:
			/* control chars and KEYCODE_* values */
			if ((c > 0 && c < 0x20) || c >= PARSER_RAW_KEY_MIN) {
				s->raw_key = c;
				s->ok = 1;
				return 1;
			}
			if (in_set(c, "hjkl$wbexoOG"  /* original */
			              "nN~JpPXsS"     /* delete/subst/search */
			              "uU."           /* undo/repeat */
			              "%;,HLM|"       /* motion/screen */
			              "IRvVBEWY"      /* insert/visual/word */
			              "*#(){}-D ")) { /* search/para/shift/delete-eol/space */
				s->op = (char)c;
				s->ok = 1;
				return 1;
			}
			if (in_set(c, "dyc<>")) { /* need range */
				s->op = (char)c;
				s->stg = STG_RANGE_COUNT;
				return 0;
			}
			if (in_set(c, "fFtTr")) { /* need anchor */
				s->op = (char)c;
				s->stg = STG_ANCHOR;
				return 0;
			}
			if (in_set(c, "/?")) { /* need string */
				s->op = (char)c;
				s->stg = STG_STRING;
				return 0;
			}
			if (in_set(c, "gzZ")) { /* need second key */
				s->op = (char)c;
				s->stg = STG_OP2;
				return 0;
			}
			if (in_set(c, "'m")) { /* need anchor */
				s->op = (char)c;
				s->stg = STG_ANCHOR;
				return 0;
			}
			/* not an op */
			s->n = s->m;
			s->m = -1;
			s->ok = 0;
			return 1;
		case STG_RANGE_COUNT:
			if ('0' <= c && c <= '9') {
				s->n = s->n * 10 + (c - '0');
				return 0;
			}
			s->stg = STG_RANGE;
			continue;
		case STG_RANGE:
			if (in_set(c, "wWeEbBdyc<>"   /* characterwise */
			              "^$%0hnN|{} l" /* more characterwise (incl. space, l) */
			              "\x08\x7f"     /* BS, DEL */
			              "GHL+-jk\r\n" /* linewise */
			              )) { /* anchor-less ranges */
				s->rg = (char)c;
				s->ok = 1;
				return 1;
			}
			if (in_set(c, "iafFtT'")) { /* anchor ranges */
				s->rg = (char)c;
				s->stg = STG_ANCHOR;
				return 0;
			}
			s->ok = 0;
			return 1;
		case STG_ANCHOR:
			if (c == '\0') {
				s->ok = 0;
				return 1;
			}
			s->a = (char)c;
			s->ok = 1;
			return 1;
		case STG_STRING:
			if (c == '\0') {
				s->ok = 1;
				return 1;
			}
			if (s->k < (int)(sizeof(s->b) - 1)) {
				s->b[s->k++] = (char)c;
				s->b[s->k] = '\0';
			}
			return 0;
		case STG_OP2:
			if (match_op2(s, c)) {
				s->op2 = (char)c;
				s->ok = 1;
				return 1;
			}
			s->ok = 0;
			return 1;
		case STG_REG:
			if (c == '\0') {
				s->ok = 0;
				return 1;
			}
			if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
			    ('0' <= c && c <= '9') ||
			    strchr("\"-+#*:.%/", c) != NULL) {
				s->reg = (char)c;
				s->stg = STG_COUNT;
				return 0;
			}
			s->ok = 0;
			return 1;
		}
		/* unrecognised state */
		break;
	}
	s->ok = 0;
	return 1;
}
