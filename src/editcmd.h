#ifndef SRC_EDITCMD_H
#define SRC_EDITCMD_H

#include "vic.h"

void edit_run_start_insert_cmd(struct editor *g);
void edit_run_start_replace_cmd(struct editor *g);
void edit_run_insert_before_first_nonblank_cmd(struct editor *g);
void edit_run_insert_before_cmd(struct editor *g);
void edit_run_append_after_cmd(struct editor *g);
void edit_run_append_eol_cmd(struct editor *g);
void edit_run_join_lines_cmd(struct editor *g);
void edit_run_open_line_cmd(struct editor *g, const struct cmd_ctx *ctx);

#endif
