#ifndef SRC_SEARCH_H
#define SRC_SEARCH_H

#include "vic.h"

char *get_input_line(struct editor *g, const char *prompt);
char *char_search(struct editor *g, char *p, const char *pat,
                  int dir_and_range);
void search_run_cmd(struct editor *g, const struct cmd_ctx *ctx);
void search_run_word_cmd(struct editor *g, int c, int whole_word);

#endif
