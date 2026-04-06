#ifndef SRC_TEXTOBJ_H
#define SRC_TEXTOBJ_H

#include "vic.h"

int textobj_find_range(struct editor *g, int ai_cmd, int obj, char **start,
                       char **stop, int *buftype);

#endif
