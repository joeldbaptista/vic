#ifndef SRC_EXCORE_H
#define SRC_EXCORE_H

#include "vic.h"

void setops(struct editor *g, char *args, int flg_no);
char *expand_args(struct editor *g, char *args);

#endif
