/*
 * tiny vi.c: A small 'vi' clone
 * Copyright (C) 2000, 2001 Sterling Huxley <sterling@europa.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "vi.h"
#include <locale.h>
#include <regex.h>
#include <wchar.h>
#include <wctype.h>

static volatile sig_atomic_t winch_pending;

extern int wcwidth(wchar_t);

#define TERM_CELL_BYTES 32

enum {
	CELL_WIDE_HEAD = 1 << 0,
	CELL_WIDE_TAIL = 1 << 1,
};

struct term_cell {
	uint8_t len;   // bytes used in bytes[]
	uint8_t flags; // CELL_* flags
	char bytes[TERM_CELL_BYTES];
};

// Treat bytes >= ' ' as printable for display purposes.
// Note: 0x9b (CSI) is a valid UTF-8 continuation byte and must not be
// treated as a special Meta-ESC when rendering file contents.
#define Isprint(c) ((unsigned char)(c) >= ' ' && (c) != 0x7f)
#define isbackspace(c) ((c) == term_orig.c_cc[VERASE] || (c) == 8 || (c) == 127)

enum {
	MAX_TABSTOP = 32,
	MAX_INPUT_LEN = 128,
	MAX_SCR_COLS = CONFIG_FEATURE_VI_MAX_LEN,
	MAX_SCR_ROWS = CONFIG_FEATURE_VI_MAX_LEN,
};

// http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
#define ESC "\033"
#define ESC_BOLD_TEXT ESC "[7m"
#define ESC_NORM_TEXT ESC "[m"
#define ESC_BELL "\007"
#define ESC_CLEAR2EOL ESC "[K"
#define ESC_CLEAR2EOS ESC "[J"
#define ESC_SET_CURSOR_POS ESC "[%u;%uH"
#define ESC_SET_CURSOR_TOPLEFT ESC "[H"

// xterm-style cursor shape (DECSCUSR): ESC [ Ps SP q
// Ps=2 steady block, Ps=6 steady bar (pipe)
#define ESC_CURSOR_BLOCK ESC "[2 q"
#define ESC_CURSOR_BAR ESC "[6 q"

// cmds modifying text[]
static const char modifying_cmds[] ALIGN1 = "aAcCdDiIJoOpPrRs"
                                            "xX<>~";

enum {
	YANKONLY = FALSE,
	YANKDEL = TRUE,
	FORWARD = 1, // code depends on "1"  for array index
	BACK = -1,   // code depends on "-1" for array index
	LIMITED = 0, // char_search() only current line
	FULL = 1,    // char_search() to the end/beginning of entire text
	PARTIAL = 0, // buffer contains partial line
	WHOLE = 1,   // buffer contains whole lines
	MULTI = 2,   // buffer may include newlines

	S_BEFORE_WS = 1, // used in skip_thing() for moving "dot"
	S_TO_WS = 2,     // used in skip_thing() for moving "dot"
	S_OVER_WS = 3,   // used in skip_thing() for moving "dot"
	S_END_PUNCT = 4, // used in skip_thing() for moving "dot"
	S_END_ALNUM = 5, // used in skip_thing() for moving "dot"

	C_END = -1, // cursor is at end of line due to '$' command
};

// vi.c expects chars to be unsigned.
// busybox build system provides that, but it's better
// to audit and fix the source

struct globals {
	// many references - keep near the top of globals
	char *text, *end; // pointers to the user data in memory
	char *dot;        // where all the action takes place
	int text_size;    // size of the allocated buffer

	// the rest
	smalluint vi_setops; // set by setops()
#define VI_AUTOINDENT (1 << 0)
#define VI_EXPANDTAB (1 << 1)
#define VI_ERR_METHOD (1 << 2)
#define VI_IGNORECASE (1 << 3)
#define VI_SHOWMATCH (1 << 4)
#define VI_TABSTOP (1 << 5)
#define VI_NUMBER (1 << 6)
#define VI_RELNUMBER (1 << 7)
#define VI_ERE (1 << 8)
#define autoindent (vi_setops & VI_AUTOINDENT)
#define expandtab (vi_setops & VI_EXPANDTAB)
#define err_method \
	(vi_setops & VI_ERR_METHOD) // indicate error with beep or flash
#define ignorecase (vi_setops & VI_IGNORECASE)
#define showmatch (vi_setops & VI_SHOWMATCH)
#define number (vi_setops & VI_NUMBER)
#define relativenumber (vi_setops & VI_RELNUMBER)
#define ere (vi_setops & VI_ERE)
// order of constants and strings must match
#define OPTS_STR           \
	"ai\0"             \
	"autoindent\0"     \
	"et\0"             \
	"expandtab\0"      \
	"fl\0"             \
	"flash\0"          \
	"ic\0"             \
	"ignorecase\0"     \
	"sm\0"             \
	"showmatch\0"      \
	"ts\0"             \
	"tabstop\0"        \
	"nu\0"             \
	"number\0"         \
	"rnu\0"            \
	"relativenumber\0" \
	"ere\0"            \
	"extendedre\0"

	smallint readonly_mode;
#define SET_READONLY_FILE(flags) ((flags) |= 0x01)
#define SET_READONLY_MODE(flags) ((flags) |= 0x02)
#define UNSET_READONLY_FILE(flags) ((flags) &= 0xfe)

	smallint editing;           // >0 while we are editing a file
	                            // [code audit says "can be 0, 1 or 2 only"]
	smallint cmd_mode;          // 0=command  1=insert 2=replace
	smallint last_cursor_shape; // -1 unknown, 0 block, 1 bar
	int modified_count;         // buffer contents changed if !0
	int last_modified_count;    // = -1;
	int cmdline_filecnt;        // how many file names on cmd line
	int cmdcnt;                 // repetition count
	char *rstart;               // start of text in Replace mode
	int rows, columns;          // the terminal screen is this size
	int get_rowcol_error;
	int crow, ccol;             // cursor is on Crow x Ccol
	int offset;                 // chars scrolled off the screen to the left
	int have_status_msg;        // is default edit status needed?
	                            // [don't make smallint!]
	uint32_t last_status_cksum; // hash of current status line
	char *current_filename;
	char *alt_filename;
	char *screenbegin;         // index into text[], of top line on the screen
	struct term_cell *screen;  // virtual screen buffer: rows*columns cells
	struct term_cell *linebuf; // per-line formatted buffer: columns cells
	int screensize;            // number of cells in screen[]
	int tabstop;
	int last_search_char;     // last char searched for (int because of Unicode)
	smallint last_search_cmd; // command used to invoke last char search
	char undo_queue_state;    // One of UNDO_INS, UNDO_DEL, UNDO_EMPTY

	smallint adding2q;          // are we currently adding user input to q
	int lmc_len;                // length of last_modifying_cmd
	char *ioq, *ioq_start;      // pointer to string for get_one_char to "read"
	int dotcnt;                 // number of times to repeat '.' command
	char *last_search_pattern;  // last pattern from a '/' or '?' search
	int char_insert__indentcol; // column of recent autoindent or 0
	int newindent;              // autoindent value for 'O'/'cc' commands
	                            // or -1 to use indent from previous line
	smallint cmd_error;

	// former statics
	char *edit_file__cur_line;
	int refresh__old_offset;
	int format_edit_status__tot;

	// a few references only
	smalluint YDreg; //,Ureg;// default delete register and orig line for "U"
#define Ureg 27
	char *reg[28];            // named register a-z, "D", and "U" 0-25,26,27
	char regtype[28];         // buffer type: WHOLE, MULTI or PARTIAL
	char *mark[28];           // user marks points somewhere in text[]-  a-z and previous
	                          // context ''
	sigjmp_buf restart;       // int_handler() jumps to location remembered here
	struct termios term_orig; // remember what the cooked mode was
	int cindex;               // saved character index for up/down motion
	smallint keep_index;      // retain saved character index
	llist_t *initial_cmds;
	char readbuffer[KEYCODE_BUFFER_SIZE];
#define STATUS_BUFFER_LEN 200
	char status_buffer[STATUS_BUFFER_LEN];   // messages to the user
	char last_modifying_cmd[MAX_INPUT_LEN];  // last modifying cmd for "."
	char get_input_line__buf[MAX_INPUT_LEN]; // former static
	char scr_out_buf[MAX_SCR_COLS + MAX_TABSTOP * 2];

// undo_push() operations
#define UNDO_INS 0
#define UNDO_DEL 1
#define UNDO_INS_CHAIN 2
#define UNDO_DEL_CHAIN 3
#define UNDO_INS_QUEUED 4
#define UNDO_DEL_QUEUED 5

// Pass-through flags for functions that can be undone
#define NO_UNDO 0
#define ALLOW_UNDO 1
#define ALLOW_UNDO_CHAIN 2
#define ALLOW_UNDO_QUEUED 3
	struct undo_object {
		struct undo_object *prev; // Linking back avoids list traversal (LIFO)
		int start;                // Offset where the data should be restored/deleted
		int length;               // total data size
		uint8_t u_type;           // 0=deleted, 1=inserted, 2=swapped
		char undo_text[1];        // text that was deleted (if deletion)
	} *undo_stack_tail;
#define UNDO_USE_SPOS 32
#define UNDO_EMPTY 64
	char *undo_queue_spos; // Start position of queued operation
	int undo_q;
	char undo_queue[CONFIG_FEATURE_VI_UNDO_QUEUE_MAX];
};
#define G (*ptr_to_globals)
#define text (G.text)
#define text_size (G.text_size)
#define end (G.end)
#define dot (G.dot)
#define reg (G.reg)

#define vi_setops (G.vi_setops)
#define editing (G.editing)
#define cmd_mode (G.cmd_mode)
#define last_cursor_shape (G.last_cursor_shape)
#define modified_count (G.modified_count)
#define last_modified_count (G.last_modified_count)
#define cmdline_filecnt (G.cmdline_filecnt)
#define cmdcnt (G.cmdcnt)
#define rstart (G.rstart)
#define rows (G.rows)
#define columns (G.columns)
#define crow (G.crow)
#define ccol (G.ccol)
#define offset (G.offset)
#define status_buffer (G.status_buffer)
#define have_status_msg (G.have_status_msg)
#define last_status_cksum (G.last_status_cksum)
#define current_filename (G.current_filename)
#define alt_filename (G.alt_filename)
#define screen (G.screen)
#define screensize (G.screensize)
#define linebuf (G.linebuf)
#define screenbegin (G.screenbegin)
#define tabstop (G.tabstop)
#define last_search_char (G.last_search_char)
#define last_search_cmd (G.last_search_cmd)
#define readonly_mode (G.readonly_mode)
#define adding2q (G.adding2q)
#define lmc_len (G.lmc_len)
#define ioq (G.ioq)
#define ioq_start (G.ioq_start)
#define dotcnt (G.dotcnt)
#define last_search_pattern (G.last_search_pattern)
#define char_insert__indentcol (G.char_insert__indentcol)
#define newindent (G.newindent)
#define cmd_error (G.cmd_error)
#define edit_file__cur_line (G.edit_file__cur_line)
#define refresh__old_offset (G.refresh__old_offset)
#define format_edit_status__tot (G.format_edit_status__tot)
#define YDreg (G.YDreg)
#define regtype (G.regtype)
#define mark (G.mark)
#define restart (G.restart)
#define term_orig (G.term_orig)
#define cindex (G.cindex)
#define keep_index (G.keep_index)
#define initial_cmds (G.initial_cmds)
#define readbuffer (G.readbuffer)
#define scr_out_buf (G.scr_out_buf)
#define last_modifying_cmd (G.last_modifying_cmd)
#define get_input_line__buf (G.get_input_line__buf)
#define undo_stack_tail (G.undo_stack_tail)
#define undo_queue_state (G.undo_queue_state)
#define undo_q (G.undo_q)
#define undo_queue (G.undo_queue)
#define undo_queue_spos (G.undo_queue_spos)

#define INIT_G()                                                        \
	do {                                                            \
		SET_PTR_TO_GLOBALS(xzalloc(sizeof(G)));                 \
		last_modified_count--;                                  \
		/* "" but has space for 2 chars: */                     \
		IF_FEATURE_VI_SEARCH(last_search_pattern = xzalloc(2);) \
		tabstop = 8;                                            \
		IF_FEATURE_VI_SETOPTS(newindent--;)                     \
	} while (0)

static void
cell_set_ascii(struct term_cell *c, unsigned char ch)
{
	c->len = 1;
	c->flags = 0;
	c->bytes[0] = (char)ch;
}

static void
cell_set_bytes(struct term_cell *c, const char *s, size_t n,
               uint8_t flags)
{
	if (n > TERM_CELL_BYTES)
		n = TERM_CELL_BYTES;
	memcpy(c->bytes, s, n);
	c->len = (uint8_t)n;
	c->flags = flags;
}

static void
cell_mark_wide_tail(struct term_cell *c)
{
	c->len = 0;
	c->flags = CELL_WIDE_TAIL;
}

static void
cell_append_bytes(struct term_cell *c, const char *s, size_t n)
{
	if (c->len >= TERM_CELL_BYTES)
		return;
	if (n > (size_t)(TERM_CELL_BYTES - c->len))
		n = (size_t)(TERM_CELL_BYTES - c->len);
	memcpy(c->bytes + c->len, s, n);
	c->len += (uint8_t)n;
}

static int
cell_equal(const struct term_cell *a, const struct term_cell *b)
{
	if (a->len != b->len)
		return 0;
	if (a->flags != b->flags)
		return 0;
	if (a->len == 0)
		return 1;
	return memcmp(a->bytes, b->bytes, a->len) == 0;
}

static size_t
mb_decode_1(wchar_t *out_wc, const char *s, size_t n,
            mbstate_t *st)
{
	size_t r = mbrtowc(out_wc, s, n, st);
	if (r == (size_t)-2) {
		// Incomplete multibyte sequence: treat first byte as raw.
		memset(st, 0, sizeof(*st));
		*out_wc = (wchar_t)(unsigned char)*s;
		return 1;
	}
	if (r == (size_t)-1) {
		// Invalid sequence: consume one byte.
		memset(st, 0, sizeof(*st));
		*out_wc = 0xfffd;
		return 1;
	}
	if (r == 0) {
		// NUL byte
		*out_wc = L'\0';
		return 1;
	}
	return r;
}

static size_t
utf8_decode_at(char *p, wchar_t *out_wc)
{
	mbstate_t st;
	memset(&st, 0, sizeof(st));
	return mb_decode_1(out_wc, p, (size_t)(end - p), &st);
}

static wchar_t
wc_at(char *p)
{
	wchar_t wc = 0;
	if (!p || p < text || p > end - 1)
		return 0;
	utf8_decode_at(p, &wc);
	return wc;
}

static int
is_word_wc(wchar_t wc)
{
	return iswalnum(wc) || wc == L'_';
}

static int
is_space_wc(wchar_t wc)
{
	return iswspace(wc);
}

static int
is_punct_wc(wchar_t wc)
{
	return iswpunct(wc);
}

static char *utf8_next_cp_anyline(char *p);
static char *utf8_prev_cp_anyline(char *p);

static char *
step_cp(char *p, int dir)
{
	return (dir >= 0) ? utf8_next_cp_anyline(p) : utf8_prev_cp_anyline(p);
}

static int
wc_display_width(wchar_t wc)
{
	int w = wcwidth(wc);
	if (w < 0)
		w = 1;
	return w;
}

// --- UTF-8 helpers (minimal: codepoint-safe navigation/deletion) ---
// The editor stores UTF-8 as raw bytes in text[]. These helpers avoid
// landing/deleting in the middle of a multibyte sequence.

static int
is_utf8_cont(unsigned char b)
{
	return (b & 0xC0) == 0x80;
}

static int
utf8_seq_len(unsigned char lead)
{
	// Minimal validation; treat invalid lead bytes as single-byte.
	if (lead < 0x80)
		return 1;
	if ((lead & 0xE0) == 0xC0)
		return 2;
	if ((lead & 0xF0) == 0xE0)
		return 3;
	if ((lead & 0xF8) == 0xF0)
		return 4;
	return 1;
}

// Return pointer to start of previous UTF-8 codepoint in the same line.
// Clamped to [line_start, p].
static char *
utf8_prev_cp(char *line_start, char *p)
{
	char *q = p;

	if (q <= line_start)
		return p;

	q--; // examine byte before p
	if (*q == '\n')
		return p;

	while (q > line_start && is_utf8_cont((unsigned char)*q))
		q--;

	if (*q == '\n')
		return p;

	return q;
}

// Return pointer to start of next UTF-8 codepoint (does not cross newline).
static char *
utf8_next_cp(char *p)
{
	unsigned char lead;
	char *q = p;

	if (*q == '\n')
		return p;
	if (q >= end - 1)
		return p;

	lead = (unsigned char)*q;
	q += utf8_seq_len(lead);

	if (q > end - 1)
		q = end - 1;
	if (*q == '\n')
		return p;

	return q;
}

// Return pointer to last byte of UTF-8 codepoint starting at p.
// Does not cross a newline and clamps to end-1.
static char *
utf8_cp_end_byte(char *p)
{
	unsigned char lead;
	int len;
	char *q;
	char *nl;

	if (!p || p < text || p >= end)
		return p;
	if (*p == '\n')
		return p;
	lead = (unsigned char)*p;
	len = utf8_seq_len(lead);
	if (len < 1)
		len = 1;
	q = p + len - 1;
	if (q >= end)
		q = end - 1;
	// Don't include newline if we hit it inside the byte span.
	nl = memchr(p, '\n', (size_t)(q - p + 1));
	if (nl) {
		if (nl == p)
			return p;
		return nl - 1;
	}
	return q;
}

static void show_status_line(void); // put a message on the bottom line

static void
show_help(void)
{
	puts("These features are available:"
	     "\n\tPattern searches with / and ?"
	     "\n\tLast command repeat with ."
	     "\n\tLine marking with 'x"
	     "\n\tNamed buffers with \"x"
	     // not implemented: "\n\tReadonly if vi is called as \"view\""
	     // redundant: usage text says this too: "\n\tReadonly with -R command
	     // line arg"
	     "\n\tSome colon mode commands with :"
	     "\n\tSettable options with \":set\""
	     "\n\tSignal catching- ^C"
	     "\n\tJob suspend and resume with ^Z"
	     "\n\tAdapt to window re-sizes");
}

static void
write1(const char *out)
{
	fputs_stdout(out);
}

static int
query_screen_dimensions(void)
{
	int old_rows = rows;
	int old_cols = columns;
	int err = get_terminal_width_height(STDIN_FILENO, &columns, &rows);
	if (err) {
		rows = old_rows;
		columns = old_cols;
		return err;
	}
	if (rows > MAX_SCR_ROWS)
		rows = MAX_SCR_ROWS;
	if (columns > MAX_SCR_COLS)
		columns = MAX_SCR_COLS;
	// Avoid zero/too-small sizes (can happen transiently during resize).
	if (rows < 2)
		rows = 2;
	if (columns < 2)
		columns = 2;
	return err;
}

// sleep for 'h' 1/100 seconds, return 1/0 if stdin is (ready for read)/(not
// ready)
static int
mysleep(int hund)
{
	struct pollfd pfd[1];

	if (hund != 0)
		fflush_all();

	pfd[0].fd = STDIN_FILENO;
	pfd[0].events = POLLIN;
	return safe_poll(pfd, 1, hund * 10) > 0;
}

//----- Set terminal attributes --------------------------------
static void
rawmode(void)
{
	// no TERMIOS_CLEAR_ISIG: leave ISIG on - allow signals
	set_termios_to_raw(STDIN_FILENO, &term_orig, TERMIOS_RAW_CRNL);
}

static void
update_cursor_shape(void)
{
	int desired = (cmd_mode == 0) ? 0 : 1;

	if (desired == last_cursor_shape)
		return;
	last_cursor_shape = desired;

	write1((desired == 0) ? ESC_CURSOR_BLOCK : ESC_CURSOR_BAR);
	fflush_all();
}

static void
cookmode(void)
{
	// Reset cursor to a sane default on exit.
	write1(ESC_CURSOR_BLOCK);
	fflush_all();
	tcsetattr_stdin_TCSANOW(&term_orig);
}

//----- Terminal Drawing ---------------------------------------
// The terminal is made up of 'rows' line of 'columns' columns.
// classically this would be 24 x 80.
//  screen coordinates
//  0,0     ...     0,79
//  1,0     ...     1,79
//  .       ...     .
//  .       ...     .
//  22,0    ...     22,79
//  23,0    ...     23,79   <- status line

//----- Move the cursor to row x col (count from 0, not 1) -------
static void
place_cursor(int row, int col)
{
	char cm1[sizeof(ESC_SET_CURSOR_POS) + sizeof(int) * 3 * 2];

	if (row < 0)
		row = 0;
	if (row >= rows)
		row = rows - 1;
	if (col < 0)
		col = 0;
	if (col >= columns)
		col = columns - 1;

	sprintf(cm1, ESC_SET_CURSOR_POS, row + 1, col + 1);
	write1(cm1);
}

//----- Erase from cursor to end of line -----------------------
static void
clear_to_eol(void)
{
	write1(ESC_CLEAR2EOL);
}

static void
go_bottom_and_clear_to_eol(void)
{
	place_cursor(rows - 1, 0);
	clear_to_eol();
}

//----- Start standout mode ------------------------------------
static void
standout_start(void)
{
	write1(ESC_BOLD_TEXT);
}

//----- End standout mode --------------------------------------
static void
standout_end(void)
{
	write1(ESC_NORM_TEXT);
}

//----- Text Movement Routines ---------------------------------
static void *
memrchr_portable(const void *s, int c, size_t n)
{
	const unsigned char *p = (const unsigned char *)s + n;
	unsigned char uc = (unsigned char)c;
	while (n--) {
		if (*--p == uc)
			return (void *)p;
	}
	return NULL;
}

static char *
strchrnul_portable(const char *s, int c)
{
	while (*s && *s != (char)c)
		s++;
	return (char *)s;
}

static char *
begin_line(char *p) // return pointer to first char cur line
{
	if (p > text) {
		p = memrchr_portable(text, '\n', (size_t)(p - text));
		if (!p)
			return text;
		return p + 1;
	}
	return p;
}

static char *
utf8_next_cp_anyline(char *p)
{
	char *n;
	if (p >= end - 1)
		return p;
	if (*p == '\n')
		return p + 1;
	n = utf8_next_cp(p);
	/* utf8_next_cp() may refuse to step onto '\n' and return p.
	 * For anyline motion we must still make progress.
	 */
	if (n == p && p < end - 1)
		return p + 1;
	return n;
}

static char *
utf8_prev_cp_anyline(char *p)
{
	char *n;
	if (p <= text)
		return p;
	if (p[-1] == '\n')
		return p - 1;
	n = utf8_prev_cp(begin_line(p), p);
	/* Safety: if we failed to move, back up by one byte. */
	if (n == p && p > text)
		return p - 1;
	return n;
}

static char *
end_line(char *p) // return pointer to NL of cur line
{
	if (p < end - 1) {
		p = memchr(p, '\n', end - p - 1);
		if (!p)
			return end - 1;
	}
	return p;
}

static char *
dollar_line(char *p) // return pointer to just before NL line
{
	p = end_line(p);
	// Try to stay off of the Newline
	if (*p == '\n' && (p - begin_line(p)) > 0)
		p--;
	return p;
}

static char *
prev_line(char *p) // return pointer first char prev line
{
	p = begin_line(p); // goto beginning of cur line
	if (p > text && p[-1] == '\n')
		p--;       // step to prev line
	p = begin_line(p); // goto beginning of prev line
	return p;
}

static char *
next_line(char *p) // return pointer first char next line
{
	p = end_line(p);
	if (p < end - 1 && *p == '\n')
		p++; // step to next line
	return p;
}

//----- Text Information Routines ------------------------------
static char *
end_screen(void)
{
	char *q;
	int cnt;

	// find new bottom line
	q = screenbegin;
	for (cnt = 0; cnt < rows - 2; cnt++)
		q = next_line(q);
	q = end_line(q);
	return q;
}

// count line from start to stop
static int
count_lines(char *start, char *stop)
{
	char *q;
	int cnt;

	if (stop < start) { // start and stop are backwards- reverse them
		q = start;
		start = stop;
		stop = q;
	}
	cnt = 0;
	stop = end_line(stop);
	while (start <= stop && start <= end - 1) {
		start = end_line(start);
		if (*start == '\n')
			cnt++;
		start++;
	}
	return cnt;
}

static char *
find_line(int li) // find beginning of line #li
{
	char *q;

	for (q = text; li > 1; li--) {
		q = next_line(q);
	}
	return q;
}

static int
next_tabstop(int col)
{
	return col + ((tabstop - 1) - (col % tabstop));
}

static int
prev_tabstop(int col)
{
	{
		int rem = (col % tabstop);
		return col - (rem ? rem : tabstop);
	}
}

static int
get_column(char *p)
{
	const char *r;
	int co = 0;
	mbstate_t st;

	memset(&st, 0, sizeof(st));
	for (r = begin_line(p); r < p && r < end;) {
		unsigned char b = (unsigned char)*r;
		wchar_t wc;
		size_t n;

		if (b == '\n')
			break;
		if (b == '\t') {
			co = next_tabstop(co);
			r++;
			memset(&st, 0, sizeof(st));
			continue;
		}
		if (b < ' ' || b == 0x7f) {
			// display as ^X
			co += 2;
			r++;
			memset(&st, 0, sizeof(st));
			continue;
		}

		n = mb_decode_1(&wc, r, (size_t)(end - r), &st);
		if (wc != L'\0') {
			int w = wc_display_width(wc);
			if (w > 0)
				co += w;
		}
		r += n;
	}
	return co;
}

//----- Erase the Screen[] memory ------------------------------
static void
screen_erase(void)
{
	int i;
	for (i = 0; i < screensize; i++)
		cell_set_ascii(&screen[i], ' ');
}

static void
new_screen(int ro, int co)
{
	struct term_cell *s;
	int i;

	free(screen);
	free(linebuf);

	screensize = ro * co;
	s = screen = xmalloc((size_t)screensize * sizeof(*screen));
	linebuf = xmalloc((size_t)co * sizeof(*linebuf));

	// initialize the new screen. assume this will be a empty file.
	screen_erase();
	for (i = 0; i < co; i++)
		cell_set_ascii(&linebuf[i], ' ');

	// non-existent text[] lines start with a tilde (~).
	ro -= 2;
	while (--ro >= 0) {
		s += co;
		cell_set_ascii(s, '~');
	}
}

static int
digits10(unsigned n)
{
	int d = 1;
	while (n >= 10) {
		n /= 10;
		d++;
	}
	return d;
}

static int
line_number_gutter_width_for_total(unsigned tot_lines)
{
	if (!number && !relativenumber)
		return 0;
	if (columns < 2)
		return 0;
	int digits = digits10(tot_lines ? tot_lines : 1);
	int gutter = digits + 1; // digits + trailing space
	if (gutter >= columns)
		return 0;
	return gutter;
}

//----- Synchronize the cursor to Dot --------------------------
static void
sync_cursor(char *d, int gutter, int *row, int *col)
{
	char *beg_cur; // begin and end of "d" line
	char *tp;
	int cnt, ro, co;
	int text_cols = columns - gutter;

	beg_cur = begin_line(d); // first char of cur line

	if (beg_cur < screenbegin) {
		// "d" is before top line on screen
		// how many lines do we have to move
		cnt = count_lines(beg_cur, screenbegin);
	sc1:
		screenbegin = beg_cur;
		if (cnt > (rows - 1) / 2) {
			// we moved too many lines. put "dot" in middle of screen
			for (cnt = 0; cnt < (rows - 1) / 2; cnt++) {
				screenbegin = prev_line(screenbegin);
			}
		}
	} else {
		char *end_scr;          // begin and end of screen
		end_scr = end_screen(); // last char of screen
		if (beg_cur > end_scr) {
			// "d" is after bottom line on screen
			// how many lines do we have to move
			cnt = count_lines(end_scr, beg_cur);
			if (cnt > (rows - 1) / 2)
				goto sc1; // too many lines
			for (ro = 0; ro < cnt - 1; ro++) {
				// move screen begin the same amount
				screenbegin = next_line(screenbegin);
				// now, move the end of screen
				end_scr = next_line(end_scr);
				end_scr = end_line(end_scr);
			}
		}
	}
	// "d" is on screen- find out which row
	tp = screenbegin;
	for (ro = 0; ro < rows - 1; ro++) { // drive "ro" to correct row
		if (tp == beg_cur)
			break;
		tp = next_line(tp);
	}

	// find out what col "d" is on
	co = get_column(d);

	// "co" is the column where "dot" is.
	// The screen has "columns" columns.
	// The currently displayed columns are  0+offset -- columns+ofset
	// |-------------------------------------------------------------|
	//               ^ ^                                ^
	//        offset | |------- columns ----------------|
	//
	// If "co" is already in this range then we do not have to adjust offset
	//      but, we do have to subtract the "offset" bias from "co".
	// If "co" is outside this range then we have to change "offset".
	// If the first char of a line is a tab the cursor will try to stay
	//  in column 7, but we have to set offset to 0.

	if (co < 0 + offset) {
		offset = co;
	}
	if (co >= text_cols + offset) {
		offset = co - text_cols + 1;
	}
	// if the first char of the line is a tab, and "dot" is sitting on it
	// force offset to 0 (keeps behavior compatible with old renderer).
	if (d == beg_cur && *d == '\t')
		offset = 0;
	co -= offset;

	*row = ro;
	*col = co + gutter;
}

//----- Format a text[] line into terminal cells ---------------------
static struct term_cell *
format_line(char *src, int lineno, int cur_lineno,
            int gutter)
{
	int i;
	int text_cols = columns - gutter;
	struct term_cell *buf = linebuf;
	struct term_cell *dest;
	int co;
	int viscol;
	int last_base = -1;
	mbstate_t st;

	// clear entire line
	for (i = 0; i < columns; i++)
		cell_set_ascii(&buf[i], ' ');

	// gutter
	if (gutter) {
		if (src < end) {
			int digits = gutter - 1;
			int n = lineno;
			if (relativenumber && lineno != cur_lineno) {
				n = lineno - cur_lineno;
				if (n < 0)
					n = -n;
			}
			if (digits > 0) {
				char numbuf[16];
				int len;
				snprintf(numbuf, sizeof(numbuf), "%d", n);
				len = (int)strlen(numbuf);
				if (len >= digits) {
					for (i = 0; i < digits; i++)
						cell_set_ascii(&buf[i], (unsigned char)numbuf[len - digits + i]);
				} else {
					for (i = 0; i < digits - len; i++)
						cell_set_ascii(&buf[i], ' ');
					for (; i < digits; i++)
						cell_set_ascii(&buf[i], (unsigned char)numbuf[i - (digits - len)]);
				}
				cell_set_ascii(&buf[digits], ' ');
			}
		}
	}

	if (text_cols <= 0)
		return buf;
	dest = buf + gutter;

	// non-existent text[] lines start with a tilde (~)
	if (src >= end) {
		cell_set_ascii(&dest[0], '~');
		return buf;
	}

	// Skip 'offset' visual columns without slicing UTF-8 sequences.
	viscol = 0;
	memset(&st, 0, sizeof(st));
	while (src < end && *src != '\n' && viscol < offset) {
		unsigned char b = (unsigned char)*src;
		int w;
		size_t n;
		wchar_t wc;

		if (b == '\t') {
			w = next_tabstop(viscol) - viscol;
			n = 1;
			memset(&st, 0, sizeof(st));
		} else if (b < ' ' || b == 0x7f) {
			w = 2;
			n = 1;
			memset(&st, 0, sizeof(st));
		} else {
			n = mb_decode_1(&wc, src, (size_t)(end - src), &st);
			w = wc_display_width(wc);
		}
		if (w > 0)
			viscol += w;
		src += n;
	}

	// Now render visible portion into dest[0..text_cols)
	co = 0;
	memset(&st, 0, sizeof(st));
	while (src < end && *src != '\n' && co < text_cols) {
		unsigned char b = (unsigned char)*src;
		int w;
		size_t n;
		wchar_t wc;

		if (b == '\t') {
			int to = next_tabstop(viscol) - viscol;
			if (to < 1)
				to = 1;
			while (to-- > 0 && co < text_cols) {
				cell_set_ascii(&dest[co], ' ');
				last_base = co;
				co++;
				viscol++;
			}
			src++;
			memset(&st, 0, sizeof(st));
			continue;
		}
		if (b < ' ' || b == 0x7f) {
			// display as ^X (2 columns)
			if (co < text_cols)
				cell_set_ascii(&dest[co++], '^');
			if (co < text_cols) {
				unsigned char shown = (b == 0x7f) ? '?' : (unsigned char)(b + '@');
				cell_set_ascii(&dest[co++], shown);
			}
			src++;
			viscol += 2;
			memset(&st, 0, sizeof(st));
			continue;
		}

		n = mb_decode_1(&wc, src, (size_t)(end - src), &st);
		w = wc_display_width(wc);
		if (w == 0) {
			// Combining mark: append to previous base cell when possible.
			if (last_base >= 0)
				cell_append_bytes(&dest[last_base], src, n);
			src += n;
			continue;
		}
		if (w <= 0)
			w = 1;
		if (w == 1) {
			cell_set_bytes(&dest[co], src, n, 0);
			last_base = co;
			co++;
			viscol++;
			src += n;
			continue;
		}
		if (w == 2) {
			if (co + 1 >= text_cols)
				break;
			cell_set_bytes(&dest[co], src, n, CELL_WIDE_HEAD);
			cell_mark_wide_tail(&dest[co + 1]);
			last_base = co;
			co += 2;
			viscol += 2;
			src += n;
			continue;
		}
		// Rare: treat >2 width as 1.
		cell_set_bytes(&dest[co], src, n, 0);
		last_base = co;
		co++;
		viscol++;
		src += n;
	}

	return buf;
}

//----- Refresh the changed screen lines -----------------------
// Copy the source line from text[] into the buffer and note
// if the current screenline is different from the new buffer.
// If they differ then that line needs redrawing on the terminal.
//
static void
refresh(int full_screen)
{
#define old_offset refresh__old_offset

	int li, changed;
	char *tp; // pointer into text[]
	unsigned tot_lines = 0;
	int gutter = 0;

	if (ENABLE_FEATURE_VI_WIN_RESIZE IF_FEATURE_VI_ASK_TERMINAL(
	        &&!G.get_rowcol_error)) {
		unsigned c = columns, r = rows;
		query_screen_dimensions();
		if (c != (unsigned)columns || r != (unsigned)rows) {
			// query_screen_dimensions() updated rows/columns; keep buffers in sync.
			new_screen(rows, columns);
			full_screen = TRUE;
		}
	}
	if (number || relativenumber) {
		tot_lines = (unsigned)count_lines(text, end - 1);
		if (tot_lines == 0)
			tot_lines = 1;
		format_edit_status__tot = (int)tot_lines;
		gutter = line_number_gutter_width_for_total(tot_lines);
	}
	sync_cursor(dot, gutter, &crow, &ccol); // where cursor will be (on "dot")
	tp = screenbegin;                       // index into text[] of top line

	int cur_lineno = 0;
	int lineno = 0;
	if (gutter) {
		cur_lineno = count_lines(text, dot);
		lineno = count_lines(text, screenbegin);
	}

	// compare text[] to screen[] and mark screen[] lines that need updating
	for (li = 0; li < rows - 1; li++) {
		int cs, ce; // column start & end
		struct term_cell *out_buf;
		struct term_cell *sp; // pointer into screen[]
		int line_exists = (tp < end);
		// format current text line
		out_buf = format_line(tp, lineno, cur_lineno, gutter);

		// skip to the end of the current text[] line
		if (tp < end) {
			char *t = memchr(tp, '\n', end - tp);
			if (!t)
				t = end - 1;
			tp = t + 1;
		}
		if (line_exists)
			lineno++;

		// see if there are any changes between virtual screen and out_buf
		changed = FALSE; // assume no change
		cs = 0;
		ce = columns - 1;
		sp = &screen[li * columns]; // start of screen line
		if (full_screen) {
			// force re-draw of every single column from 0 - columns-1
			goto re0;
		}
		// compare newly formatted buffer with virtual screen
		// look forward for first difference between buf and screen
		for (; cs <= ce; cs++) {
			if (!cell_equal(&out_buf[cs], &sp[cs])) {
				changed = TRUE; // mark for redraw
				break;
			}
		}

		// look backward for last difference between out_buf and screen
		for (; ce >= cs; ce--) {
			if (!cell_equal(&out_buf[ce], &sp[ce])) {
				changed = TRUE; // mark for redraw
				break;
			}
		}
		// now, cs is index of first diff, and ce is index of last diff

		// if horz offset has changed, force a redraw
		if (offset != old_offset) {
		re0:
			changed = TRUE;
		}

		// make a sanity check of columns indexes
		if (cs < 0)
			cs = 0;
		if (ce > columns - 1)
			ce = columns - 1;
		if (cs > ce) {
			cs = 0;
			ce = columns - 1;
		}
		// Don't start painting in the middle of a wide character.
		if (cs > 0 && ((out_buf[cs].flags | sp[cs].flags) & CELL_WIDE_TAIL))
			cs--;
		// If range ends on a wide-head cell, include its tail for screen copy.
		if (ce < columns - 1 &&
		    ((out_buf[ce].flags | sp[ce].flags) & CELL_WIDE_HEAD))
			ce++;
		// is there a change between virtual screen and out_buf
		if (changed) {
			int i;
			// copy changed part of buffer to virtual screen
			for (i = cs; i <= ce; i++)
				sp[i] = out_buf[i];
			place_cursor(li, cs);
			// write line out to terminal
			for (i = cs; i <= ce; i++) {
				if (sp[i].flags & CELL_WIDE_TAIL)
					continue;
				if (sp[i].len)
					fwrite(sp[i].bytes, sp[i].len, 1, stdout);
			}
		}
	}

	place_cursor(crow, ccol);

	if (!keep_index) {
		cindex = (ccol - gutter) + offset;
		if (cindex < 0)
			cindex = 0;
	}

	old_offset = offset;
#undef old_offset
}

//----- Force refresh of all Lines -----------------------------
static void
redraw(int full_screen)
{
	// cursor to top,left; clear to the end of screen
	write1(ESC_SET_CURSOR_TOPLEFT ESC_CLEAR2EOS);
	screen_erase();        // erase the internal screen buffer
	last_status_cksum = 0; // force status update
	refresh(full_screen);  // this will redraw the entire display
	show_status_line();
}

//----- Flash the screen  --------------------------------------
static void
flash(int h)
{
	// standout_start();
	// redraw(TRUE);
	write1(ESC "[?5h"); // "reverse screen on"

	mysleep(h);

	// standout_end();
	// redraw(TRUE);
	write1(ESC "[?5l"); // "reverse screen off"
}

static void
indicate_error(void)
{
	cmd_error = TRUE;
	if (!err_method) {
		write1(ESC_BELL);
	} else {
		flash(10);
	}
}

//----- IO Routines --------------------------------------------
static int
readit(void) // read (maybe cursor) key from stdin
{
	int c;

	fflush_all();

	// Wait for input. TIMEOUT = -1 makes read_key wait even
	// on nonblocking stdin.
	// Note: read_key sets errno to 0 on success.
again:
	c = safe_read_key(STDIN_FILENO, readbuffer, /*timeout:*/ -1);
	if (c == -1) {               // EOF/error
		if (errno == EAGAIN) // paranoia
			goto again;
		go_bottom_and_clear_to_eol();
		cookmode(); // terminal to "cooked"
		bb_simple_error_msg_and_die("can't read user input");
	}
	return c;
}

static int
get_one_char(void)
{
	int c;

	if (!adding2q) {
		// we are not adding to the q.
		// but, we may be reading from a saved q.
		// (checking "ioq" for NULL is wrong, it's not reset to NULL
		// when done - "ioq_start" is reset instead).
		if (ioq_start != NULL) {
			// there is a queue to get chars from.
			// careful with correct sign expansion!
			c = (unsigned char)*ioq++;
			if (c != '\0')
				return c;
			// the end of the q
			free(ioq_start);
			ioq_start = NULL;
			// read from STDIN:
		}
		return readit();
	}
	// we are adding STDIN chars to q.
	c = readit();
	if (lmc_len >= ARRAY_SIZE(last_modifying_cmd) - 2) {
		// last_modifying_cmd[] is too small, can't remember the cmd
		// - drop it
		adding2q = 0;
		lmc_len = 0;
	} else {
		last_modifying_cmd[lmc_len++] = c;
	}
	return c;
}

// Get type of thing to operate on and adjust count
static int
get_motion_char(void)
{
	int c, cnt;

	c = get_one_char();
	if (isdigit(c)) {
		if (c != '0') {
			// get any non-zero motion count
			for (cnt = 0; isdigit(c); c = get_one_char())
				cnt = cnt * 10 + (c - '0');
			cmdcnt = (cmdcnt ? cmdcnt : 1) * cnt;
		} else {
			// ensure standalone '0' works
			cmdcnt = 0;
		}
	}

	return c;
}

// Get input line (uses "status line" area)
static char *
get_input_line(const char *prompt)
{
	// char [MAX_INPUT_LEN]
#define buf get_input_line__buf

	int c;
	int i;

	strcpy(buf, prompt);
	last_status_cksum = 0; // force status update
	go_bottom_and_clear_to_eol();
	write1(buf); // write out the :, /, or ? prompt

	i = strlen(buf);
	while (i < MAX_INPUT_LEN - 1) {
		c = get_one_char();
		if (c == '\n' || c == '\r' || c == 27)
			break; // this is end of input
		if (isbackspace(c)) {
			// user wants to erase prev char
			buf[--i] = '\0';
			go_bottom_and_clear_to_eol();
			if (i <= 0) // user backs up before b-o-l, exit
				break;
			write1(buf);
		} else if (c > 0 && c < 256) { // exclude Unicode
			// (TODO: need to handle Unicode)
			buf[i] = c;
			buf[++i] = '\0';
			bb_putchar(c);
		}
	}
	refresh(FALSE);
	return buf;
#undef buf
}

static void
Hit_Return(void)
{
	int c;

	standout_start();
	write1("[Hit return to continue]");
	standout_end();
	while ((c = get_one_char()) != '\n' && c != '\r')
		continue;
	redraw(TRUE); // force redraw all
}

//----- Draw the status line at bottom of the screen -------------
// show file status on status line
static int
format_edit_status(void)
{
	static const char *const cmd_mode_indicator[] = {"-", "INSERT -", "REPLACE -",
	                                                 "-"};

#define tot format_edit_status__tot

	int cur, percent, ret, trunc_at;

	// modified_count is now a counter rather than a flag.  this
	// helps reduce the amount of line counting we need to do.
	// (this will cause a mis-reporting of modified status
	// once every MAXINT editing operations.)

	// it would be nice to do a similar optimization here -- if
	// we haven't done a motion that could have changed which line
	// we're on, then we shouldn't have to do this count_lines()
	cur = count_lines(text, dot);

	// count_lines() is expensive.
	// Call it only if something was changed since last time
	// we were here:
	if (modified_count != last_modified_count) {
		tot = cur + count_lines(dot, end - 1) - 1;
		last_modified_count = modified_count;
	}

	//    current line         percent
	//   -------------    ~~ ----------
	//    total lines            100
	if (tot > 0) {
		percent = (100 * cur) / tot;
	} else {
		cur = tot = 0;
		percent = 100;
	}

	trunc_at = columns < STATUS_BUFFER_LEN - 1 ? columns : STATUS_BUFFER_LEN - 1;

	ret = snprintf(status_buffer, trunc_at + 1, "%s %s%s%s %d/%d %d%%",
	               cmd_mode_indicator[cmd_mode & 3],
	               (current_filename != NULL ? current_filename : "No file"),
	               (readonly_mode ? " [Readonly]" : ""),
	               (modified_count ? " [Modified]" : ""), cur, tot, percent);

	if (ret >= 0 && ret < trunc_at)
		return ret; // it all fit

	return trunc_at; // had to truncate
#undef tot
}

static uint32_t
bufsum(const char *buf, int count)
{
	// FNV-1a 32-bit hash. Used to detect status line changes.
	uint32_t h = 2166136261u;
	for (int i = 0; i < count; i++) {
		h ^= (unsigned char)buf[i];
		h *= 16777619u;
	}
	// Mix in length to reduce ambiguities for different-length strings
	h ^= (uint32_t)count;
	h *= 16777619u;
	return h;
}

static void
show_status_line(void)
{
	int cnt = 0;
	uint32_t cksum = 0;

	// either we already have an error or status message, or we
	// create one.
	if (!have_status_msg) {
		cnt = format_edit_status();
		cksum = bufsum(status_buffer, cnt);
	}
	if (have_status_msg || ((cnt > 0 && last_status_cksum != cksum))) {
		last_status_cksum = cksum; // remember if we have seen this line
		go_bottom_and_clear_to_eol();
		write1(status_buffer);
		if (have_status_msg) {
			int n = (int)strlen(status_buffer) - (have_status_msg - 1);
			// careful with int->unsigned promotion in comparison!
			if (n >= 0 && n >= columns)
				Hit_Return();
			have_status_msg = 0;
		}
		place_cursor(crow, ccol); // put cursor back in correct place
	}
	fflush_all();
}

//----- format the status buffer, the bottom line of screen ------
static void
status_line(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vsnprintf(status_buffer, STATUS_BUFFER_LEN, format, args);
	va_end(args);

	have_status_msg = 1;
}

static void
status_line_bold(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	strcpy(status_buffer, ESC_BOLD_TEXT);
	vsnprintf(status_buffer + (sizeof(ESC_BOLD_TEXT) - 1),
	          STATUS_BUFFER_LEN - sizeof(ESC_BOLD_TEXT) - sizeof(ESC_NORM_TEXT),
	          format, args);
	strcat(status_buffer, ESC_NORM_TEXT);
	va_end(args);

	have_status_msg =
	    1 + (sizeof(ESC_BOLD_TEXT) - 1) + (sizeof(ESC_NORM_TEXT) - 1);
}

static void
status_line_bold_errno(const char *fn)
{
	status_line_bold("'%s' " STRERROR_FMT, fn STRERROR_ERRNO);
}

// copy s to buf, convert unprintable
static void
print_literal(char *buf, const char *s)
{
	char *d;
	unsigned char c;

	if (!s[0])
		s = "(NULL)";

	d = buf;
	for (; *s; s++) {
		c = *s;
		if ((c & 0x80) && !Isprint(c))
			c = '?';
		if (c < ' ' || c == 0x7f) {
			*d++ = '^';
			c |= '@'; // 0x40
			if (c == 0x7f)
				c = '?';
		}
		*d++ = c;
		*d = '\0';
		if (d - buf > MAX_INPUT_LEN - 10) // paranoia
			break;
	}
}
static void
not_implemented(const char *s)
{
	char buf[MAX_INPUT_LEN];
	print_literal(buf, s);
	status_line_bold("'%s' is not implemented", buf);
}

//----- Block insert/delete, undo ops --------------------------
// copy text into a register
static char *
text_yank(char *p, char *q, int dest, int buftype)
{
	char *oldreg = reg[dest];
	int cnt = q - p;
	if (cnt < 0) { // they are backwards- reverse them
		p = q;
		cnt = -cnt;
	}
	// Don't free register yet.  This prevents the memory allocator
	// from reusing the free block so we can detect if it's changed.
	reg[dest] = xstrndup(p, cnt + 1);
	regtype[dest] = buftype;
	free(oldreg);
	return p;
}

static char
what_reg(void)
{
	char c;

	c = 'D'; // default to D-reg
	if (YDreg <= 25)
		c = 'a' + (char)YDreg;
	if (YDreg == 26)
		c = 'D';
	if (YDreg == 27)
		c = 'U';
	return c;
}

static void
check_context(char cmd)
{
	// Certain movement commands update the context.
	if (strchr(":%{}'GHLMz/?Nn", cmd) != NULL) {
		mark[27] = mark[26]; // move cur to prev
		mark[26] = dot;      // move local to cur
	}
}

static char *
swap_context(
    char *p) // goto new context for '' command make this the current context
{
	char *tmp;

	// the current context is in mark[26]
	// the previous context is in mark[27]
	// only swap context if other context is valid
	if (text <= mark[27] && mark[27] <= end - 1) {
		tmp = mark[27];
		mark[27] = p;
		mark[26] = p = tmp;
	}
	return p;
}

static void
yank_status(const char *op, const char *p, int cnt)
{
	int lines, chars;

	lines = chars = 0;
	while (*p) {
		++chars;
		if (*p++ == '\n')
			++lines;
	}
	status_line("%s %d lines (%d chars) from [%c]", op, lines * cnt, chars * cnt,
	            what_reg());
}

static void undo_push(char *, unsigned, int);

// open a hole in text[]
// might reallocate text[]! use p += text_hole_make(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t
text_hole_make(char *p,
               int size) // at "p", make a 'size' byte hole
{
	uintptr_t bias = 0;

	if (size <= 0)
		return bias;
	end += size; // adjust the new END
	if (end >= (text + text_size)) {
		char *new_text;
		text_size += end - (text + text_size) + 10240;
		new_text = xrealloc(text, text_size);
		bias = (new_text - text);
		screenbegin += bias;
		dot += bias;
		end += bias;
		p += bias;
		{
			int i;
			for (i = 0; i < ARRAY_SIZE(mark); i++)
				if (mark[i])
					mark[i] += bias;
		}
		text = new_text;
	}
	memmove(p + size, p, end - size - p);
	memset(p, ' ', size); // clear new hole
	return bias;
}

// close a hole in text[] - delete "p" through "q", inclusive
// "undo" value indicates if this operation should be undo-able
static char *
text_hole_delete(char *p, char *q, int undo)
{
	char *src, *dest;
	int cnt, hole_size;

	// move forwards, from beginning
	// assume p <= q
	src = q + 1;
	dest = p;
	if (q < p) { // they are backward- swap them
		src = p + 1;
		dest = q;
	}
	hole_size = q - p + 1;
	cnt = end - src;
	switch (undo) {
	case NO_UNDO:
		break;
	case ALLOW_UNDO:
		undo_push(p, hole_size, UNDO_DEL);
		break;
	case ALLOW_UNDO_CHAIN:
		undo_push(p, hole_size, UNDO_DEL_CHAIN);
		break;
	case ALLOW_UNDO_QUEUED:
		undo_push(p, hole_size, UNDO_DEL_QUEUED);
		break;
	}
	modified_count--;
	if (src < text || src > end)
		goto thd0;
	if (dest < text || dest >= end)
		goto thd0;
	modified_count++;
	if (src >= end)
		goto thd_atend; // just delete the end of the buffer
	memmove(dest, src, cnt);
thd_atend:
	end = end - hole_size; // adjust the new END
	if (dest >= end)
		dest = end - 1; // make sure dest in below end-1
	if (end <= text)
		dest = end = text; // keep pointers valid
thd0:
	return dest;
}

// Flush any queued objects to the undo stack
static void
undo_queue_commit(void)
{
	// Pushes the queue object onto the undo stack
	if (undo_q > 0) {
		// Deleted character undo events grow from the end
		undo_push(undo_queue + CONFIG_FEATURE_VI_UNDO_QUEUE_MAX - undo_q, undo_q,
		          (undo_queue_state | UNDO_USE_SPOS));
		undo_queue_state = UNDO_EMPTY;
		undo_q = 0;
	}
}

static void
flush_undo_data(void)
{
	struct undo_object *undo_entry;

	while (undo_stack_tail) {
		undo_entry = undo_stack_tail;
		undo_stack_tail = undo_entry->prev;
		free(undo_entry);
	}
}

// Undo functions and hooks added by Jody Bruchon (jody@jodybruchon.com)
// Add to the undo stack
static void
undo_push(char *src, unsigned length, int u_type)
{
	struct undo_object *undo_entry;
	int use_spos = u_type & UNDO_USE_SPOS;

	// "u_type" values
	// UNDO_INS: insertion, undo will remove from buffer
	// UNDO_DEL: deleted text, undo will restore to buffer
	// UNDO_{INS,DEL}_CHAIN: Same as above but also calls undo_pop() when complete
	// The CHAIN operations are for handling multiple operations that the user
	// performs with a single action, i.e. REPLACE mode or find-and-replace
	// commands UNDO_{INS,DEL}_QUEUED: If queuing feature is enabled, allow use of
	// the queue for the INS/DEL operation. UNDO_{INS,DEL} ORed with
	// UNDO_USE_SPOS: commit the undo queue

	// This undo queuing functionality groups multiple character typing or
	// backspaces into a single large undo object. This greatly reduces calls to
	// malloc() for single-character operations while typing and has the side
	// benefit of letting an undo operation remove chunks of text rather than a
	// single character.
	switch (u_type) {
	case UNDO_EMPTY: // Just in case this ever happens...
		return;
	case UNDO_DEL_QUEUED:
		if (length != 1)
			return; // Only queue single characters
		switch (undo_queue_state) {
		case UNDO_EMPTY:
			undo_queue_state = UNDO_DEL;
			/* fall through */
		case UNDO_DEL:
			undo_queue_spos = src;
			undo_q++;
			undo_queue[CONFIG_FEATURE_VI_UNDO_QUEUE_MAX - undo_q] = *src;
			// If queue is full, dump it into an object
			if (undo_q == CONFIG_FEATURE_VI_UNDO_QUEUE_MAX)
				undo_queue_commit();
			return;
		case UNDO_INS:
			// Switch from storing inserted text to deleted text
			undo_queue_commit();
			undo_push(src, length, UNDO_DEL_QUEUED);
			return;
		}
		break;
	case UNDO_INS_QUEUED:
		if (length < 1)
			return;
		switch (undo_queue_state) {
		case UNDO_EMPTY:
			undo_queue_state = UNDO_INS;
			undo_queue_spos = src;
		case UNDO_INS:
			while (length--) {
				undo_q++; // Don't need to save any data for insertions
				if (undo_q == CONFIG_FEATURE_VI_UNDO_QUEUE_MAX)
					undo_queue_commit();
			}
			return;
		case UNDO_DEL:
			// Switch from storing deleted text to inserted text
			undo_queue_commit();
			undo_push(src, length, UNDO_INS_QUEUED);
			return;
		}
		break;
	}
	u_type &= ~UNDO_USE_SPOS;

	// Allocate a new undo object
	if (u_type == UNDO_DEL || u_type == UNDO_DEL_CHAIN) {
		// For UNDO_DEL objects, save deleted text
		if ((text + length) == end)
			length--;
		// If this deletion empties text[], strip the newline. When the buffer
		// becomes zero-length, a newline is added back, which requires this to
		// compensate.
		undo_entry = xzalloc(offsetof(struct undo_object, undo_text) + length);
		memcpy(undo_entry->undo_text, src, length);
	} else {
		undo_entry = xzalloc(sizeof(*undo_entry));
	}
	undo_entry->length = length;
	if (use_spos) {
		undo_entry->start = undo_queue_spos - text; // use start position from queue
	} else {
		undo_entry->start = src - text; // use offset from start of text buffer
	}
	undo_entry->u_type = u_type;

	// Push it on undo stack
	undo_entry->prev = undo_stack_tail;
	undo_stack_tail = undo_entry;
	modified_count++;
}

static void
undo_push_insert(char *p, int len, int undo)
{
	switch (undo) {
	case ALLOW_UNDO:
		undo_push(p, len, UNDO_INS);
		break;
	case ALLOW_UNDO_CHAIN:
		undo_push(p, len, UNDO_INS_CHAIN);
		break;
	case ALLOW_UNDO_QUEUED:
		undo_push(p, len, UNDO_INS_QUEUED);
		break;
	}
}

// Undo the last operation
static void
undo_pop(void)
{
	int repeat;
	char *u_start, *u_end;
	struct undo_object *undo_entry;

	// Commit pending undo queue before popping (should be unnecessary)
	undo_queue_commit();

	undo_entry = undo_stack_tail;
	// Check for an empty undo stack
	if (!undo_entry) {
		status_line("Already at oldest change");
		return;
	}

	switch (undo_entry->u_type) {
	case UNDO_DEL:
	case UNDO_DEL_CHAIN:
		// make hole and put in text that was deleted; deallocate text
		u_start = text + undo_entry->start;
		text_hole_make(u_start, undo_entry->length);
		memcpy(u_start, undo_entry->undo_text, undo_entry->length);
		status_line("Undo [%d] %s %d chars at position %d", modified_count,
		            "restored", undo_entry->length, undo_entry->start);
		break;
	case UNDO_INS:
	case UNDO_INS_CHAIN:
		// delete what was inserted
		u_start = undo_entry->start + text;
		u_end = u_start - 1 + undo_entry->length;
		text_hole_delete(u_start, u_end, NO_UNDO);
		status_line("Undo [%d] %s %d chars at position %d", modified_count,
		            "deleted", undo_entry->length, undo_entry->start);
		break;
	}
	repeat = 0;
	switch (undo_entry->u_type) {
	// If this is the end of a chain, lower modification count and refresh display
	case UNDO_DEL:
	case UNDO_INS:
		dot = (text + undo_entry->start);
		refresh(FALSE);
		break;
	case UNDO_DEL_CHAIN:
	case UNDO_INS_CHAIN:
		repeat = 1;
		break;
	}
	// Deallocate the undo object we just processed
	undo_stack_tail = undo_entry->prev;
	free(undo_entry);
	modified_count--;
	// For chained operations, continue popping all the way down the chain.
	if (repeat) {
		undo_pop(); // Follow the undo chain if one exists
	}
}

//----- Dot Movement Routines ----------------------------------
static void
dot_left(void)
{
	undo_queue_commit();
	if (dot <= text)
		return;
	if (dot[-1] == '\n')
		return;

	dot = utf8_prev_cp(begin_line(dot), dot);
}

static void
dot_right(void)
{
	undo_queue_commit();
	if (dot >= end - 1)
		return;
	if (*dot == '\n')
		return;

	dot = utf8_next_cp(dot);
}

static void
dot_begin(void)
{
	undo_queue_commit();
	dot = begin_line(dot); // return pointer to first char cur line
}

static void
dot_end(void)
{
	undo_queue_commit();
	dot = end_line(dot); // return pointer to last char cur line
}

static char *
move_to_col(char *p, int l)
{
	int co = 0;
	mbstate_t st;

	p = begin_line(p);
	memset(&st, 0, sizeof(st));
	while (p < end && *p != '\n') {
		unsigned char b = (unsigned char)*p;
		size_t n;
		int w;
		wchar_t wc;

		if (co >= l)
			break;
		if (b == '\t') {
			w = next_tabstop(co) - co;
			n = 1;
			memset(&st, 0, sizeof(st));
		} else if (b < ' ' || b == 0x7f) {
			w = 2;
			n = 1;
			memset(&st, 0, sizeof(st));
		} else {
			n = mb_decode_1(&wc, p, (size_t)(end - p), &st);
			w = wc_display_width(wc);
			if (w < 0)
				w = 1;
		}

		// If the target column falls inside a multi-column glyph/tab,
		// stop at its first byte.
		if (co + w > l)
			break;
		co += (w > 0) ? w : 0;
		p += n;
	}
	return p;
}

static void
dot_next(void)
{
	undo_queue_commit();
	dot = next_line(dot);
}

static void
dot_prev(void)
{
	undo_queue_commit();
	dot = prev_line(dot);
}

static void
dot_skip_over_ws(void)
{
	// skip WS
	while (isspace(*dot) && *dot != '\n' && dot < end - 1)
		dot++;
}

static void
dot_to_char(int cmd)
{
	char *q = dot;
	int dir = islower(cmd) ? FORWARD : BACK;

	if (last_search_char == 0)
		return;

	do {
		do {
			q += dir;
			if ((dir == FORWARD ? q > end - 1 : q < text) || *q == '\n') {
				indicate_error();
				return;
			}
		} while (*q != last_search_char);
	} while (--cmdcnt > 0);

	dot = q;

	// place cursor before/after char as required
	if (cmd == 't')
		dot_left();
	else if (cmd == 'T')
		dot_right();
}

static void
dot_scroll(int cnt, int dir)
{
	char *q;

	undo_queue_commit();
	for (; cnt > 0; cnt--) {
		if (dir < 0) {
			// scroll Backwards
			// ctrl-Y scroll up one line
			screenbegin = prev_line(screenbegin);
		} else {
			// scroll Forwards
			// ctrl-E scroll down one line
			screenbegin = next_line(screenbegin);
		}
	}
	// make sure "dot" stays on the screen so we dont scroll off
	if (dot < screenbegin)
		dot = screenbegin;
	q = end_screen(); // find new bottom line
	if (dot > q)
		dot = begin_line(q); // is dot is below bottom line?
	dot_skip_over_ws();
}

static char *
bound_dot(char *p) // make sure  text[0] <= P < "end"
{
	if (p >= end && end > text) {
		p = end - 1;
		indicate_error();
	}
	if (p < text) {
		p = text;
		indicate_error();
	}
	return p;
}

static void
start_new_cmd_q(char c)
{
	// get buffer for new cmd
	dotcnt = cmdcnt ? cmdcnt : 1;
	last_modifying_cmd[0] = c;
	lmc_len = 1;
	adding2q = 1;
}
static void
end_cmd_q(void)
{
	YDreg = 26; // go back to default Yank/Delete reg
	adding2q = 0;
}

// copy text into register, then delete text.
//
static char *
yank_delete(char *start, char *stop, int buftype, int yf,
            int undo)
{
	char *p;

	// make sure start <= stop
	if (start > stop) {
		// they are backwards, reverse them
		p = start;
		start = stop;
		stop = p;
	}
	if (buftype == PARTIAL && *start == '\n')
		return start;
	p = start;
	text_yank(start, stop, YDreg, buftype);
	if (yf == YANKDEL) {
		p = text_hole_delete(start, stop, undo);
	} // delete lines
	return p;
}

// might reallocate text[]!
static int
file_insert(const char *fn, char *p, int initial)
{
	int cnt = -1;
	int fd, size;
	struct stat statbuf;

	if (p < text)
		p = text;
	if (p > end)
		p = end;

	fd = open(fn, O_RDONLY);
	if (fd < 0) {
		if (!initial)
			status_line_bold_errno(fn);
		return cnt;
	}

	// Validate file
	if (fstat(fd, &statbuf) < 0) {
		status_line_bold_errno(fn);
		goto fi;
	}
	if (!S_ISREG(statbuf.st_mode)) {
		status_line_bold("'%s' is not a regular file", fn);
		goto fi;
	}
	size = (statbuf.st_size < INT_MAX ? (int)statbuf.st_size : INT_MAX);
	p += text_hole_make(p, size);
	cnt = full_read(fd, p, size);
	if (cnt < 0) {
		status_line_bold_errno(fn);
		p = text_hole_delete(p, p + size - 1, NO_UNDO); // un-do buffer insert
	} else if (cnt < size) {
		// There was a partial read, shrink unused space
		p = text_hole_delete(p + cnt, p + size - 1, NO_UNDO);
		status_line_bold("can't read '%s'", fn);
	} else {
		undo_push_insert(p, size, ALLOW_UNDO);
	}
fi:
	close(fd);

	if (initial && ((access(fn, W_OK) < 0) ||
	                // root will always have access()
	                // so we check fileperms too
	                !(statbuf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)))) {
		SET_READONLY_FILE(readonly_mode);
	}
	return cnt;
}

// find matching char of pair  ()  []  {}
// will crash if c is not one of these
static char *
find_pair(char *p, const char c)
{
	const char *braces = "()[]{}";
	char match;
	int dir, level;

	dir = strchr(braces, c) - braces;
	dir ^= 1;
	match = braces[dir];
	dir = ((dir & 1) << 1) - 1; // 1 for ([{, -1 for )\}

	// look for match, count levels of pairs  (( ))
	level = 1;
	for (;;) {
		p += dir;
		if (p < text || p >= end)
			return NULL;
		if (*p == c)
			level++; // increase pair levels
		if (*p == match) {
			level--; // reduce pair level
			if (level == 0)
				return p; // found matching pair
		}
	}
}

// show the matching char of a pair,  ()  []  {}
static void
showmatching(char *p)
{
	char *q, *save_dot;

	// we found half of a pair
	q = find_pair(p, *p); // get loc of matching char
	if (q == NULL) {
		indicate_error(); // no matching char
	} else {
		// "q" now points to matching pair
		save_dot = dot; // remember where we are
		dot = q;        // go to new loc
		refresh(FALSE); // let the user see it
		mysleep(40);    // give user some time
		dot = save_dot; // go back to old loc
		refresh(FALSE);
	}
}

// might reallocate text[]! use p += stupid_insert(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t
stupid_insert(char *p,
              char c) // stupidly insert the char c at 'p'
{
	uintptr_t bias;
	bias = text_hole_make(p, 1);
	p += bias;
	*p = c;
	return bias;
}

// find number of characters in indent, p must be at beginning of line
static size_t
indent_len(char *p)
{
	char *r = p;

	while (r < (end - 1) && isblank(*r))
		r++;
	return r - p;
}

static char *
char_insert(char *p, char c, int undo) // insert the char c at 'p'
{
#define indentcol char_insert__indentcol
	size_t len;
	int col, ntab, nspc;
	char *bol = begin_line(p);

	if (c == 22) {                      // Is this an ctrl-V?
		p += stupid_insert(p, '^'); // use ^ to indicate literal next
		refresh(FALSE);             // show the ^
		c = get_one_char();
		*p = c;
		undo_push_insert(p, 1, undo);
		p++;
	} else if (c == 27) { // Is this an ESC?
		cmd_mode = 0;
		undo_queue_commit();
		cmdcnt = 0;
		end_cmd_q();           // stop adding to q
		last_status_cksum = 0; // force status update
		if ((dot > text) && (p[-1] != '\n')) {
			p--;
		}
		if (autoindent) {
			len = indent_len(bol);
			col = get_column(bol + len);
			if (len && col == indentcol && bol[len] == '\n') {
				// remove autoindent from otherwise empty line
				text_hole_delete(bol, bol + len - 1, undo);
				p = bol;
			}
		}
	} else if (c == 4) { // ctrl-D reduces indentation
		char *r = bol + indent_len(bol);
		int prev = prev_tabstop(get_column(r));
		while (r > bol && get_column(r) > prev) {
			if (p > bol)
				p--;
			r--;
			r = text_hole_delete(r, r, ALLOW_UNDO_QUEUED);
		}

		if (autoindent && indentcol && r == end_line(p)) {
			// record changed size of autoindent
			indentcol = get_column(p);
			return p;
		}
	} else if (c == '\t' && expandtab) { // expand tab
		col = get_column(p);
		col = next_tabstop(col) - col + 1;
		while (col--) {
			undo_push_insert(p, 1, undo);
			p += 1 + stupid_insert(p, ' ');
		}
	} else if (isbackspace(c)) {
		if (cmd_mode == 2) {
			// special treatment for backspace in Replace mode
			if (p > rstart) {
				if (p[-1] == '\n')
					p--;
				else
					p = utf8_prev_cp(begin_line(p), p);
				undo_pop();
			}
		} else if (p > text) {
			char *cur = p;
			char *prev;
			// At BOL, allow deleting the newline (join lines)
			if (cur[-1] == '\n')
				prev = cur - 1;
			else
				prev = utf8_prev_cp(begin_line(cur), cur);

			if (prev != cur)
				p = text_hole_delete(prev, cur - 1, ALLOW_UNDO_QUEUED);
		}
	} else {
		// insert a char into text[]
		if (c == 13)
			c = '\n'; // translate \r to \n
		if (c == '\n')
			undo_queue_commit();
		undo_push_insert(p, 1, undo);
		p += 1 + stupid_insert(p, c); // insert the char
		if (showmatch && strchr(")]}", c) != NULL) {
			showmatching(p - 1);
		}
		if (autoindent && c == '\n') { // auto indent the new line
			if (newindent < 0) {
				// use indent of previous line
				bol = prev_line(p);
				len = indent_len(bol);
				col = get_column(bol + len);

				if (len && col == indentcol) {
					// previous line was empty except for autoindent
					// move the indent to the current line
					memmove(bol + 1, bol, len);
					*bol = '\n';
					return p;
				}
			} else {
				// for 'O'/'cc' commands add indent before newly inserted NL
				if (p != end - 1) // but not for 'cc' at EOF
					p--;
				col = newindent;
			}

			if (col) {
				// only record indent if in insert/replace mode or for
				// the 'o'/'O'/'cc' commands, which are switched to
				// insert mode early.
				indentcol = cmd_mode != 0 ? col : 0;
				if (expandtab) {
					ntab = 0;
					nspc = col;
				} else {
					ntab = col / tabstop;
					nspc = col % tabstop;
				}
				p += text_hole_make(p, ntab + nspc);
				undo_push_insert(p, ntab + nspc, undo);
				memset(p, '\t', ntab);
				p += ntab;
				memset(p, ' ', nspc);
				return p + nspc;
			}
		}
	}
	indentcol = 0;
#undef indentcol
	return p;
}

static void
init_filename(char *fn)
{
	char *copy = xstrdup(fn);

	if (current_filename == NULL) {
		current_filename = copy;
	} else {
		free(alt_filename);
		alt_filename = copy;
	}
}

static void
update_filename(char *fn)
{
	if (fn == NULL)
		return;

	if (current_filename == NULL || strcmp(fn, current_filename) != 0) {
		free(alt_filename);
		alt_filename = current_filename;
		current_filename = xstrdup(fn);
	}
}

// read text from file or create an empty buf
// will also update current_filename
static int
init_text_buffer(char *fn)
{
	int rc;

	// allocate/reallocate text buffer
	free(text);
	text_size = 10240;
	screenbegin = dot = end = text = xzalloc(text_size);

	update_filename(fn);
	rc = file_insert(fn, text, 1);
	if (rc <= 0 || *(end - 1) != '\n') {
		// file doesn't exist or doesn't end in a newline.
		// insert a newline to the end
		char_insert(end, '\n', NO_UNDO);
	}

	flush_undo_data();
	modified_count = 0;
	last_modified_count = -1;
	// init the marks
	memset(mark, 0, sizeof(mark));
	return rc;
}

// might reallocate text[]! use p += string_insert(p, ...),
// and be careful to not use pointers into potentially freed text[]!
static uintptr_t
string_insert(char *p, const char *s,
              int undo) // insert the string at 'p'
{
	uintptr_t bias;
	int i;

	i = strlen(s);
	undo_push_insert(p, i, undo);
	bias = text_hole_make(p, i);
	p += bias;
	memcpy(p, s, i);
	return bias;
}

static int
file_write(char *fn, char *first, char *last)
{
	int fd, cnt, charcnt;

	if (fn == 0) {
		status_line_bold("No current filename");
		return -2;
	}
	// By popular request we do not open file with O_TRUNC,
	// but instead ftruncate() it _after_ successful write.
	// Might reduce amount of data lost on power fail etc.
	fd = open(fn, (O_WRONLY | O_CREAT), 0666);
	if (fd < 0)
		return -1;
	cnt = last - first + 1;
	charcnt = full_write(fd, first, cnt);
	ftruncate(fd, charcnt);
	if (charcnt == cnt) {
		// good write
		// modified_count = FALSE;
	} else {
		charcnt = 0;
	}
	close(fd);
	return charcnt;
}

// search for pattern starting at p
static char *
char_search(char *p, const char *pat, int dir_and_range)
{
	regex_t preg;
	char errbuf[128];
	int cflags;
	int rc;

	// BusyBox vi historically used POSIX BASIC regex syntax
	cflags = REG_NEWLINE;
	if (ignorecase)
		cflags |= REG_ICASE;
	if (ere)
		cflags |= REG_EXTENDED;

	rc = regcomp(&preg, pat, cflags);
	if (rc != 0) {
		regerror(rc, NULL, errbuf, sizeof(errbuf));
		status_line_bold("bad search pattern '%s': %s", pat, errbuf);
		return p;
	}

	if (dir_and_range >= 0) {
		// forward search
		char *line = begin_line(p);
		char *line_end = end_line(p);
		char saved = *line_end;
		regmatch_t m;
		int eflags;
		char *found;

		*line_end = '\0';
		eflags = 0;
		if (p != line)
			eflags |= REG_NOTBOL;
		rc = regexec(&preg, p, 1, &m, eflags);
		*line_end = saved;
		if (rc == 0 && m.rm_so >= 0) {
			regfree(&preg);
			return p + m.rm_so;
		}

		if ((dir_and_range & 1) == LIMITED) {
			regfree(&preg);
			return NULL;
		}

		for (line = next_line(line); line < end; line = next_line(line)) {
			line_end = end_line(line);
			saved = *line_end;
			*line_end = '\0';
			rc = regexec(&preg, line, 1, &m, 0);
			*line_end = saved;
			if (rc == 0 && m.rm_so >= 0) {
				found = line + m.rm_so;
				regfree(&preg);
				return found;
			}
			if (line_end >= end - 1)
				break;
		}

		regfree(&preg);
		return NULL;
	}

	// backward search
	char *scan_start;
	char *scan_stop;
	char *line;
	char *last = NULL;

	if ((dir_and_range & 1) == LIMITED) {
		scan_start = begin_line(p);
		scan_stop = p;
	} else {
		scan_start = text;
		scan_stop = p;
	}

	for (line = scan_start; line < scan_stop;) {
		char *line_start = line;
		char *line_end = end_line(line_start);
		char *seg_end = line_end;
		int noteol = 0;
		char saved;
		char *s;
		char *last_in_line = NULL;
		regmatch_t m;
		int eflags;

		if (seg_end > scan_stop) {
			seg_end = scan_stop;
			noteol = 1;
		}

		saved = *seg_end;
		*seg_end = '\0';
		s = line_start;
		while (*s) {
			eflags = 0;
			if (s != line_start)
				eflags |= REG_NOTBOL;
			if (noteol)
				eflags |= REG_NOTEOL;
			rc = regexec(&preg, s, 1, &m, eflags);
			if (rc != 0 || m.rm_so < 0)
				break;
			last_in_line = s + m.rm_so;
			if (m.rm_eo > m.rm_so)
				s += m.rm_eo;
			else
				s += m.rm_so + 1; // avoid infinite loops on empty matches
		}
		*seg_end = saved;
		if (last_in_line)
			last = last_in_line;

		if (line_end >= scan_stop)
			break;
		line = next_line(line_end);
	}

	regfree(&preg);
	return last;
}

//----- The Colon commands -------------------------------------
// Evaluate colon address expression.  Returns a pointer to the
// next character or NULL on error.  If 'result' contains a valid
// address 'valid' is TRUE.
static char *
get_one_address(char *p, int *result, int *valid)
{
	int num, sign, addr, got_addr;
	char *q, c;
	IF_FEATURE_VI_SEARCH(int dir;)

	got_addr = FALSE;
	addr = count_lines(text, dot); // default to current line
	sign = 0;
	for (;;) {
		if (isblank(*p)) {
			if (got_addr) {
				addr += sign;
				sign = 0;
			}
			p++;
		} else if (!got_addr && *p == '.') { // the current line
			p++;
			// addr = count_lines(text, dot);
			got_addr = TRUE;
		} else if (!got_addr && *p == '$') { // the last line in file
			p++;
			addr = count_lines(text, end - 1);
			got_addr = TRUE;
		} else if (!got_addr && *p == '\'') { // is this a mark addr
			p++;
			c = tolower(*p);
			p++;
			q = NULL;
			if (c >= 'a' && c <= 'z') {
				// we have a mark
				c = c - 'a';
				q = mark[(unsigned char)c];
			}
			if (q == NULL) { // is mark valid
				status_line_bold("Mark not set");
				return NULL;
			}
			addr = count_lines(text, q);
			got_addr = TRUE;
		} else if (!got_addr && (*p == '/' || *p == '?')) { // a search pattern
			c = *p;
			q = strchrnul_portable(p + 1, c);
			if (p + 1 != q) {
				// save copy of new pattern
				free(last_search_pattern);
				last_search_pattern = xstrndup(p, q - p);
			}
			p = q;
			if (*p == c)
				p++;
			if (c == '/') {
				q = next_line(dot);
				dir = (FORWARD << 1) | FULL;
			} else {
				q = begin_line(dot);
				dir = ((unsigned)BACK << 1) | FULL;
			}
			q = char_search(q, last_search_pattern + 1, dir);
			if (q == NULL) {
				// no match, continue from other end of file
				q = char_search(dir > 0 ? text : end - 1, last_search_pattern + 1, dir);
				if (q == NULL) {
					status_line_bold("Pattern not found");
					return NULL;
				}
			}
			addr = count_lines(text, q);
			got_addr = TRUE;
		} else if (isdigit(*p)) {
			num = 0;
			while (isdigit(*p))
				num = num * 10 + *p++ - '0';
			if (!got_addr) { // specific line number
				addr = num;
				got_addr = TRUE;
			} else { // offset from current addr
				addr += sign >= 0 ? num : -num;
			}
			sign = 0;
		} else if (*p == '-' || *p == '+') {
			if (!got_addr) { // default address is dot
				// addr = count_lines(text, dot);
				got_addr = TRUE;
			} else {
				addr += sign;
			}
			sign = *p++ == '-' ? -1 : 1;
		} else {
			addr += sign; // consume unused trailing sign
			break;
		}
	}
	*result = addr;
	*valid = got_addr;
	return p;
}

#define GET_ADDRESS 0
#define GET_SEPARATOR 1

// Read line addresses for a colon command.  The user can enter as
// many as they like but only the last two will be used.
static char *
get_address(char *p, int *b, int *e, unsigned *got)
{
	int state = GET_ADDRESS;
	int valid;
	int addr;
	char *save_dot = dot;

	//----- get the address' i.e., 1,3   'a,'b  -----
	for (;;) {
		if (isblank(*p)) {
			p++;
		} else if (state == GET_ADDRESS && *p == '%') { // alias for 1,$
			p++;
			*b = 1;
			*e = count_lines(text, end - 1);
			*got = 3;
			state = GET_SEPARATOR;
		} else if (state == GET_ADDRESS) {
			valid = FALSE;
			p = get_one_address(p, &addr, &valid);
			// Quit on error or if the address is invalid and isn't of
			// the form ',$' or '1,' (in which case it defaults to dot).
			if (p == NULL || !(valid || *p == ',' || *p == ';' || *got & 1))
				break;
			*b = *e;
			*e = addr;
			*got = (*got << 1) | 1;
			state = GET_SEPARATOR;
		} else if (state == GET_SEPARATOR && (*p == ',' || *p == ';')) {
			if (*p == ';')
				dot = find_line(*e);
			p++;
			state = GET_ADDRESS;
		} else {
			break;
		}
	}
	dot = save_dot;
	return p;
}

static void
setops(char *args, int flg_no)
{
	char *eq;
	int index;

	eq = strchr(args, '=');
	if (eq)
		*eq = '\0';
	index = index_in_strings(OPTS_STR, args + flg_no);
	if (eq)
		*eq = '=';
	if (index < 0) {
	bad:
		status_line_bold("bad option: %s", args);
		return;
	}

	index = 1 << (index >> 1); // convert to VI_bit

	if (index & VI_TABSTOP) {
		int t;
		if (!eq || flg_no) // no "=NNN" or it is "notabstop"?
			goto bad;
		t = bb_strtou(eq + 1, NULL, 10);
		if (t <= 0 || t > MAX_TABSTOP)
			goto bad;
		tabstop = t;
		return;
	}
	if (eq)
		goto bad; // boolean option has "="?
	if (flg_no) {
		vi_setops &= ~index;
	} else {
		vi_setops |= index;
	}
}

static char *
expand_args(char *args)
{
	char *s;
	const char *replace;

	args = xstrdup(args);
	for (s = args; *s; s++) {
		unsigned n;

		if (*s == '%') {
			replace = current_filename;
		} else if (*s == '#') {
			replace = alt_filename;
		} else {
			if (*s == '\\' && s[1] != '\0') {
				char *t;
				for (t = s; *t; t++)
					*t = t[1];
				s++;
			}
			continue;
		}

		if (replace == NULL) {
			free(args);
			status_line_bold("No previous filename");
			return NULL;
		}

		n = (s - args);
		xasprintf_inplace(args, "%.*s%s%s", n, args, replace, s + 1);
		s = args + n + strlen(replace);
	}
	return args;
}

#define MAX_SUBPATTERN 10 // subpatterns \0 .. \9

// Like strchr() but skipping backslash-escaped characters
static char *
strchr_backslash(const char *s, int c)
{
	while (*s) {
		if (*s == c)
			return (char *)s;
		if (*s == '\\')
			if (*++s == '\0')
				break;
		s++;
	}
	return NULL;
}

// If the return value is not NULL the caller should free R
static char *
regex_search(char *q, regex_t *preg, const char *Rorig,
             size_t *len_F, size_t *len_R, char **R)
{
	regmatch_t regmatch[MAX_SUBPATTERN], *cur_match;
	char *found = NULL;
	const char *t;
	char *r;
	char *line_start = begin_line(q);
	char *line_end = end_line(q);
	char saved = *line_end;
	int eflags = 0;

	*line_end = '\0';
	if (q != line_start)
		eflags |= REG_NOTBOL;
	if (regexec(preg, q, MAX_SUBPATTERN, regmatch, eflags) != 0) {
		*line_end = saved;
		return found;
	}

	*line_end = saved;
	found = q + regmatch[0].rm_so;
	*len_F = regmatch[0].rm_eo - regmatch[0].rm_so;
	*R = NULL;

fill_result:
	// first pass calculates len_R, second fills R
	*len_R = 0;
	for (t = Rorig, r = *R; *t; t++) {
		size_t len = 1; // default is to copy one char from replace pattern
		const char *from = t;
		if (*t == '\\') {
			from = ++t; // skip backslash
			if (*t >= '0' && *t < '0' + MAX_SUBPATTERN) {
				cur_match = regmatch + (*t - '0');
				if (cur_match->rm_so >= 0) {
					len = cur_match->rm_eo - cur_match->rm_so;
					from = q + cur_match->rm_so;
				}
			}
		}
		*len_R += len;
		if (*R) {
			memcpy(r, from, len);
			r += len;
			/* *r = '\0'; - xzalloc did it */
		}
	}
	if (*R == NULL) {
		*R = xzalloc(*len_R + 1);
		goto fill_result;
	}

	return found;
}

static void
colon(char *buf)
{
	char cmd[sizeof("features!")]; // longest known command + NUL
	char *args;
	int cmdlen;
	char *useforce;
	char *q, *r;
	int b, e;
// check how many addresses we got
#define GOT_ADDRESS (got & 1)
#define GOT_RANGE ((got & 3) == 3)
	unsigned got;
	char *exp =
	    NULL; // may hold expand_args() result: if VI_COLON_EXPAND, needs freeing!

	// :3154	// if (-e line 3154) goto it  else stay put
	// :4,33w! foo	// write a portion of buffer to file "foo"
	// :w		// write all of buffer to current file
	// :q		// quit
	// :q!		// quit- dont care about modified file
	// :'a,'z!sort -u   // filter block through sort
	// :'f		// goto mark "f"
	// :'fl		// list literal the mark "f" line
	// :.r bar	// read file "bar" into buffer before dot
	// :/123/,/abc/d    // delete lines from "123" line to "abc" line
	// :/xyz/	// goto the "xyz" line
	// :s/find/replace/ // substitute pattern "find" with "replace"
	// :!<cmd>	// run <cmd> then return

	while (*buf == ':')
		buf++;              // move past leading colons
	buf = skip_whitespace(buf); // move past leading blanks
	if (!*buf || *buf == '"')
		goto ret; // ignore empty lines or those starting with '"'

	// look for optional address(es)  ":." ":1" ":1,9" ":'q,'a" ":%"
	b = e = -1;
	got = 0;
	buf = get_address(buf, &b, &e, &got);
	if (buf == NULL)
		goto ret;

	// get the COMMAND into cmd[]
	safe_strncpy(cmd, buf, sizeof(cmd));
	skip_non_whitespace(cmd)[0] = '\0';
	useforce = last_char_is(cmd, '!');
	if (useforce && useforce > cmd)
		*useforce = '\0'; // "CMD!" -> "CMD" (unless single "!")
	// find ARGuments
	args = skip_whitespace(skip_non_whitespace(buf));

	// assume the command will want a range, certain commands
	// (read, substitute) need to adjust these assumptions
	q = text; // if no addr, use 1,$ for the range
	r = end - 1;
	if (GOT_ADDRESS) { // at least one addr was given, get its details
		int lines;
		if (e < 0 || e > (lines = count_lines(text, end - 1))) {
			status_line_bold("Invalid range");
			goto ret;
		}
		q = r = find_line(e);
		if (!GOT_RANGE) {
			// if there is only one addr, then it's the line
			// number of the single line the user wants.
			// Reset the end pointer to the end of that line.
			r = end_line(q);
		} else {
			// we were given two addrs.  change the
			// start pointer to the addr given by user.
			if (b < 0 || b > lines || b > e) {
				status_line_bold("Invalid range");
				goto ret;
			}
			q = find_line(b); // what line is #b
			r = end_line(r);
		}
	}
	// ------------ now look for the command ------------
	cmdlen = strlen(cmd);
	if (cmdlen == 0) { // ":123<enter>" - goto line #123
		if (e >= 0) {
			dot = find_line(e); // what line is #e
			dot_skip_over_ws();
		}
	} else if (cmd[0] == '=' && !cmd[1]) { // where is the address
		if (!GOT_ADDRESS) {            // no addr given- use defaults
			e = count_lines(text, dot);
		}
		status_line("%d", e);
	} else if (cmd[0] == 'n' && cmd[1] == 'u' && !cmd[2]) { // :nu
		vi_setops |= VI_NUMBER;
		last_status_cksum = 0;
		if (editing)
			redraw(TRUE);
	} else if (strcmp(cmd, "nonu") == 0) { // :nonu
		vi_setops &= ~VI_NUMBER;
		last_status_cksum = 0;
		if (editing)
			redraw(TRUE);
	} else if (strcmp(cmd, "rnu") == 0) { // :rnu
		vi_setops |= VI_RELNUMBER;
		last_status_cksum = 0;
		if (editing)
			redraw(TRUE);
	} else if (strcmp(cmd, "nornu") == 0) { // :nornu
		vi_setops &= ~VI_RELNUMBER;
		last_status_cksum = 0;
		if (editing)
			redraw(TRUE);
	} else if (strncmp(cmd, "delete", cmdlen) == 0) { // delete lines
		if (!GOT_ADDRESS) {                       // no addr given- use defaults
			q = begin_line(dot);              // assume .,. for the range
			r = end_line(dot);
		}
		dot = yank_delete(q, r, WHOLE, YANKDEL,
		                  ALLOW_UNDO); // save, then delete lines
		dot_skip_over_ws();
	} else if (strncmp(cmd, "edit", cmdlen) == 0) { // Edit a file
		int size;
		char *fn;

		// don't edit, if the current file has been modified
		if (modified_count && !useforce) {
			status_line_bold("No write since last change (:%s! overrides)", cmd);
			goto ret;
		}
		fn = current_filename;
		if (args[0]) {
			// the user supplied a file name
			fn = expand_args(args);
			if (fn == NULL)
				goto ret;
		} else if (current_filename == NULL) {
			// no user file name, no current name- punt
			status_line_bold("No current filename");
			goto ret;
		}

		size = init_text_buffer(fn);

		if (Ureg >= 0 && Ureg < 28) {
			free(reg[Ureg]); //   free orig line reg- for 'U'
			reg[Ureg] = NULL;
		}
		/*if (YDreg < 28) - always true*/ {
			free(reg[YDreg]); //   free default yank/delete register
			reg[YDreg] = NULL;
		}
		status_line("'%s'%s" IF_FEATURE_VI_READONLY("%s") " %uL, %uC", fn,
		            (size < 0 ? " [New file]" : ""),
		            IF_FEATURE_VI_READONLY(((readonly_mode) ? " [Readonly]" : ""), )
		                count_lines(text, end - 1),
		            (int)(end - text));
	} else if (strncmp(cmd, "file", cmdlen) == 0) { // what File is this
		if (e >= 0) {
			status_line_bold("No address allowed on this command");
			goto ret;
		}
		if (args[0]) {
			// user wants a new filename
			exp = expand_args(args);
			if (exp == NULL)
				goto ret;
			update_filename(exp);
		} else {
			// user wants file status info
			last_status_cksum = 0; // force status update
		}
	} else if (strncmp(cmd, "features", cmdlen) ==
	           0) { // what features are available
		// print out values of all features
		go_bottom_and_clear_to_eol();
		cookmode();
		show_help();
		rawmode();
		Hit_Return();
	} else if (strncmp(cmd, "list", cmdlen) == 0) { // literal print line
		char *dst;
		if (!GOT_ADDRESS) {          // no addr given- use defaults
			q = begin_line(dot); // assume .,. for the range
			r = end_line(dot);
		}
		have_status_msg = 1;
		dst = status_buffer;
#define MAXPRINT (sizeof(ESC_BOLD_TEXT "^?" ESC_NORM_TEXT) + 1)
		while (q <= r && dst < status_buffer + STATUS_BUFFER_LEN - MAXPRINT) {
			char c;
			int c_is_no_print;

			c = *q++;
			if (c == '\n') {
				*dst++ = '$';
				break;
			}
			c_is_no_print = (c & 0x80) && !Isprint(c);
			if (c_is_no_print) {
				// TODO: print fewer ESC if more than one ctrl char
				dst = stpcpy(dst, ESC_BOLD_TEXT);
				*dst++ = '.';
				dst = stpcpy(dst, ESC_NORM_TEXT);
				continue;
			}
			if (c < ' ' || c == 127) {
				*dst++ = '^';
				if (c == 127)
					c = '?';
				else
					c += '@';
			}
			*dst++ = c;
		}
		*dst = '\0';
	} else if (strncmp(cmd, "quit", cmdlen) == 0    // quit
	           || strncmp(cmd, "next", cmdlen) == 0 // edit next file
	           || strncmp(cmd, "prev", cmdlen) == 0 // edit previous file
	) {
		int n;
		if (useforce) {
			if (*cmd == 'q') {
				// force end of argv list
				optind = cmdline_filecnt;
			}
			editing = 0;
			goto ret;
		}
		// don't exit if the file been modified
		if (modified_count) {
			status_line_bold("No write since last change (:%s! overrides)", cmd);
			goto ret;
		}
		// are there other file to edit
		n = cmdline_filecnt - optind - 1;
		if (*cmd == 'q' && n > 0) {
			status_line_bold("%u more file(s) to edit", n);
			goto ret;
		}
		if (*cmd == 'n' && n <= 0) {
			status_line_bold("No more files to edit");
			goto ret;
		}
		if (*cmd == 'p') {
			// are there previous files to edit
			if (optind < 1) {
				status_line_bold("No previous files to edit");
				goto ret;
			}
			optind -= 2;
		}
		editing = 0;
	} else if (strncmp(cmd, "read", cmdlen) == 0) { // read file into text[]
		int size, num;
		char *fn = current_filename;

		if (args[0]) {
			// the user supplied a file name
			fn = expand_args(args);
			if (fn == NULL)
				goto ret;
			init_filename(fn);
		} else if (current_filename == NULL) {
			// no user file name, no current name- punt
			status_line_bold("No current filename");
			goto ret;
		}
		if (e == 0) { // user said ":0r foo"
			q = text;
		} else { // read after given line or current line if none given
			q = next_line(GOT_ADDRESS ? find_line(e) : dot);
			// read after last line
			if (q == end - 1)
				++q;
		}
		num = count_lines(text, q);
		if (q == end)
			num++;
		{ // dance around potentially-reallocated text[]
			uintptr_t ofs = q - text;
			size = file_insert(fn, q, 0);
			q = text + ofs;
		}
		if (size < 0)
			goto ret; // nothing was inserted
		status_line("'%s'" IF_FEATURE_VI_READONLY("%s") " %uL, %uC", fn,
		            IF_FEATURE_VI_READONLY((readonly_mode ? " [Readonly]" : ""), )
		                count_lines(q, q + size - 1),
		            size);
		dot = find_line(num);
	} else if (strncmp(cmd, "rewind", cmdlen) == 0) { // rewind cmd line args
		if (modified_count && !useforce) {
			status_line_bold("No write since last change (:%s! overrides)", cmd);
		} else {
			// reset the filenames to edit
			optind = -1; // start from 0th file
			editing = 0;
		}
	} else if (strncmp(cmd, "set", cmdlen) == 0 // set or clear features
	           IF_FEATURE_VI_SEARCH(&&cmdlen >
	                                1) // (do not confuse with "s /find/repl/")
	) {
		char *argp, *argn, oldch;
		if (!args[0] || strcmp(args, "all") == 0) {
			// print out values of all options
			status_line_bold("%sautoindent "
			                 "%sexpandtab "
			                 "%sflash "
			                 "%signorecase "
			                 "%sshowmatch "
			                 "%snumber "
			                 "%srelativenumber "
			                 "%sextendedre "
			                 "tabstop=%u",
			                 autoindent ? "" : "no", expandtab ? "" : "no",
			                 err_method ? "" : "no", ignorecase ? "" : "no",
			                 showmatch ? "" : "no", number ? "" : "no",
			                 relativenumber ? "" : "no", ere ? "" : "no", tabstop);
			goto ret;
		}
		argp = args;
		while (*argp) {
			int i = 0;
			if (argp[0] == 'n' && argp[1] == 'o') // "noXXX"
				i = 2;
			argn = skip_non_whitespace(argp);
			oldch = *argn;
			*argn = '\0';
			setops(argp, i);
			*argn = oldch;
			argp = skip_whitespace(argn);
		}
		last_status_cksum = 0;
		if (editing)
			redraw(TRUE);

	} else if (cmd[0] == 's') { // substitute a pattern with a replacement pattern
		char c;
		char *F, *R, *flags;
		size_t len_F, len_R;
		int i;
		int gflag = 0; // global replace flag
		int subs = 0;  // number of substitutions
		int last_line = 0, lines = 0;
		regex_t preg;
		int cflags;
		char *Rorig;
		int undo = 0;
		buf = skip_whitespace(buf + 1); // spaces allowed: "s  /find/repl/"
		// F points to the "find" pattern
		// R points to the "replace" pattern
		// replace the cmd line delimiters "/" with NULs
		c = buf[0];                 // what is the delimiter
		F = buf + 1;                // start of "find"
		R = strchr_backslash(F, c); // middle delimiter
		if (!R)
			goto colon_s_fail;
		len_F = R - F;
		*R++ = '\0'; // terminate "find"
		flags = strchr_backslash(R, c);
		if (flags) {
			*flags++ = '\0'; // terminate "replace"
			gflag = *flags;
		}

		if (len_F) { // save "find" as last search pattern
			free(last_search_pattern);
			last_search_pattern = xstrdup(F - 1);
			last_search_pattern[0] = '/';
		} else if (last_search_pattern[1] == '\0') {
			status_line_bold("No previous search");
			goto ret;
		} else {
			F = last_search_pattern + 1;
			len_F = strlen(F);
		}

		if (!GOT_ADDRESS) {          // no addr given
			q = begin_line(dot); // start with cur line
			r = end_line(dot);
			b = e = count_lines(text, q); // cur line number
		} else if (!GOT_RANGE) {              // one addr given
			b = e;
		}

		Rorig = R;
		cflags = 0;
		if (ignorecase)
			cflags |= REG_ICASE;
		if (ere)
			cflags |= REG_EXTENDED;
		memset(&preg, 0, sizeof(preg));
		if (regcomp(&preg, F, cflags) != 0) {
			status_line(":s bad search pattern");
			goto regex_search_end;
		}
		for (i = b; i <= e; i++) { // so, :20,23 s \0 find \0 replace \0
			char *ls = q;      // orig line start
			char *found;
		vc4:
			found = regex_search(q, &preg, Rorig, &len_F, &len_R, &R);
			if (found) {
				uintptr_t bias;
				// we found the "find" pattern - delete it
				// For undo support, the first item should not be chained
				// This needs to be handled differently depending on
				// whether or not regex support is enabled.
#define TEST_LEN_F len_F // len_F may be zero
#define TEST_UNDO1 undo++
#define TEST_UNDO2 undo++
				if (TEST_LEN_F) // match can be empty, no delete needed
					text_hole_delete(found, found + len_F - 1,
					                 TEST_UNDO1 ? ALLOW_UNDO_CHAIN : ALLOW_UNDO);
				if (len_R != 0) { // insert the "replace" pattern, if required
					bias = string_insert(found, R,
					                     TEST_UNDO2 ? ALLOW_UNDO_CHAIN : ALLOW_UNDO);
					found += bias;
					ls += bias;
					// q += bias; - recalculated anyway
				}
				free(R);
				if (TEST_LEN_F || len_R != 0) {
					dot = ls;
					subs++;
					if (last_line != i) {
						last_line = i;
						++lines;
					}
				}
				// check for "global"  :s/foo/bar/g
				if (gflag == 'g') {
					if ((found + len_R) < end_line(ls)) {
						q = found + len_R;
						goto vc4; // don't let q move past cur line
					}
				}
			}
			q = next_line(ls);
		}
		if (subs == 0) {
			status_line_bold("No match");
		} else {
			dot_skip_over_ws();
			if (subs > 1)
				status_line("%d substitutions on %d lines", subs, lines);
		}
	regex_search_end:
		regfree(&preg);
	} else if (strncmp(cmd, "version", cmdlen) == 0) { // show software version
		status_line(BB_VER);
	} else if (strncmp(cmd, "write", cmdlen) == 0 // write text to file
	           || strcmp(cmd, "wq") == 0 || strcmp(cmd, "wn") == 0 ||
	           (cmd[0] == 'x' && !cmd[1])) {
		int size, l;
		// int forced = FALSE;
		char *fn = current_filename;

		// is there a file name to write to?
		if (args[0]) {
			struct stat statbuf;

			exp = expand_args(args);
			if (exp == NULL)
				goto ret;
			if (!useforce && (fn == NULL || strcmp(fn, exp) != 0) &&
			    stat(exp, &statbuf) == 0) {
				status_line_bold("File exists (:w! overrides)");
				goto ret;
			}
			fn = exp;
			init_filename(fn);
		} else if (readonly_mode && !useforce && fn) {
			status_line_bold("'%s' is read only", fn);
			goto ret;
		}
		// if (useforce) {
		//  if "fn" is not write-able, chmod u+w
		//  sprintf(syscmd, "chmod u+w %s", fn);
		//  system(syscmd);
		//  forced = TRUE;
		//}
		size = l = 0;
		if (modified_count != 0 || cmd[0] != 'x') {
			size = r - q + 1;
			l = file_write(fn, q, r);
		}
		// if (useforce && forced) {
		//  chmod u-w
		//  sprintf(syscmd, "chmod u-w %s", fn);
		//  system(syscmd);
		//  forced = FALSE;
		//}
		if (l < 0) {
			if (l == -1)
				status_line_bold_errno(fn);
		} else {
			// how many lines written
			int lines = count_lines(q, q + l - 1);
			status_line("'%s' %uL, %uC", fn, lines, l);
			if (l == size) {
				if (q == text && q + l == end) {
					modified_count = 0;
					last_modified_count = -1;
				}
				if (cmd[1] == 'n') {
					editing = 0;
				} else if (cmd[0] == 'x' || cmd[1] == 'q') {
					// are there other files to edit?
					int n = cmdline_filecnt - optind - 1;
					if (n > 0) {
						if (!useforce) {
							status_line_bold("%u more file(s) to edit", n);
							goto ret;
						}
						// force end of argv list
						optind = cmdline_filecnt;
					}
					editing = 0;
				}
			}
		}
	} else if (strncmp(cmd, "yank", cmdlen) == 0) { // yank lines
		int lines;
		if (!GOT_ADDRESS) {          // no addr given- use defaults
			q = begin_line(dot); // assume .,. for the range
			r = end_line(dot);
		}
		text_yank(q, r, YDreg, WHOLE);
		lines = count_lines(q, r);
		status_line("Yank %d lines (%d chars) into [%c]", lines, strlen(reg[YDreg]),
		            what_reg());
	} else {
		// cmd unknown
		not_implemented(cmd);
	}
ret:
	IF_FEATURE_VI_COLON_EXPAND(free(exp);)
	dot = bound_dot(dot); // make sure "dot" is valid
	return;
colon_s_fail:
	status_line(":s expression missing delimiters");
}

//----- Char Routines --------------------------------------------
// Chars that are part of a word-
//    0123456789_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz
// Chars that are Not part of a word (stoppers)
//    !"#$%&'()*+,-./:;<=>?@[\]^`{|}~
// Chars that are WhiteSpace
//    TAB NEWLINE VT FF RETURN SPACE
// DO NOT COUNT NEWLINE AS WHITESPACE

static int
st_test(char *p, int type, int dir, char *tested)
{
	wchar_t wc0 = 0, wci = 0, wc;
	char *pi;
	int test = 0;

	if (!p || p < text || p > end - 1)
		return 0;

	utf8_decode_at(p, &wc0);
	pi = (dir >= 0) ? utf8_next_cp_anyline(p) : utf8_prev_cp_anyline(p);
	if (pi < text)
		pi = text;
	if (pi > end - 1)
		pi = end - 1;
	utf8_decode_at(pi, &wci);

	wc = wc0;
	if (type == S_BEFORE_WS || type == S_END_PUNCT || type == S_END_ALNUM)
		wc = wci;

	if (type == S_BEFORE_WS) {
		// don't count newline as whitespace for stop test
		test = !(iswspace(wc) && wc != L'\n');
	}
	if (type == S_TO_WS) {
		// don't count newline as whitespace for stop test
		test = !(iswspace(wc) && wc != L'\n');
	}
	if (type == S_OVER_WS) {
		// here newline is treated as whitespace
		test = iswspace(wc);
	}
	if (type == S_END_PUNCT) {
		test = iswpunct(wc);
	}
	if (type == S_END_ALNUM) {
		test = (iswalnum(wc) || wc == L'_');
	}
	// st_test() historically returns the tested byte.
	*tested = (char)((wc == 0 || wc > 0x7f) ? '?' : (char)wc);
	return test;
}

static char *
skip_thing(char *p, int linecnt, int dir, int type)
{
	char c;

	while (st_test(p, type, dir, &c)) {
		// make sure we limit search to correct number of lines
		if (c == '\n' && --linecnt < 1)
			break;
		if (dir >= 0 && p >= end - 1)
			break;
		if (dir < 0 && p <= text)
			break;
		{
			char *np = (dir >= 0) ? utf8_next_cp_anyline(p) : utf8_prev_cp_anyline(p);
			if (np == p)
				break;
			p = np;
		}
		// avoid infinite loops at buffer edges
		if (dir >= 0 && p >= end - 1)
			break;
		if (dir < 0 && p <= text)
			break;
	}
	return p;
}

static void
winch_handler(int sig UNUSED_PARAM)
{
	int save_errno = errno;
	signal(SIGWINCH, winch_handler);
	winch_pending = 1;
	errno = save_errno;
}

static void
tstp_handler(int sig UNUSED_PARAM)
{
	int save_errno = errno;

	// ioctl inside cookmode() was seen to generate SIGTTOU,
	// stopping us too early. Prevent that:
	signal(SIGTTOU, SIG_IGN);

	// Make the shell screen "clean" while suspended: leave alt screen.
	write1(ESC_NORM_TEXT);
	go_bottom_and_clear_to_eol();
	write1(ESC "[?1049l");
	cookmode(); // terminal to "cooked"

	// stop now
	// signal(SIGTSTP, SIG_DFL);
	// raise(SIGTSTP);
	raise(SIGSTOP); // avoid "dance" with TSTP handler - use SIGSTOP instead
	// signal(SIGTSTP, tstp_handler);

	// we have been "continued" with SIGCONT, restore screen and termios
	write1(ESC "[?1049h");
	rawmode();              // terminal to "raw"
	last_status_cksum = 0;  // force status update
	last_cursor_shape = -1; // force cursor-shape update
	redraw(TRUE);           // re-draw the screen
	update_cursor_shape();

	errno = save_errno;
}
static void
int_handler(int sig)
{
	signal(SIGINT, int_handler);
	siglongjmp(restart, sig);
}

static void do_cmd(int c);

static int
at_eof(const char *s)
{
	// does 's' point to end of file, even with no terminating newline?
	return ((s == end - 2 && s[1] == '\n') || s == end - 1);
}

static int
textobj_delim_pair(int key, char *open_out, char *close_out)
{
	switch (key) {
	case '(':
	case ')':
		*open_out = '(';
		*close_out = ')';
		return 1;
	case '[':
	case ']':
		*open_out = '[';
		*close_out = ']';
		return 1;
	case '{':
	case '}':
		*open_out = '{';
		*close_out = '}';
		return 1;
	}
	return 0;
}

// Find smallest surrounding open..close pair that encloses dot.
// Returns 1 on success and sets *openp/*closep to the delimiters.
static int
textobj_find_surrounding_pair(char open, char close, char **openp,
                              char **closep)
{
	char *scan = dot;
	char *op;
	char *cp;

	if (!text || !end || !dot)
		return 0;
	if (scan < text)
		scan = text;
	if (scan > end - 1)
		scan = end - 1;

	for (;;) {
		int depth = 0;
		op = NULL;
		for (char *p = scan;; p--) {
			if (*p == close) {
				depth++;
				continue;
			}
			if (*p == open) {
				if (depth == 0) {
					op = p;
					break;
				}
				depth--;
			}
			if (p == text)
				break;
		}
		if (!op)
			return 0;

		depth = 0;
		cp = NULL;
		for (char *p = op + 1; p < end; p++) {
			if (*p == open) {
				depth++;
				continue;
			}
			if (*p == close) {
				if (depth == 0) {
					cp = p;
					break;
				}
				depth--;
			}
		}
		if (!cp)
			return 0;

		// Does this pair enclose dot?
		if (cp >= dot)
			break;

		// Otherwise, try an earlier opening delimiter.
		if (op == text)
			return 0;
		scan = op - 1;
	}

	*openp = op;
	*closep = cp;
	return 1;
}

static int
textobj_word_range(int inner, int bigword, char **startp,
                   char **stopp)
{
	char *walker;
	char *bol;
	char *eol;
	char *start = NULL;
	char *stop = NULL;
	int count = (cmdcnt ? cmdcnt : 1);
	int i;

	walker = dot;
	if (!walker || walker < text || walker > end - 1)
		return 0;
	if (*walker == '\n')
		return 0;
	bol = begin_line(walker);
	eol = end_line(walker);

	for (i = 0; i < count; i++) {
		int mode; // 0=word, 1=punct, 2=other/big
		char *p;
		char *q;
		wchar_t wc;

		// Skip whitespace to next token (but do not cross newline).
		while (walker < eol) {
			wc = wc_at(walker);
			if (wc == L'\n')
				break;
			if (!is_space_wc(wc))
				break;
			walker = step_cp(walker, +1);
			if (walker == eol)
				break;
		}
		if (walker >= eol)
			return 0;
		wc = wc_at(walker);
		if (wc == L'\n')
			return 0;

		if (bigword) {
			mode = 2;
		} else if (is_word_wc(wc)) {
			mode = 0;
		} else if (is_punct_wc(wc)) {
			mode = 1;
		} else {
			mode = 2;
		}

		p = walker;
		while (p > bol) {
			char *prev = step_cp(p, -1);
			wchar_t pwc;
			if (prev == p)
				break;
			pwc = wc_at(prev);
			if (pwc == L'\n')
				break;
			if (mode == 0) {
				if (!is_word_wc(pwc))
					break;
			} else if (mode == 1) {
				if (!is_punct_wc(pwc))
					break;
			} else {
				if (is_space_wc(pwc) || pwc == L'\n')
					break;
			}
			p = prev;
		}

		q = walker;
		while (q < eol) {
			char *next = step_cp(q, +1);
			wchar_t nwc;
			if (next == q)
				break;
			if (next >= eol)
				break;
			nwc = wc_at(next);
			if (nwc == L'\n')
				break;
			if (mode == 0) {
				if (!is_word_wc(nwc))
					break;
			} else if (mode == 1) {
				if (!is_punct_wc(nwc))
					break;
			} else {
				if (is_space_wc(nwc) || nwc == L'\n')
					break;
			}
			q = next;
		}

		if (!start)
			start = p;
		stop = q;
		walker = step_cp(q, +1);
		if (walker >= eol)
			break;
	}

	if (!start || !stop)
		return 0;

	if (!inner) {
		// Include adjacent whitespace (prefer trailing), but never include newline.
		char *after = step_cp(stop, +1);
		wchar_t awc = wc_at(after);
		if (after != stop && after < eol && awc != L'\n' && is_space_wc(awc)) {
			stop = after;
			while (1) {
				char *n = step_cp(stop, +1);
				wchar_t nw = wc_at(n);
				if (n == stop || n >= eol)
					break;
				if (nw == L'\n' || !is_space_wc(nw))
					break;
				stop = n;
			}
		} else {
			char *before = step_cp(start, -1);
			wchar_t bwc = wc_at(before);
			if (before != start && before >= bol && bwc != L'\n' &&
			    is_space_wc(bwc)) {
				start = before;
				while (1) {
					char *n = step_cp(start, -1);
					wchar_t nw = wc_at(n);
					if (n == start || n < bol)
						break;
					if (nw == L'\n' || !is_space_wc(nw))
						break;
					start = n;
				}
			}
		}
	}

	*startp = start;
	*stopp = utf8_cp_end_byte(stop);
	return 1;
}

static int
find_range(char **start, char **stop, int cmd)
{
	char *p, *q, *t;
	int buftype = -1;
	int c;

	p = q = dot;

	if (cmd == 'Y') {
		c = 'y';
	} else {
		c = get_motion_char();
	}

	if (c == 'i' || c == 'a') {
		// Minimal text objects: i(/a(, i[/a[, i{/a{ (also accept closing
		// delimiters)
		int inner = (c == 'i');
		int obj = get_one_char();
		if (obj == 'w' || obj == 'W') {
			int bigword = (obj == 'W');
			if (!textobj_word_range(inner, bigword, &p, &q)) {
				indicate_error();
				return -1;
			}
			buftype = PARTIAL;
			*start = p;
			*stop = q;
			return buftype;
		}
		char open, close;
		char *op, *cp;
		if (!textobj_delim_pair(obj, &open, &close)) {
			indicate_error();
			return -1;
		}
		if (!textobj_find_surrounding_pair(open, close, &op, &cp)) {
			indicate_error();
			return -1;
		}
		p = inner ? (op + 1) : op;
		q = inner ? (cp - 1) : cp;
		if (p <= q && memchr(p, '\n', (size_t)(q - p + 1)) != NULL)
			buftype = MULTI;
		else
			buftype = PARTIAL;
		*start = p;
		*stop = q;
		return buftype;
	}

	if ((cmd == 'Y' || cmd == c) && strchr("cdy><", c)) {
		// these cmds operate on whole lines
		buftype = WHOLE;
		if (--cmdcnt > 0) {
			do_cmd('j');
			if (cmd_error)
				buftype = -1;
		}
	} else if (strchr("^%$0bBeEfFtThnN/?|{}\b\177", c)) {
		// Most operate on char positions within a line.  Of those that
		// don't '%' needs no special treatment, search commands are
		// marked as MULTI and  "{}" are handled below.
		buftype = strchr("nN/?", c) ? MULTI : PARTIAL;
		do_cmd(c);    // execute movement cmd
		if (p == dot) // no movement is an error
			buftype = -1;
	} else if (strchr("wW", c)) {
		buftype = MULTI;
		do_cmd(c); // execute movement cmd
		// step back one char, but not if we're at end of file,
		// or if we are at EOF and search was for 'w' and we're at
		// the start of a 'W' word.
		if (dot > p && (!at_eof(dot) || (c == 'w' && is_punct_wc(wc_at(dot))))) {
			char *prev = step_cp(dot, -1);
			if (prev < dot)
				dot = utf8_cp_end_byte(prev);
		}
		t = dot;
		// don't include trailing WS as part of word
		while (dot > p && isspace(*dot)) {
			if (*dot-- == '\n')
				t = dot;
		}
		// for non-change operations WS after NL is not part of word
		if (cmd != 'c' && dot != t && *dot != '\n')
			dot = t;
	} else if (strchr("GHL+-gjk'\r\n", c)) {
		// these operate on whole lines
		buftype = WHOLE;
		do_cmd(c); // execute movement cmd
		if (cmd_error)
			buftype = -1;
	} else if (c == ' ' || c == 'l') {
		// forward motion by character
		int tmpcnt = (cmdcnt ? cmdcnt : 1);
		buftype = PARTIAL;
		do_cmd(c); // execute movement cmd
		// exclude last char unless range isn't what we expected
		// this indicates we've hit EOL
		if (tmpcnt == dot - p)
			dot--;
	}

	if (buftype == -1) {
		if (c != 27)
			indicate_error();
		return buftype;
	}

	q = dot;
	if (q < p) {
		t = q;
		q = p;
		p = t;
	}

	// movements which don't include end of range
	if (q > p) {
		if (strchr("^0bBFThnN/?|\b\177", c)) {
			q--;
		} else if (strchr("{}", c)) {
			buftype =
			    (p == begin_line(p) && (*q == '\n' || at_eof(q))) ? WHOLE : MULTI;
			if (!at_eof(q)) {
				q--;
				if (q > p && p != begin_line(p))
					q--;
			}
		}
	}

	*start = p;
	*stop = q;
	return buftype;
}

//---------------------------------------------------------------------
//----- the Ascii Chart -----------------------------------------------
//  00 nul   01 soh   02 stx   03 etx   04 eot   05 enq   06 ack   07 bel
//  08 bs    09 ht    0a nl    0b vt    0c np    0d cr    0e so    0f si
//  10 dle   11 dc1   12 dc2   13 dc3   14 dc4   15 nak   16 syn   17 etb
//  18 can   19 em    1a sub   1b esc   1c fs    1d gs    1e rs    1f us
//  20 sp    21 !     22 "     23 #     24 $     25 %     26 &     27 '
//  28 (     29 )     2a *     2b +     2c ,     2d -     2e .     2f /
//  30 0     31 1     32 2     33 3     34 4     35 5     36 6     37 7
//  38 8     39 9     3a :     3b ;     3c <     3d =     3e >     3f ?
//  40 @     41 A     42 B     43 C     44 D     45 E     46 F     47 G
//  48 H     49 I     4a J     4b K     4c L     4d M     4e N     4f O
//  50 P     51 Q     52 R     53 S     54 T     55 U     56 V     57 W
//  58 X     59 Y     5a Z     5b [     5c \     5d ]     5e ^     5f _
//  60 `     61 a     62 b     63 c     64 d     65 e     66 f     67 g
//  68 h     69 i     6a j     6b k     6c l     6d m     6e n     6f o
//  70 p     71 q     72 r     73 s     74 t     75 u     76 v     77 w
//  78 x     79 y     7a z     7b {     7c |     7d }     7e ~     7f del
//---------------------------------------------------------------------

//----- Execute a Vi Command -----------------------------------
static void
do_cmd(int c)
{
	char *p, *q, *save_dot;
	char buf[12];
	int dir;
	int cnt, i, j;
	int c1;
	char *orig_dot = dot;
	int allow_undo = ALLOW_UNDO;
	int undo_del = UNDO_DEL;

	//	c1 = c; // quiet the compiler
	//	cnt = yf = 0; // quiet the compiler
	//	p = q = save_dot = buf; // quiet the compiler
	memset(buf, '\0', sizeof(buf));
	keep_index = FALSE;
	cmd_error = FALSE;

	show_status_line();

	// if this is a cursor key, skip these checks
	switch (c) {
	case KEYCODE_UP:
	case KEYCODE_DOWN:
	case KEYCODE_LEFT:
	case KEYCODE_RIGHT:
	case KEYCODE_HOME:
	case KEYCODE_END:
	case KEYCODE_PAGEUP:
	case KEYCODE_PAGEDOWN:
	case KEYCODE_DELETE:
		goto key_cmd_mode;
	}

	if (cmd_mode == 2) {
		//  flip-flop Insert/Replace mode
		if (c == KEYCODE_INSERT)
			goto dc_i;
		// we are 'R'eplacing the current *dot with new char
		if (*dot == '\n') {
			// don't Replace past E-o-l
			cmd_mode = 1; // convert to insert
			undo_queue_commit();
		} else {
			if (1 <= c || Isprint(c)) {
				if (c != 27 && !isbackspace(c)) {
					if (*dot != '\n') {
						char *n = utf8_next_cp(dot);
						if (n > dot)
							dot = yank_delete(dot, n - 1, PARTIAL, YANKDEL, ALLOW_UNDO);
					} else {
						dot = yank_delete(dot, dot, PARTIAL, YANKDEL, ALLOW_UNDO);
					}
				}
				dot = char_insert(dot, c, ALLOW_UNDO_CHAIN);
			}
			goto dc1;
		}
	}
	if (cmd_mode == 1) {
		// hitting "Insert" twice means "R" replace mode
		if (c == KEYCODE_INSERT)
			goto dc5;
		// insert the char c at "dot"
		if (1 <= c || Isprint(c)) {
			dot = char_insert(dot, c, ALLOW_UNDO_QUEUED);
		}
		goto dc1;
	}

key_cmd_mode:
	switch (c) {
		// case 0x01:	// soh
		// case 0x09:	// ht
		// case 0x0b:	// vt
		// case 0x0e:	// so
		// case 0x0f:	// si
		// case 0x10:	// dle
		// case 0x11:	// dc1
		// case 0x13:	// dc3
		// case 0x16:	// syn
		// case 0x17:	// etb
		// case 0x18:	// can
		// case 0x1c:	// fs
		// case 0x1d:	// gs
		// case 0x1e:	// rs
		// case 0x1f:	// us
		// case '!':	// !-
		// case '#':	// #-
		// case '&':	// &-
		// case '(':	// (-
		// case ')':	// )-
		// case '*':	// *-
		// case '=':	// =-
		// case '@':	// @-
		// case 'K':	// K-
		// case 'Q':	// Q-
		// case 'S':	// S-
		// case 'V':	// V-
		// case '[':	// [-
		// case '\\':	// \-
		// case ']':	// ]-
		// case '_':	// _-
		// case '`':	// `-
		// case 'v':	// v-
	default: // unrecognized command
		buf[0] = c;
		buf[1] = '\0';
		not_implemented(buf);
		end_cmd_q(); // stop adding to q
	case 0x00:           // nul- ignore
		break;
	case 2:              // ctrl-B  scroll up   full screen
	case KEYCODE_PAGEUP: // Cursor Key Page Up
		dot_scroll(rows - 2, -1);
		break;
	case 4: // ctrl-D  scroll down half screen
		dot_scroll((rows - 2) / 2, 1);
		break;
	case 5: // ctrl-E  scroll down one line
		dot_scroll(1, 1);
		break;
	case 6:                // ctrl-F  scroll down full screen
	case KEYCODE_PAGEDOWN: // Cursor Key Page Down
		dot_scroll(rows - 2, 1);
		break;
	case 7:                        // ctrl-G  show current status
		last_status_cksum = 0; // force status update
		break;
	case 'h':          // h- move left
	case KEYCODE_LEFT: // cursor key Left
	case 8:            // ctrl-H- move left    (This may be ERASE char)
	case 0x7f:         // DEL- move left   (This may be ERASE char)
		do {
			dot_left();
		} while (--cmdcnt > 0);
		break;
	case 10:           // Newline ^J
	case 'j':          // j- goto next line, same col
	case KEYCODE_DOWN: // cursor key Down
	case 13:           // Carriage Return ^M
	case '+':          // +- goto next line
		q = dot;
		do {
			p = next_line(q);
			if (p == end_line(q)) {
				indicate_error();
				goto dc1;
			}
			q = p;
		} while (--cmdcnt > 0);
		dot = q;
		if (c == 13 || c == '+') {
			dot_skip_over_ws();
		} else {
			// try to stay in saved column
			dot = cindex == C_END ? end_line(dot) : move_to_col(dot, cindex);
			keep_index = TRUE;
		}
		break;
	case 12:              // ctrl-L  force redraw whole screen
	case 18:              // ctrl-R  force redraw
		redraw(TRUE); // this will redraw the entire display
		break;
	case 21: // ctrl-U  scroll up half screen
		dot_scroll((rows - 2) / 2, -1);
		break;
	case 25: // ctrl-Y  scroll up one line
		dot_scroll(1, -1);
		break;
	case 27: // esc
		if (cmd_mode == 0)
			indicate_error();
		cmd_mode = 0; // stop inserting
		undo_queue_commit();
		end_cmd_q();
		last_status_cksum = 0; // force status update
		break;
	case ' ':           // move right
	case 'l':           // move right
	case KEYCODE_RIGHT: // Cursor Key Right
		do {
			dot_right();
		} while (--cmdcnt > 0);
		break;
	case '"':                                   // "- name a register to use for Delete/Yank
		c1 = (get_one_char() | 0x20) - 'a'; // | 0x20 is tolower()
		if ((unsigned)c1 <= 25) {           // a-z?
			YDreg = c1;
		} else {
			indicate_error();
		}
		break;
	case '\'': // '- goto a specific mark
		c1 = (get_one_char() | 0x20);
		if ((unsigned)(c1 - 'a') <= 25) { // a-z?
			c1 = (c1 - 'a');
			// get the b-o-l
			q = mark[c1];
			if (text <= q && q < end) {
				dot = q;
				dot_begin(); // go to B-o-l
				dot_skip_over_ws();
			} else {
				indicate_error();
			}
		} else if (c1 == '\'') {         // goto previous context
			dot = swap_context(dot); // swap current and previous context
			dot_begin();             // go to B-o-l
			dot_skip_over_ws();
			orig_dot = dot; // this doesn't update stored contexts
		} else {
			indicate_error();
		}
		break;
	case 'm': // m- Mark a line
		// this is really stupid.  If there are any inserts or deletes
		// between text[0] and dot then this mark will not point to the
		// correct location! It could be off by many lines!
		// Well..., at least its quick and dirty.
		c1 = (get_one_char() | 0x20) - 'a';
		if ((unsigned)c1 <= 25) { // a-z?
			// remember the line
			mark[c1] = dot;
		} else {
			indicate_error();
		}
		break;
	case 'P': // P- Put register before
	case 'p': // p- put register after
		p = reg[YDreg];
		if (p == NULL) {
			status_line_bold("Nothing in register %c", what_reg());
			break;
		}
		cnt = 0;
		i = cmdcnt ? cmdcnt : 1;
		// are we putting whole lines or strings
		if (regtype[YDreg] == WHOLE) {
			if (c == 'P') {
				dot_begin(); // putting lines- Put above
			} else /* if ( c == 'p') */ {
				// are we putting after very last line?
				if (end_line(dot) == (end - 1)) {
					dot = end; // force dot to end of text[]
				} else {
					dot_next(); // next line, then put before
				}
			}
		} else {
			if (c == 'p')
				dot_right(); // move to right, can move to NL
			// how far to move cursor if register doesn't have a NL
			if (strchr(p, '\n') == NULL)
				cnt = i * strlen(p) - 1;
		}
		do {
			// dot is adjusted if text[] is reallocated so we don't have to
			string_insert(dot, p, allow_undo); // insert the string
			allow_undo = ALLOW_UNDO_CHAIN;
		} while (--cmdcnt > 0);
		dot += cnt;
		dot_skip_over_ws();
		yank_status("Put", p, i);
		end_cmd_q(); // stop adding to q
		break;
	case 'U': // U- Undo; replace current line with original version
		if (reg[Ureg] != NULL) {
			p = begin_line(dot);
			q = end_line(dot);
			p = text_hole_delete(p, q, ALLOW_UNDO);             // delete cur line
			p += string_insert(p, reg[Ureg], ALLOW_UNDO_CHAIN); // insert orig line
			dot = p;
			dot_skip_over_ws();
			yank_status("Undo", reg[Ureg], 1);
		}
		break;
	case 'u': // u- undo last operation
		undo_pop();
		break;
	case '$':         // $- goto end of line
	case KEYCODE_END: // Cursor Key End
		for (;;) {
			dot = end_line(dot);
			if (--cmdcnt <= 0)
				break;
			dot_next();
		}
		cindex = C_END;
		keep_index = TRUE;
		break;
	case '%': // %- find matching char of pair () [] {}
		for (q = dot; q < end && *q != '\n'; q++) {
			if (strchr("()[]{}", *q) != NULL) {
				// we found half of a pair
				p = find_pair(q, *q);
				if (p == NULL) {
					indicate_error();
				} else {
					dot = p;
				}
				break;
			}
		}
		if (*q == '\n')
			indicate_error();
		break;
	case 'f':                                  // f- forward to a user specified char
	case 'F':                                  // F- backward to a user specified char
	case 't':                                  // t- move to char prior to next x
	case 'T':                                  // T- move to char after previous x
		last_search_char = get_one_char(); // get the search char
		last_search_cmd = c;
		// fall through
	case ';': // ;- look at rest of line for last search char
	case ',': // ,- repeat latest search in opposite direction
		dot_to_char(c != ',' ? last_search_cmd : last_search_cmd ^ 0x20);
		break;
	case '.': // .- repeat the last modifying command
		// Stuff the last_modifying_cmd back into stdin
		// and let it be re-executed.
		if (lmc_len != 0) {
			if (cmdcnt) // update saved count if current count is non-zero
				dotcnt = cmdcnt;
			last_modifying_cmd[lmc_len] = '\0';
			ioq = ioq_start = xasprintf("%u%s", dotcnt, last_modifying_cmd);
		}
		break;
	case 'N': // N- backward search for last pattern
		dir = last_search_pattern[0] == '/' ? BACK : FORWARD;
		goto dc4; // now search for pattern
		break;
	case '?': // ?- backward search for a pattern
	case '/': // /- forward search for a pattern
		buf[0] = c;
		buf[1] = '\0';
		q = get_input_line(buf); // get input line- use "status line"
		if (!q[0])               // user changed mind and erased the "/"-  do nothing
			break;
		if (!q[1]) { // if no pat re-use old pat
			if (last_search_pattern[0])
				last_search_pattern[0] = c;
		} else { // strlen(q) > 1: new pat- save it and find
			free(last_search_pattern);
			last_search_pattern = xstrdup(q);
		}
		// fall through
	case 'n': // n- repeat search for last pattern
		// search rest of text[] starting at next char
		// if search fails "dot" is unchanged
		dir = last_search_pattern[0] == '/' ? FORWARD : BACK;
	dc4:
		if (last_search_pattern[1] == '\0') {
			status_line_bold("No previous search");
			break;
		}
		do {
			q = char_search(dot + dir, last_search_pattern + 1, (dir << 1) | FULL);
			if (q != NULL) {
				dot = q; // good search, update "dot"
			} else {
				// no pattern found between "dot" and top/bottom of file
				// continue from other end of file
				const char *msg;
				q = char_search(dir == FORWARD ? text : end - 1,
				                last_search_pattern + 1, (dir << 1) | FULL);
				if (q != NULL) { // found something
					dot = q; // found new pattern- goto it
					msg = "search hit %s, continuing at %s";
				} else {            // pattern is nowhere in file
					cmdcnt = 0; // force exit from loop
					msg = "Pattern not found";
				}
				if (dir == FORWARD)
					status_line_bold(msg, "BOTTOM", "TOP");
				else
					status_line_bold(msg, "TOP", "BOTTOM");
			}
		} while (--cmdcnt > 0);
		break;
	case '{': // {- move backward paragraph
	case '}': // }- move forward paragraph
		dir = c == '}' ? FORWARD : BACK;
		do {
			int skip = TRUE; // initially skip consecutive empty lines
			while (dir == FORWARD ? dot < end - 1 : dot > text) {
				if (*dot == '\n' && dot[dir] == '\n') {
					if (!skip) {
						if (dir == FORWARD)
							++dot; // move to next blank line
						goto dc2;
					}
				} else {
					skip = FALSE;
				}
				dot += dir;
			}
			goto dc6; // end of file
		dc2:
			continue;
		} while (--cmdcnt > 0);
		break;
	case '0': // 0- goto beginning of line
	case '1': // 1-
	case '2': // 2-
	case '3': // 3-
	case '4': // 4-
	case '5': // 5-
	case '6': // 6-
	case '7': // 7-
	case '8': // 8-
	case '9': // 9-
		if (c == '0' && cmdcnt < 1) {
			dot_begin(); // this was a standalone zero
		} else {
			cmdcnt = cmdcnt * 10 + (c - '0'); // this 0 is part of a number
		}
		break;
	case ':':                        // :- the colon mode commands
		p = get_input_line(":"); // get input line- use "status line"
		colon(p);                // execute the command
		break;
	case '<':                             // <- Left  shift something
	case '>':                             // >- Right shift something
		cnt = count_lines(text, dot); // remember what line we are on
		if (find_range(&p, &q, c) == -1)
			goto dc6;
		i = count_lines(p, q); // # of lines we are shifting
		for (p = begin_line(p); i > 0; i--, p = next_line(p)) {
			if (c == '<') {
				// shift left- remove tab or tabstop spaces
				if (*p == '\t') {
					// shrink buffer 1 char
					text_hole_delete(p, p, allow_undo);
				} else if (*p == ' ') {
					// we should be calculating columns, not just SPACE
					for (j = 0; *p == ' ' && j < tabstop; j++) {
						text_hole_delete(p, p, allow_undo);
						allow_undo = ALLOW_UNDO_CHAIN;
					}
				}
			} else if (/* c == '>' && */ p != end_line(p)) {
				// shift right -- add tab or tabstop spaces on non-empty lines
				char_insert(p, '\t', allow_undo);
			}
			allow_undo = ALLOW_UNDO_CHAIN;
		}
		dot = find_line(cnt); // what line were we on
		dot_skip_over_ws();
		end_cmd_q(); // stop adding to q
		break;
	case 'A': // A- append at e-o-l
		dot_end();
		/* fall through */
	case 'a': // a- append after current char
		if (*dot != '\n')
			dot++;
		goto dc_i;
		break;
	case 'B': // B- back a blank-delimited Word
	case 'E': // E- end of a blank-delimited word
	case 'W': // W- forward a blank-delimited word
		dir = FORWARD;
		if (c == 'B')
			dir = BACK;
		do {
			char *adj = step_cp(dot, dir);
			wchar_t adj_wc = wc_at(adj);
			if (c == 'W' || is_space_wc(adj_wc)) {
				dot = skip_thing(dot, 1, dir, S_TO_WS);
				dot = skip_thing(dot, 2, dir, S_OVER_WS);
			}
			if (c != 'W')
				dot = skip_thing(dot, 1, dir, S_BEFORE_WS);
		} while (--cmdcnt > 0);
		break;
	case 'C': // C- Change to e-o-l
	case 'D': // D- delete to e-o-l
		save_dot = dot;
		dot = dollar_line(dot); // move to before NL
		// copy text into a register and delete
		dot = yank_delete(save_dot, dot, PARTIAL, YANKDEL,
		                  ALLOW_UNDO); // delete to e-o-l
		if (c == 'C')
			goto dc_i; // start inserting
		if (c == 'D')
			end_cmd_q(); // stop adding to q
		break;
	case 'g': // 'gg' goto a line number (vim) (default: very first line)
		c1 = get_one_char();
		if (c1 != 'g') {
			buf[0] = 'g';
			// c1 < 0 if the key was special. Try "g<up-arrow>"
			// TODO: if Unicode?
			buf[1] = (c1 >= 0 ? c1 : '*');
			buf[2] = '\0';
			not_implemented(buf);
			cmd_error = TRUE;
			break;
		}
		if (cmdcnt == 0)
			cmdcnt = 1;
		// fall through
	case 'G':              // G- goto to a line number (default= E-O-F)
		dot = end - 1; // assume E-O-F
		if (cmdcnt > 0) {
			dot = find_line(cmdcnt); // what line is #cmdcnt
		}
		dot_begin();
		dot_skip_over_ws();
		break;
	case 'H': // H- goto top line on screen
		dot = screenbegin;
		if (cmdcnt > (rows - 1)) {
			cmdcnt = (rows - 1);
		}
		while (--cmdcnt > 0) {
			dot_next();
		}
		dot_begin();
		dot_skip_over_ws();
		break;
	case 'I':            // I- insert before first non-blank
		dot_begin(); // 0
		dot_skip_over_ws();
		/* fall through */
	case 'i':            // i- insert before current char
	case KEYCODE_INSERT: // Cursor Key Insert
	dc_i:
		newindent = -1;
		cmd_mode = 1;        // start inserting
		undo_queue_commit(); // commit queue when cmd_mode changes
		break;
	case 'J': // J- join current and next lines together
		do {
			dot_end();           // move to NL
			if (dot < end - 1) { // make sure not last char in text[]
				undo_push(dot, 1, UNDO_DEL);
				*dot++ = ' '; // replace NL with space
				undo_push((dot - 1), 1, UNDO_INS_CHAIN);
				while (isblank(*dot)) { // delete leading WS
					text_hole_delete(dot, dot, ALLOW_UNDO_CHAIN);
				}
			}
		} while (--cmdcnt > 0);
		end_cmd_q(); // stop adding to q
		break;
	case 'L': // L- goto bottom line on screen
		dot = end_screen();
		if (cmdcnt > (rows - 1)) {
			cmdcnt = (rows - 1);
		}
		while (--cmdcnt > 0) {
			dot_prev();
		}
		dot_begin();
		dot_skip_over_ws();
		break;
	case 'M': // M- goto middle line on screen
		dot = screenbegin;
		for (cnt = 0; cnt < (rows - 1) / 2; cnt++)
			dot = next_line(dot);
		dot_skip_over_ws();
		break;
	case 'O': // O- open an empty line above
		dot_begin();
		// special case: use indent of current line
		newindent = get_column(dot + indent_len(dot));
		goto dc3;
	case 'o': // o- open an empty line below
		dot_end();
	dc3:
		cmd_mode = 1; // switch to insert mode early
		dot = char_insert(dot, '\n', ALLOW_UNDO);
		if (c == 'O' && !autoindent) {
			// done in char_insert() for 'O'+autoindent
			dot_prev();
		}
		goto dc_i;
		break;
	case 'R': // R- continuous Replace char
	dc5:
		cmd_mode = 2;
		undo_queue_commit();
		rstart = dot;
		break;
	case KEYCODE_DELETE:
		if (dot < end - 1) {
			if (*dot != '\n') {
				char *n = utf8_next_cp(dot);
				if (n > dot)
					dot = yank_delete(dot, n - 1, PARTIAL, YANKDEL, ALLOW_UNDO);
			} else {
				dot = yank_delete(dot, dot, PARTIAL, YANKDEL, ALLOW_UNDO);
			}
		}
		break;
	case 'X': // X- delete char before dot
	case 'x': // x- delete the current char
	case 's': // s- substitute the current char
		dir = 0;
		if (c == 'X')
			dir = -1;
		do {
			if (c == 'X') {
				if (dot > text && dot[-1] != '\n') {
					char *prev = utf8_prev_cp(begin_line(dot), dot);
					if (prev != dot) {
						dot = yank_delete(prev, dot - 1, PARTIAL, YANKDEL, allow_undo);
						allow_undo = ALLOW_UNDO_CHAIN;
					}
				}
			} else {
				if (*dot != '\n') {
					char *n = utf8_next_cp(dot);
					if (n > dot) {
						dot = yank_delete(dot, n - 1, PARTIAL, YANKDEL, allow_undo);
						allow_undo = ALLOW_UNDO_CHAIN;
					}
				}
			}
		} while (--cmdcnt > 0);
		end_cmd_q(); // stop adding to q
		if (c == 's')
			goto dc_i; // start inserting
		break;
	case 'Z': // Z- if modified, {write}; exit
		c1 = get_one_char();
		// ZQ means to exit without saving
		if (c1 == 'Q') {
			editing = 0;
			optind = cmdline_filecnt;
			break;
		}
		// ZZ means to save file (if necessary), then exit
		if (c1 != 'Z') {
			indicate_error();
			break;
		}
		if (modified_count) {
			if (ENABLE_FEATURE_VI_READONLY && readonly_mode && current_filename) {
				status_line_bold("'%s' is read only", current_filename);
				break;
			}
			cnt = file_write(current_filename, text, end - 1);
			if (cnt < 0) {
				if (cnt == -1)
					status_line_bold("Write error: " STRERROR_FMT STRERROR_ERRNO);
			} else if (cnt == (end - 1 - text + 1)) {
				editing = 0;
			}
		} else {
			editing = 0;
		}
		// are there other files to edit?
		j = cmdline_filecnt - optind - 1;
		if (editing == 0 && j > 0) {
			editing = 1;
			modified_count = 0;
			last_modified_count = -1;
			status_line_bold("%u more file(s) to edit", j);
		}
		break;
	case '^': // ^- move to first non-blank on line
		dot_begin();
		dot_skip_over_ws();
		break;
	case 'b': // b- back a word
	case 'e': // e- end of word
		dir = FORWARD;
		if (c == 'b')
			dir = BACK;
		do {
			char *n = step_cp(dot, dir);
			if (n == dot)
				break;
			dot = n;
			if (is_space_wc(wc_at(dot))) {
				dot = skip_thing(dot, (c == 'e') ? 2 : 1, dir, S_OVER_WS);
			}
			if (is_word_wc(wc_at(dot))) {
				dot = skip_thing(dot, 1, dir, S_END_ALNUM);
			} else if (is_punct_wc(wc_at(dot))) {
				dot = skip_thing(dot, 1, dir, S_END_PUNCT);
			}
		} while (--cmdcnt > 0);
		break;
	case 'c': // c- change something
	case 'd': // d- delete something
	case 'y': // y- yank   something
	case 'Y': // Y- Yank a line
	{
		int yf = YANKDEL; // assume either "c" or "d"
		int buftype;
		char *savereg = reg[YDreg];
		if (c == 'y' || c == 'Y')
			yf = YANKONLY;
		// determine range, and whether it spans lines
		buftype = find_range(&p, &q, c);
		if (buftype == -1) // invalid range
			goto dc6;
		// Empty inner text object (like "()" with "di("):
		// - d/y: no-op
		// - c: enter insert mode at the inner position
		if (p > q) {
			dot = p;
			if (c == 'c')
				goto dc_i;
			goto dc6;
		}
		if (buftype == WHOLE) {
			save_dot = p; // final cursor position is start of range
			p = begin_line(p);
			if (c == 'c') // special case: use indent of current line
				newindent = get_column(p + indent_len(p));
			q = end_line(q);
		}
		dot = yank_delete(p, q, buftype, yf, ALLOW_UNDO); // delete word
		if (buftype == WHOLE) {
			if (c == 'c') {
				cmd_mode = 1; // switch to insert mode early
				dot = char_insert(dot, '\n', ALLOW_UNDO_CHAIN);
				// on the last line of file don't move to prev line,
				// handled in char_insert() if autoindent is enabled
				if (dot != (end - 1) && !autoindent) {
					dot_prev();
				}
			} else if (c == 'd') {
				dot_begin();
				dot_skip_over_ws();
			} else {
				dot = save_dot;
			}
		}
		// if CHANGING, not deleting, start inserting after the delete
		if (c == 'c') {
			goto dc_i; // start inserting
		}
		// only update status if a yank has actually happened
		if (reg[YDreg] != savereg)
			yank_status(c == 'd' ? "Delete" : "Yank", reg[YDreg], 1);
	dc6:
		end_cmd_q(); // stop adding to q
		break;
	}
	case 'k':        // k- goto prev line, same col
	case KEYCODE_UP: // cursor key Up
	case '-':        // -- goto prev line
		q = dot;
		do {
			p = prev_line(q);
			if (p == begin_line(q)) {
				indicate_error();
				goto dc1;
			}
			q = p;
		} while (--cmdcnt > 0);
		dot = q;
		if (c == '-') {
			dot_skip_over_ws();
		} else {
			// try to stay in saved column
			dot = cindex == C_END ? end_line(dot) : move_to_col(dot, cindex);
			keep_index = TRUE;
		}
		break;
	case 'r':                    // r- replace the current char with user input
		c1 = get_one_char(); // get the replacement char
		if (c1 != 27) {
			if (end_line(dot) - dot < (cmdcnt ? cmdcnt : 1)) {
				indicate_error();
				goto dc6;
			}
			do {
				if (*dot != '\n') {
					char *n = utf8_next_cp(dot);
					if (n > dot)
						dot = text_hole_delete(dot, n - 1, allow_undo);
				} else {
					dot = text_hole_delete(dot, dot, allow_undo);
				}
				allow_undo = ALLOW_UNDO_CHAIN;
				dot = char_insert(dot, c1, allow_undo);
			} while (--cmdcnt > 0);
			dot_left();
		}
		end_cmd_q(); // stop adding to q
		break;
	case 'w': // w- forward a word
		do {
			if (is_word_wc(wc_at(dot))) { // we are on ALNUM
				dot = skip_thing(dot, 1, FORWARD, S_END_ALNUM);
			} else if (is_punct_wc(wc_at(dot))) { // we are on PUNCT
				dot = skip_thing(dot, 1, FORWARD, S_END_PUNCT);
			}
			if (dot < end - 1)
				dot = step_cp(dot, +1); // move over word
			if (is_space_wc(wc_at(dot))) {
				dot = skip_thing(dot, 2, FORWARD, S_OVER_WS);
			}
		} while (--cmdcnt > 0);
		break;
	case 'z':                    // z-
		c1 = get_one_char(); // get the replacement char
		cnt = 0;
		if (c1 == '.')
			cnt = (rows - 2) / 2; // put dot at center
		if (c1 == '-')
			cnt = rows - 2;        // put dot at bottom
		screenbegin = begin_line(dot); // start dot at top
		dot_scroll(cnt, -1);
		break;
	case '|':                                   // |- move to column "cmdcnt"
		dot = move_to_col(dot, cmdcnt - 1); // try to move to column
		break;
	case '~': // ~- flip the case of letters   a-z -> A-Z
		do {
			if (isalpha(*dot)) {
				undo_push(dot, 1, undo_del);
				*dot = islower(*dot) ? toupper(*dot) : tolower(*dot);
				undo_push(dot, 1, UNDO_INS_CHAIN);
				undo_del = UNDO_DEL_CHAIN;
			}
			dot_right();
		} while (--cmdcnt > 0);
		end_cmd_q(); // stop adding to q
		break;
		//----- The Cursor and Function Keys -----------------------------
	case KEYCODE_HOME: // Cursor Key Home
		dot_begin();
		break;
		// The Fn keys could point to do_macro which could translate them
	}
dc1:
	// if text[] just became empty, add back an empty line
	if (end == text) {
		char_insert(text, '\n', NO_UNDO); // start empty buf with dummy line
		dot = text;
	}
	// it is OK for dot to exactly equal to end, otherwise check dot validity
	if (dot != end) {
		dot = bound_dot(dot); // make sure "dot" is valid
	}
	if (dot != orig_dot)
		check_context(c); // update the current context

	if (!isdigit(c))
		cmdcnt = 0; // cmd was not a number, reset cmdcnt
	cnt = dot - begin_line(dot);
	// Try to stay off of the Newline
	if (*dot == '\n' && cnt > 0 && cmd_mode == 0)
		dot--;

	update_cursor_shape();
}

static void
run_cmds(char *p)
{
	while (p) {
		char *q = p;
		p = strchr(q, '\n');
		if (p)
			while (*p == '\n')
				*p++ = '\0';
		colon(q);
	}
}

static void
edit_file(char *fn)
{
#define cur_line edit_file__cur_line
	int c;
	int sig;

	editing = 1; // 0 = exit, 1 = one file, 2 = multiple files
	rawmode();
	rows = 24;
	columns = 80;
	IF_FEATURE_VI_ASK_TERMINAL(G.get_rowcol_error =)
	query_screen_dimensions();
	if (G.get_rowcol_error /* TODO? && no input on stdin */) {
		uint64_t k;
		write1(ESC "[999;999H" ESC "[6n");
		fflush_all();
		k = safe_read_key(STDIN_FILENO, readbuffer, /*timeout_ms:*/ 100);
		if ((int32_t)k == KEYCODE_CURSOR_POS) {
			uint32_t rc = (k >> 32);
			columns = (rc & 0x7fff);
			if (columns > MAX_SCR_COLS)
				columns = MAX_SCR_COLS;
			rows = ((rc >> 16) & 0x7fff);
			if (rows > MAX_SCR_ROWS)
				rows = MAX_SCR_ROWS;
		}
	}
	new_screen(rows, columns); // get memory for virtual screen
	init_text_buffer(fn);

	YDreg =
	    26; // default Yank/Delete reg
	        //	Ureg = 27; - const		// hold orig line for "U" cmd
	mark[26] = mark[27] = text; // init "previous context"
	crow = 0;
	ccol = 0;

	signal(SIGWINCH, winch_handler);
	signal(SIGTSTP, tstp_handler);
	sig = sigsetjmp(restart, 1);
	if (sig != 0) {
		screenbegin = dot = text;
	}
	// int_handler() can jump to "restart",
	// must install handler *after* initializing "restart"
	signal(SIGINT, int_handler);

	cmd_mode = 0; // 0=command  1=insert  2='R'eplace
	cmdcnt = 0;
	offset = 0; // no horizontal offset
	c = '\0';
	free(ioq_start);
	ioq_start = NULL;
	adding2q = 0;
	last_cursor_shape = -1;
	update_cursor_shape();

	while (initial_cmds)
		run_cmds((char *)llist_pop(&initial_cmds));
	redraw(FALSE); // dont force every col re-draw
	//------This is the main Vi cmd handling loop -----------------------
	while (editing > 0) {
		if (winch_pending) {
			winch_pending = 0;
			query_screen_dimensions();
			new_screen(rows, columns);
			redraw(TRUE);
		}
		c = get_one_char(); // get a cmd from user
		// save a copy of the current line- for the 'U" command
		if (begin_line(dot) != cur_line) {
			cur_line = begin_line(dot);
			text_yank(begin_line(dot), end_line(dot), Ureg, PARTIAL);
		}
		// If c is a command that changes text[],
		// (re)start remembering the input for the "." command.
		if (!adding2q && ioq_start == NULL && cmd_mode == 0 // command mode
		    && c > '\0'                                     // exclude NUL and non-ASCII chars
		    && c < 0x7f                                     // (Unicode and such)
		    && strchr(modifying_cmds, c)) {
			start_new_cmd_q(c);
		}
		do_cmd(c); // execute the user command

		// poll to see if there is input already waiting. if we are
		// not able to display output fast enough to keep up, skip
		// the display update until we catch up with input.
		if (!readbuffer[0] && mysleep(0) == 0) {
			// no input pending - so update output
			refresh(FALSE);
			show_status_line();
		}
	}
	//-------------------------------------------------------------------

	go_bottom_and_clear_to_eol();
	cookmode();
#undef cur_line
}

#define VI_OPTSTR                  \
	IF_FEATURE_VI_COLON("c:*") \
	"Hh" IF_FEATURE_VI_READONLY("R")

enum {
	IF_FEATURE_VI_COLON(OPTBIT_c, ) OPTBIT_H,
	OPTBIT_h,
	IF_FEATURE_VI_READONLY(OPTBIT_R, ) OPT_c =
	    IF_FEATURE_VI_COLON((1 << OPTBIT_c)) + 0,
	OPT_H = 1 << OPTBIT_H,
	OPT_h = 1 << OPTBIT_h,
	OPT_R = IF_FEATURE_VI_READONLY((1 << OPTBIT_R)) + 0,
};

int
main(int argc, char **argv)
{
	int opts;

	INIT_G();

	// Enable UTF-8 locale behavior in libc for future mbrtowc/wcwidth work.
	// This is safe even if the locale is not UTF-8; it just follows user env.
	setlocale(LC_CTYPE, "");

	// undo_stack_tail = NULL; - already is
	undo_queue_state = UNDO_EMPTY;
	// undo_q = 0; - already is
	// 0: all of our options are disabled by default in vim
	// vi_setops = 0;
	opts = getopt32(argv, VI_OPTSTR IF_FEATURE_VI_COLON(, &initial_cmds));
	if (opts & OPT_R)
		SET_READONLY_MODE(readonly_mode);
	if (opts & OPT_H)
		show_help();
	if (opts & (OPT_H | OPT_h)) {
		bb_show_usage();
		return 1;
	}

	argv += optind;
	cmdline_filecnt = argc - optind;

	//  1-  process EXINIT variable from environment
	//  2-  if EXINIT is unset process $HOME/.exrc file
	//  3-  process command line args
	{
		const char *exinit = getenv("EXINIT");
		char *cmds = NULL;

		if (exinit) {
			cmds = xstrdup(exinit);
		} else {
			const char *home = getenv("HOME");

			if (home && *home) {
				char *exrc = concat_path_file(home, ".exrc");
				struct stat st;

				// .exrc must belong to and only be writable by user
				if (stat(exrc, &st) == 0) {
					if ((st.st_mode & (S_IWGRP | S_IWOTH)) == 0 &&
					    st.st_uid == getuid()) {
						cmds = xmalloc_open_read_close(exrc, NULL);
					} else {
						status_line_bold(".exrc: permission denied");
					}
				}
				free(exrc);
			}
		}

		if (cmds) {
			init_text_buffer(NULL);
			run_cmds(cmds);
			free(cmds);
		}
	}
	// "Save cursor, use alternate screen buffer, clear screen"
	write1(ESC "[?1049h");
	// This is the main file handling loop
	optind = 0;
	while (1) {
		edit_file(argv[optind]); // might be NULL on 1st iteration
		// NB: optind can be changed by ":next" and ":rewind" commands
		optind++;
		if (optind >= cmdline_filecnt)
			break;
	}
	// "Use normal screen buffer, restore cursor"
	write1(ESC "[?1049l");
	write1(ESC_CURSOR_BLOCK);

	return 0;
}
