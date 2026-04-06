#ifndef UTF8_H
#define UTF8_H

#include <stddef.h>

/* stepfwd steps forward one utf-8 codepoint from p within [p, e). */
const char *stepfwd(const char *p, const char *e);

/* stepbwd steps backward one utf-8 codepoint from p within string s. */
const char *stepbwd(const char *p, const char *s);

#endif
