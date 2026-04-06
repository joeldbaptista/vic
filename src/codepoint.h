#ifndef SRC_CODEPOINT_H
#define SRC_CODEPOINT_H

#include "vic.h"

char *cp_start(struct editor *g, char *p);
char *cp_next(struct editor *g, char *p);
char *cp_prev(struct editor *g, char *p);
char *cp_end(struct editor *g, char *p);
int utf8_cell_width(const char *p, const char *e);
int next_column(struct editor *g, const char *p, int co);
int get_column(struct editor *g, char *p);

#endif
