#ifndef SRC_TERM_H
#define SRC_TERM_H

#include "vic.h"

enum {
	CURSOR_STYLE_DEFAULT = 0,
	CURSOR_STYLE_BLINK_BLOCK = 1,
	CURSOR_STYLE_BLOCK = 2,
	CURSOR_STYLE_BLINK_UNDERLINE = 3,
	CURSOR_STYLE_UNDERLINE = 4,
	CURSOR_STYLE_BLINK_PIPE = 5,
	CURSOR_STYLE_PIPE = 6,

	CURSOR_SHAPE_PIPE = CURSOR_STYLE_PIPE,
	CURSOR_SHAPE_BLOCK = CURSOR_STYLE_BLOCK,
	CURSOR_SHAPE_UNDERLINE = CURSOR_STYLE_UNDERLINE,
};

int query_screen_dimensions(struct editor *g);
int mysleep(int hund);

void term_cursor_shape_set(int shape);
int term_cursor_shape_set_configured(int shape);
int term_cursor_shape_get_configured(void);
int term_cursor_shape_set_insert(int shape);
int term_cursor_shape_get_insert(void);
void term_cursor_shape_update_for_mode(int cmd_mode);
void term_cursor_shape_init_and_set(void);
void term_cursor_shape_restore_startup(void);

void rawmode(struct editor *g);
void cookmode(struct editor *g);

void place_cursor(struct editor *g, int row, int col);
void clear_to_eol(void);
void go_bottom_and_clear_to_eol(struct editor *g);
void standout_start(void);
void standout_end(void);

#endif