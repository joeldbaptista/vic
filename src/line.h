#ifndef SRC_LINE_H
#define SRC_LINE_H

#include "vic.h"

int begin_line(struct editor *g, int p);
int end_line(struct editor *g, int p);
int dollar_line(struct editor *g, int p);
int prev_line(struct editor *g, int p);
int next_line(struct editor *g, int p);
int end_screen(struct editor *g);
int count_lines(struct editor *g, int start, int stop);
int total_line_count(struct editor *g);
int find_line(struct editor *g, int li);
int next_tabstop(struct editor *g, int col);
int prev_tabstop(struct editor *g, int col);
int at_eof(struct editor *g, int s);
size_t indent_len(struct editor *g, int p);

#endif
