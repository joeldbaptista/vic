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
	/*
	 * == Execute a newline-separated sequence of ex commands ==
	 *
	 * Splits p on newlines, NUL-terminating each segment in-place, and
	 * calls colon() for each.  Used to replay EXINIT, .exrc, and -c CMD
	 * strings after the file has been loaded.
	 */
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
	/*
	 * == Append one -c CMD string to the deferred command queue ==
	 *
	 * Strings are collected before the first file is loaded, then played
	 * back by run_initial_cmds after the buffer is ready.
	 */
	if (!cmd)
		return;
	g->initial_cmdv = xrealloc(g->initial_cmdv, (size_t)(g->initial_cmdc + 1) *
	                                                sizeof(g->initial_cmdv[0]));
	g->initial_cmdv[g->initial_cmdc++] = xstrdup(cmd);
}

void
run_initial_cmds(struct editor *g)
{
	/*
	 * == Execute all deferred -c CMD strings and free the queue ==
	 *
	 * Called after the first file is loaded.  Runs each stored command
	 * string via run_cmds, then frees the array.
	 */
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
	/*
	 * == Load startup ex commands from EXINIT or ~/.exrc ==
	 *
	 * Priority: EXINIT environment variable first, then ~/.exrc.  The
	 * .exrc file is only read if it is owned by the current user and not
	 * group- or world-writable (security check).  Returns a heap-allocated
	 * NUL-terminated string, or NULL if no startup commands are found.
	 * The caller is responsible for free()ing the result.
	 */
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
	/*
	 * == Load and execute EXINIT / .exrc startup commands ==
	 *
	 * Initialises a scratch buffer (needed for colon()), runs the commands
	 * via run_cmds, then frees the string.  The scratch buffer is discarded;
	 * the real file is loaded later by run_file_loop.
	 */
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
	/*
	 * == Parse command-line flags into opts ==
	 *
	 * Recognised flags:
	 *   -c CMD  — defer CMD for execution after file load
	 *   -R      — open files read-only
	 *   -H      — show full help and exit
	 *   -h      — show usage and exit
	 *
	 * Returns optind (the index of the first non-flag argument), or -1 on
	 * an unrecognised flag.
	 */
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
	/*
	 * == Apply already-parsed CLI options to the editor state ==
	 *
	 * Sets readonly mode and handles -H / -h by printing help/usage and
	 * returning -1 so the caller knows to exit.
	 */
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
	/*
	 * == Zero-initialise g and set safe defaults ==
	 *
	 * Called once before any file is loaded.  Records the session epoch
	 * (used for the yank temp file name), sets the default tabstop, and
	 * allocates the initial last_search_pattern buffer.
	 */
	g->session_epoch = time(NULL);
	g->last_modified_count = -1;
	g->tabstop = 8;
	g->setops |= VI_SYNTAX;
	g->newindent = -1;
	g->line_count_cache_stamp = INT_MIN;
	g->refresh_last_modified_count = INT_MIN;
	g->last_search_pattern = xzalloc(2);
	g->undo_queue_state = UNDO_EMPTY;
}

void
run_file_loop(struct editor *g, char **argv)
{
	/*
	 * == Iterate over each file on the command line, editing it in turn ==
	 *
	 * Calls edit_file() for each argv entry up to g->cmdline_filecnt.
	 * When no files were given, argv[0] is NULL and edit_file opens a
	 * scratch buffer.
	 */
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
	/*
	 * == Top-level editor lifecycle: startup → file loop → teardown ==
	 *
	 * 1. Runs startup commands (EXINIT / .exrc).
	 * 2. Switches to the alternate screen (?1049h).
	 * 3. Runs the file loop.
	 * 4. Restores the primary screen (?1049l) and the original cursor shape.
	 */
	run_startup_cmds(g);
	fputs("\033[?1049h", stdout);
	run_file_loop(g, argv);
	fputs("\033[?1049l", stdout);
	term_cursor_shape_restore_startup();
}
