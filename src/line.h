#ifndef SRC_LINE_H
#define SRC_LINE_H

#include "vic.h"

char *begin_line(struct editor *g, char *p);
char *end_line(struct editor *g, char *p);
char *dollar_line(struct editor *g, char *p);
char *prev_line(struct editor *g, char *p);
char *next_line(struct editor *g, char *p);
char *end_screen(struct editor *g);
int count_lines(struct editor *g, char *start, char *stop);
int total_line_count(struct editor *g);
char *find_line(struct editor *g, int li);
int next_tabstop(struct editor *g, int col);
int prev_tabstop(struct editor *g, int col);
int at_eof(struct editor *g, const char *s);
size_t indent_len(struct editor *g, char *p);

#endif
