#ifndef SRC_BUFFER_H
#define SRC_BUFFER_H

#include "vic.h"

int text_insert_bytes(struct editor *g, int p, const char *s, int n);
int text_hole_delete(struct editor *g, int p, int q, int undo);
int file_insert(struct editor *g, const char *fn, int p, int initial);
int char_insert(struct editor *g, int p, char c, int undo);
void init_filename(struct editor *g, char *fn);
void update_filename(struct editor *g, char *fn);
int init_text_buffer(struct editor *g, char *fn);
int string_insert(struct editor *g, int p, const char *s, int undo);
int buffer_replace_range(struct editor *g, int rs, int re,
                         const char *new_buf, int new_len);
int file_write(struct editor *g, char *fn, int first, int last);

#endif
