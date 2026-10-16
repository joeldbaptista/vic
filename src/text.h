#ifndef SRC_TEXT_H
#define SRC_TEXT_H

#include "vic.h"

/*
 * text.h - read access to the text, which lives in a piece tree.
 *
 * A position in the text is an int byte offset: 0 is the first byte and
 * text_len() is one past the last.  -1 stands for "no position" where the
 * old char * code used NULL.  Edits go through buffer.h.
 */

int text_len(const struct editor *g);
int text_char(const struct editor *g, int p);
int text_find(const struct editor *g, int p, int c);
int text_rfind(const struct editor *g, int p, int c);
int text_read(const struct editor *g, int p, int n, char *dst);
char *text_copy(const struct editor *g, int p, int n);
void text_set_char(struct editor *g, int p, char c);
void text_set_bytes(struct editor *g, int p, const char *s, int n);
int text_line_of(const struct editor *g, int p);
int text_line_start(const struct editor *g, int line);
int text_lines(const struct editor *g);

#endif
