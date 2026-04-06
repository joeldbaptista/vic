#ifndef SRC_WORDMOTION_H
#define SRC_WORDMOTION_H

#include "vic.h"

void run_word_forward_cmd(struct editor *g);
void run_word_backward_cmd(struct editor *g);
void run_word_end_cmd(struct editor *g);
void run_blank_word_backward_cmd(struct editor *g);
void run_blank_word_end_cmd(struct editor *g);
void run_blank_word_forward_cmd(struct editor *g);

#endif
