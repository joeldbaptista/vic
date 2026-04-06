#ifndef SRC_STATUS_H
#define SRC_STATUS_H

#include "vic.h"

void hit_return(struct editor *g);
void show_status_line(struct editor *g);
void status_line(struct editor *g, const char *format, ...);
void status_line_bold(struct editor *g, const char *format, ...);
void status_line_bold_errno(struct editor *g, const char *fn);
void not_implemented(struct editor *g, const char *s);

#endif
