#ifndef SRC_CODEPOINT_H
#define SRC_CODEPOINT_H

#include "vic.h"

int cp_start(struct editor *g, int p);
int cp_next(struct editor *g, int p);
int cp_prev(struct editor *g, int p);
int cp_end(struct editor *g, int p);
int utf8_cell_width(const char *p, const char *e);
int csi_len(const char *p, const char *end, int *is_sgr);
int esc_skip(struct editor *g, int p);
int esc_snap_fwd(struct editor *g, int p);
int esc_snap_bwd(struct editor *g, int p);
int next_column(struct editor *g, int p, int co);
int get_column(struct editor *g, int p);

#endif
