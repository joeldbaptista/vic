#ifndef SRC_GAP_H
#define SRC_GAP_H

#include "vic.h"

char *buf_end(const struct editor *g);
int buf_content_size(const struct editor *g);
char *buf_next(const struct editor *g, char *p);
char *buf_prev(const struct editor *g, char *p);
unsigned char buf_char_before(const struct editor *g, char *p);
int logical_pos(const struct editor *g, const char *p);
char *phys_ptr(const struct editor *g, int n);

#endif
