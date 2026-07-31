/*
 * regex.h - self-contained regex engine (vendored from neatvi).
 *
 * Replaces the platform <regex.h> so vic depends on one small, auditable
 * NFA engine instead of the libc implementation. A pattern is compiled to
 * an instruction array (regex_t) and matched by a tiny VM that walks it
 * (fork/jump/mark/match) — see regex.c.
 *
 * The grammar is always POSIX Extended (ERE); REG_EXTENDED is accepted
 * for source compatibility with existing call sites but has no effect.
 */
#ifndef SRC_REGEX_H
#define SRC_REGEX_H

#define REG_EXTENDED 0x01
#define REG_NOSUB 0x02
#define REG_ICASE 0x04
#define REG_NEWLINE 0x08
#define REG_NOTBOL 0x10
#define REG_NOTEOL 0x20
#define REG_EOLSTOP 0x40

typedef struct {
	long rm_so;
	long rm_eo;
} regmatch_t;

typedef struct regex *regex_t;

/* regcomp compiles pat into preg under flg (REG_* above). Returns 0 on
 * success, nonzero on a malformed pattern. */
int regcomp(regex_t *preg, const char *pat, int flg);

/* regexec matches preg against s. psub[0..nsub) receive submatch offsets
 * (byte offsets from s); unmatched groups get rm_so = rm_eo = -1.
 * Returns 0 on a match, nonzero otherwise. */
int regexec(const regex_t *preg, char *s, int nsub, regmatch_t psub[],
            int flg);

/* regerror is a stub kept for source compatibility; always returns 0. */
int regerror(int errcode, regex_t *preg, char *errbuf, int errbuf_size);

/* regfree releases the compiled pattern in preg. *preg == NULL (e.g. a
 * regex_t zeroed before a regcomp() that then failed) is a safe no-op,
 * mirroring free(NULL). */
void regfree(regex_t *preg);

#endif
