#ifndef SRC_VISUAL_H
#define SRC_VISUAL_H

#include "vic.h"

/* Per-row byte range for block visual selection: [p, q), p < q guaranteed. */
struct block_range {
	char *p; /* first byte of column window on this row (inclusive) */
	char *q; /* first byte past column window on this row (exclusive) */
};

void visual_leave(struct editor *g);
void visual_enter(struct editor *g, int mode); /* 1=char  2=line  3=block */
int visual_get_range(struct editor *g, char **start, char **stop,
                     int *buftype);
void visual_apply_operator(struct editor *g, int op);
void visual_block_insert_replay(struct editor *g);
void block_visual_cols(struct editor *g, int *col_left, int *col_right,
                       char **row_top, char **row_bot);
struct block_range *block_selection_ranges(struct editor *g, int *count);

#endif
