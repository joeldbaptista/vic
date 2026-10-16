#ifndef SRC_RUN_H
#define SRC_RUN_H

/*
 * run.h - `:run <command> [args...]` dispatch interface.
 *
 * A run_fn receives:
 *   g           — editor state (buffer, cursor, undo, status bar, …)
 *   argc/argv   — argv[0] is the command name; argv[1..] are the extra args
 *   range_start — offset of the first byte of the address range
 *   range_end   — offset of the last byte of the address range (inclusive)
 *
 * When no address is given, range_start == 0 and
 * range_end == text_len(g) - 1 (the entire buffer).
 *
 * Adding a new command:
 *   1. Write a run_fn in run.c (or a dedicated file).
 *   2. Add an entry to run_table[] in run.c.
 */

#include "vic.h"

typedef void (*run_fn)(struct editor *g, int argc, char *argv[],
                       int range_start, int range_end);

struct run_entry {
	const char *name;
	run_fn fn;
};

/*
 * run_dispatch — look up argv[0] in run_table and call the matching function.
 * Prints an error on the status bar if the command is not found.
 */
void run_dispatch(struct editor *g, int argc, char *argv[],
                  int range_start, int range_end);

#endif /* SRC_RUN_H */
