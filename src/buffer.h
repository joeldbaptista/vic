#ifndef SRC_BUFFER_H
#define SRC_BUFFER_H

#include "vic.h"

uintptr_t text_hole_make(struct editor *g, char *p, int size);
char *text_hole_delete(struct editor *g, char *p, char *q, int undo);
int file_insert(struct editor *g, const char *fn, char *p, int initial);
char *char_insert(struct editor *g, char *p, char c, int undo);
void init_filename(struct editor *g, char *fn);
void update_filename(struct editor *g, char *fn);
int init_text_buffer(struct editor *g, char *fn);
uintptr_t string_insert(struct editor *g, char *p, const char *s, int undo);
int file_write(struct editor *g, char *fn, char *first, char *last);

#endif
