#ifndef SRC_STATUS_H
#define SRC_STATUS_H

#include "vic.h"

void hit_return(struct editor *g);
void show_status_line(struct editor *g);
void status_line(struct editor *g, const char *format, ...);
void status_line_bold(struct editor *g, const char *format, ...);
void status_line_bold_errno(struct editor *g, const char *fn);
void not_implemented(struct editor *g, const char *s);
/* Copy s into buf with control characters rendered as ^X and non-printables
 * as '?', so untrusted text can be shown on the status line without letting
 * escape sequences reach the terminal.  Output is truncated to fit. */
#define PRINT_LITERAL_LEN 128
void print_literal(char *buf, const char *s);

#endif
