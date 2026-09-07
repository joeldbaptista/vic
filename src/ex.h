#ifndef SRC_EX_H
#define SRC_EX_H

#include "vic.h"

void colon(struct editor *g, char *buf);
void colon_do_filter(struct editor *g, char *q, char *r, const char *cmd);
void filter_prompt_and_run(struct editor *g, char *q, char *r,
                           const char *prompt);

#endif
