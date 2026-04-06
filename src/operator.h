#ifndef SRC_OPERATOR_H
#define SRC_OPERATOR_H

#include "vic.h"

void yank_sync_in(struct editor *g);
void shared_yank_in(struct editor *g);
char *text_yank(struct editor *g, char *p, char *q, int dest, int buftype);
char what_reg(const struct editor *g);
void yank_status(struct editor *g, const char *op, const char *p, int cnt);
char *yank_delete(struct editor *g, char *start, char *stop, int buftype,
                  int do_delete, int undo, int dest_reg);
char *yank_delete_current(struct editor *g, char *start, char *stop,
                          int buftype, int yf, int undo);
void operator_run_change_delete_yank_cmd(struct editor *g, const struct cmd_ctx *ctx);
void operator_run_delete_or_substitute_cmd(struct editor *g, const struct cmd_ctx *ctx);
void operator_run_change_or_delete_eol_cmd(struct editor *g, const struct cmd_ctx *ctx);
void operator_run_replace_char_cmd(struct editor *g, const struct cmd_ctx *ctx);
void operator_run_flip_case_cmd(struct editor *g);

#endif