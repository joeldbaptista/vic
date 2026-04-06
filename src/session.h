#ifndef SRC_SESSION_H
#define SRC_SESSION_H

#include "vic.h"

struct cli_options {
	int usage_only;
	int help;
	int readonly;
};

void run_cmds(struct editor *g, char *p);
void append_initial_cmd(struct editor *g, const char *cmd);
void run_initial_cmds(struct editor *g);
char *load_startup_cmds(struct editor *g);
void run_startup_cmds(struct editor *g);
int parse_cli_options(struct editor *g, int argc, char **argv,
                      struct cli_options *opts);
int apply_cli_options(struct editor *g, const struct cli_options *opts);
void init_globals(struct editor *g);
void run_file_loop(struct editor *g, char **argv);
void run_editor_session(struct editor *g, char **argv);

#endif
