/*
 * term.c - terminal mode and cursor control.
 *
 * rawmode() / cookmode()  — switch stdin between raw (single-keypress) and
 *                           cooked (line-buffered) terminal modes, saving and
 *                           restoring the original termios.
 *
 * query_screen_dimensions() — reads TIOCGWINSZ (with a CPR fallback) and
 *                              updates g->rows / g->columns.
 *
 * Cursor shape (DECSCUSR):
 *   term_cursor_shape_set()           — emit the ANSI sequence for a shape
 *   term_cursor_shape_init_and_set()  — read configured shape and apply it
 *   term_cursor_shape_restore_startup() — restore the shape seen at startup
 *
 * Output helpers (no hooks, write directly to stdout):
 *   place_cursor()            — move the hardware cursor to (row, col)
 *   clear_to_eol()            — emit EL sequence
 *   go_bottom_and_clear_to_eol() — move to last row and clear it
 *   standout_start/end()      — reverse-video on/off
 *
 * No hooks.  Called directly from screen.c, status.c, and ex.c.
 */
#include "term.h"

#define ESC "\033"
#define ESC_BOLD_TEXT ESC "[7m"
#define ESC_NORM_TEXT ESC "[m"
#define ESC_CLEAR2EOL ESC "[K"
#define ESC_SET_CURSOR_POS ESC "[%u;%uH"

static int normal_cursor_shape = CURSOR_STYLE_BLINK_BLOCK;
static int insert_cursor_shape = CURSOR_STYLE_BLINK_PIPE;
static int startup_cursor_shape = -1;
static int startup_cursor_shape_initialized;

static const char *
cursor_shape_seq(int shape)
{
	static const char *const seq[] = {
	    ESC "[0 q",
	    ESC "[1 q",
	    ESC "[2 q",
	    ESC "[3 q",
	    ESC "[4 q",
	    ESC "[5 q",
	    ESC "[6 q",
	};

	if ((unsigned)shape >= ARRAY_SIZE(seq))
		shape = CURSOR_SHAPE_BLOCK;
	return seq[shape];
}

void
term_cursor_shape_set(int shape)
{
	fputs(cursor_shape_seq(shape), stdout);
	fflush(NULL);
}

static int
query_cursor_shape(void)
{
	char buf[128];
	size_t len = 0;
	char *p;
	char *endp;
	long style;

	fputs(ESC "P$q q" ESC "\\", stdout);
	fflush(NULL);

	while (len < sizeof(buf) - 1) {
		struct pollfd pfd;
		unsigned char ch;
		int ready;
		ssize_t rd;

		pfd.fd = STDIN_FILENO;
		pfd.events = POLLIN;
		ready = safe_poll(&pfd, 1, 80);
		if (ready <= 0)
			break;

		rd = safe_read(STDIN_FILENO, &ch, 1);
		if (rd != 1)
			break;

		buf[len++] = (char)ch;
		if (len >= 2 && buf[len - 2] == '\033' && buf[len - 1] == '\\')
			break;
	}
	buf[len] = '\0';

	p = strstr(buf, "$r");
	if (!p)
		return -1;
	p += 2;

	while (*p && !isdigit((unsigned char)*p) && *p != ' ')
		p++;

	if (isdigit((unsigned char)*p)) {
		style = strtol(p, &endp, 10);
		p = endp;
	} else if (*p == ' ') {
		style = 0;
	} else {
		return -1;
	}

	while (*p == ' ')
		p++;
	if (*p != 'q')
		return -1;

	if (style < 0 || style > CURSOR_STYLE_PIPE)
		return -1;

	return (int)style;
}

int
term_cursor_shape_set_configured(int shape)
{
	if (shape < CURSOR_STYLE_DEFAULT || shape > CURSOR_STYLE_PIPE)
		return -1;
	normal_cursor_shape = shape;
	term_cursor_shape_set(normal_cursor_shape);
	return 0;
}

int
term_cursor_shape_get_configured(void)
{
	return normal_cursor_shape;
}

int
term_cursor_shape_set_insert(int shape)
{
	if (shape < CURSOR_STYLE_DEFAULT || shape > CURSOR_STYLE_PIPE)
		return -1;
	insert_cursor_shape = shape;
	return 0;
}

int
term_cursor_shape_get_insert(void)
{
	return insert_cursor_shape;
}

void
term_cursor_shape_update_for_mode(int cmd_mode)
{
	int shape = (cmd_mode == 0) ? normal_cursor_shape : insert_cursor_shape;
	term_cursor_shape_set(shape);
}

void
term_cursor_shape_init_and_set(void)
{
	if (!startup_cursor_shape_initialized) {
		startup_cursor_shape = query_cursor_shape();
		startup_cursor_shape_initialized = 1;
	}
	term_cursor_shape_set(normal_cursor_shape);
}

void
term_cursor_shape_restore_startup(void)
{
	if (startup_cursor_shape >= 0)
		term_cursor_shape_set(startup_cursor_shape);
}

int
query_screen_dimensions(struct editor *g)
{
	int err;

	err = get_terminal_width_height(STDIN_FILENO, &g->columns, &g->rows);
	if (g->rows > VI_MAX_LINE)
		g->rows = VI_MAX_LINE;
	if (g->columns > VI_MAX_LINE)
		g->columns = VI_MAX_LINE;
	return err;
}

int
mysleep(int hund)
{
	struct pollfd pfd[1];

	if (hund != 0)
		fflush(NULL);

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	return safe_poll(pfd, 1, hund * 10) > 0;
}

void
rawmode(struct editor *g)
{
	set_termios_to_raw(STDIN_FILENO, &g->term_orig, TERMIOS_RAW_CRNL);
	fputs(ESC "[?2004h", stdout);
}

void
cookmode(struct editor *g)
{
	fputs(ESC "[?2004l", stdout);
	fflush(NULL);
	tcsetattr_stdin_TCSANOW(&g->term_orig);
}

void
place_cursor(struct editor *g, int row, int col)
{
	char cm1[sizeof(ESC_SET_CURSOR_POS) + sizeof(int) * 3 * 2];

	if (row < 0)
		row = 0;
	if (row >= (int)g->rows)
		row = (int)g->rows - 1;
	if (col < 0)
		col = 0;
	if (col >= (int)g->columns)
		col = (int)g->columns - 1;

	sprintf(cm1, ESC_SET_CURSOR_POS, row + 1, col + 1);
	fputs(cm1, stdout);
}

void
clear_to_eol(void)
{
	fputs(ESC_CLEAR2EOL, stdout);
}

void
go_bottom_and_clear_to_eol(struct editor *g)
{
	place_cursor(g, g->rows - 1, 0);
	clear_to_eol();
}

void
standout_start(void)
{
	fputs(ESC_BOLD_TEXT, stdout);
}

void
standout_end(void)
{
	fputs(ESC_NORM_TEXT, stdout);
}