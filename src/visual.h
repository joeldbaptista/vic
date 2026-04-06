#ifndef SRC_VISUAL_H
#define SRC_VISUAL_H

#include "vic.h"

void visual_leave(struct editor *g);
void visual_enter(struct editor *g, int linewise);
int visual_get_range(struct editor *g, char **start, char **stop,
                     int *buftype);
void visual_apply_operator(struct editor *g, int op);

#endif
