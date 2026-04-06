#ifndef SRC_UNDO_H
#define SRC_UNDO_H

#include "vic.h"

void undo_queue_commit(struct editor *g);
void flush_undo_data(struct editor *g);
void undo_push(struct editor *g, char *src, unsigned length, int u_type);
void undo_push_insert(struct editor *g, char *p, int len, int undo);
void undo_pop(struct editor *g);
void undo_with_redo(struct editor *g);
void redo_pop(struct editor *g);

/* Persist undo history to / restore it from a sidecar file.
 * fn is the path of the file being edited.  Both functions
 * are no-ops when VI_UNDOFILE is not set in g->setops. */
void undo_save(struct editor *g, const char *fn);
void undo_load(struct editor *g, const char *fn);

#endif
