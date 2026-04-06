#ifndef SRC_MOTION_H
#define SRC_MOTION_H

#include "vic.h"

void dot_left(struct editor *g);
void dot_right(struct editor *g);
void dot_begin(struct editor *g);
void dot_end(struct editor *g);
char *move_to_col(struct editor *g, char *p, int l);
void dot_next(struct editor *g);
void dot_prev(struct editor *g);
char *next_empty_line(struct editor *g, char *p);
char *prev_empty_line(struct editor *g, char *p);
void dot_skip_over_ws(struct editor *g);
void dot_to_char(struct editor *g, int cmd);
void motion_run_repeat_search_same_cmd(struct editor *g);
void motion_run_repeat_search_reverse_cmd(struct editor *g);
void motion_run_find_char_cmd(struct editor *g, const struct cmd_ctx *ctx);
void motion_run_first_nonblank_cmd(struct editor *g);
void motion_run_screen_top_cmd(struct editor *g);
void motion_run_screen_bottom_cmd(struct editor *g);
void motion_run_screen_middle_cmd(struct editor *g);
void motion_run_goto_column_cmd(struct editor *g);
void motion_run_goto_line_cmd(struct editor *g);
void motion_run_left_cmd(struct editor *g);
void motion_run_right_cmd(struct editor *g);
void motion_run_prev_empty_line_cmd(struct editor *g);
void motion_run_next_empty_line_cmd(struct editor *g);
void motion_run_next_line_keep_col_cmd(struct editor *g);
void motion_run_next_line_skip_ws_cmd(struct editor *g);
void motion_run_prev_line_skip_ws_cmd(struct editor *g);
void motion_run_line_end_cmd(struct editor *g);
int motion_run_paragraph_cmd(struct editor *g, int c);
void motion_run_scroll_to_screenpos_cmd(struct editor *g, const struct cmd_ctx *ctx);
void dot_scroll(struct editor *g, int cnt, int dir);
char *bound_dot(struct editor *g, char *p);

#endif