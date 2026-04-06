#ifndef SRC_SCREEN_H
#define SRC_SCREEN_H

#include "vic.h"

void screen_erase(struct editor *g);
void new_screen(struct editor *g, int ro, int co);
unsigned screen_line_number_width(struct editor *g);
int screen_text_columns_on_screen(struct editor *g);
void refresh(struct editor *g, int full_screen);
void redraw(struct editor *g, int full_screen);

#endif
