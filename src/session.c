/*
 * session.c - startup, CLI handling, and the top-level file loop.
 *
 * parse_cli_options()     — parses argc/argv (-c, -R, -H, filenames)
 * apply_cli_options()     — applies parsed options to struct editor
 * init_globals()          — zeroes and initialises g before the first file
 * load_startup_cmds()     — reads EXINIT or ~/.exrc (with permission check)
 * run_startup_cmds()      — executes startup command strings via colon
 * run_editor_session()    — the main file loop: iterates over cmdline files,
 *                              calls init_text_buffer + edit_file for each
 * append_initial_cmd() /
 * run_initial_cmds()      — accumulate and replay -c CMD strings
 */
#include "session.h"

#include "buffer.h"
#include "ex.h"
#include "status.h"
#include "term.h"

void
run_cmds(struct editor *g, char *p)
{
	while (p) {
		char *q = p;
		p = strchr(q, '\n');
		if (p)
			while (*p == '\n')
				*p++ = '\0';
		colon(g, q);
	}
}

void
append_initial_cmd(struct editor *g, const char *cmd)
{
	if (!cmd)
		return;
	g->initial_cmdv = xrealloc(g->initial_cmdv, (size_t)(g->initial_cmdc + 1) *
	                                                sizeof(g->initial_cmdv[0]));
	g->initial_cmdv[g->initial_cmdc++] = xstrdup(cmd);
}

void
run_initial_cmds(struct editor *g)
{
	int i;

	for (i = 0; i < g->initial_cmdc; i++) {
		run_cmds(g, g->initial_cmdv[i]);
		free(g->initial_cmdv[i]);
	}
	free(g->initial_cmdv);
	g->initial_cmdv = NULL;
	g->initial_cmdc = 0;
}

char *
load_startup_cmds(struct editor *g)
{
	const char *exinit = getenv("EXINIT");
	char *cmds = NULL;

	if (exinit)
		return xstrdup(exinit);

	{
		const char *home = getenv("HOME");

		if (home && *home) {
			char exrc_buf[PATH_MAX];
			snprintf(exrc_buf, sizeof(exrc_buf), "%s/.exrc", home);
			char *exrc = exrc_buf;
			struct stat st;

			if (stat(exrc, &st) == 0) {
				if ((st.st_mode & (S_IWGRP | S_IWOTH)) == 0 && st.st_uid == getuid()) {
					cmds = xmalloc_open_read_close(exrc, NULL);
				} else {
					status_line_bold(g, ".exrc: permission denied");
				}
			}
		}
	}

	return cmds;
}

void
run_startup_cmds(struct editor *g)
{
	char *cmds = load_startup_cmds(g);

	if (!cmds)
		return;

	init_text_buffer(g, NULL);
	run_cmds(g, cmds);
	free(cmds);
}

int
parse_cli_options(struct editor *g, int argc, char **argv,
                  struct cli_options *opts)
{
	int c;

	memset(opts, 0, sizeof(*opts));
	optind = 1;
	opterr = 0;
	while ((c = getopt(argc, argv, "c:HhR")) != -1) {
		switch (c) {
		case 'c':
			append_initial_cmd(g, optarg);
			break;
		case 'H':
			opts->help = 1;
			break;
		case 'h':
			opts->usage_only = 1;
			break;
		case 'R':
			opts->readonly = 1;
			break;
		default:
			return -1;
		}
	}

	return optind;
}

int
apply_cli_options(struct editor *g, const struct cli_options *opts)
{
	if (opts->readonly)
		SET_READONLY_MODE(g->readonly_mode);
	if (opts->help)
		show_help();
	if (opts->help || opts->usage_only) {
		show_usage();
		return -1;
	}

	return 0;
}

void
init_globals(struct editor *g)
{
	g->session_epoch = time(NULL);
	g->last_modified_count = -1;
	g->tabstop = 8;
	g->newindent = -1;
	g->line_count_cache_stamp = INT_MIN;
	g->refresh_last_modified_count = INT_MIN;
	g->last_search_pattern = xzalloc(2);
	g->undo_queue_state = UNDO_EMPTY;
}

void
run_file_loop(struct editor *g, char **argv)
{
	optind = 0;
	while (1) {
		edit_file(g, argv[optind]);
		optind++;
		if (optind >= g->cmdline_filecnt)
			break;
	}
}

void
run_editor_session(struct editor *g, char **argv)
{
	run_startup_cmds(g);
	fputs("\033[?1049h", stdout);
	run_file_loop(g, argv);
	fputs("\033[?1049l", stdout);
	term_cursor_shape_restore_startup();
}
