#ifndef SRC_TEXTOBJ_H
#define SRC_TEXTOBJ_H

#include "vic.h"

int textobj_find_range(struct editor *g, int ai_cmd, int obj, int *start,
                       int *stop, int *buftype);

#endif
