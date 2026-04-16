#ifndef VI_H
#define VI_H

#include "parser.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* Register/buffer type: how a yanked region is treated on paste. */
enum {
	PARTIAL = 0,
	WHOLE = 1,
	MULTI = 2,
	BLOCK = 3
};

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef ALIGN1
#define ALIGN1 __attribute__((aligned(1)))
#endif

#ifndef UNUSED_PARAM
#define UNUSED_PARAM __attribute__((unused))
#endif

#define KEYCODE_BUFFER_SIZE 32
#define VI_MAX_LINE 4096
#define UNDO_QUEUE_MAX 256

/* Named register indices into reg[]/regtype[]  (a-z = 0-25) */
#define dreg 26       /* default delete/yank register */
#define ureg 27       /* original line for the 'U' undo command */
#define SHARED_REG 28 /* cross-session shared register ("+) */

/* ASCII control characters referenced by name throughout the editor */
#define ASCII_ESC 0x1b    /* ESC — escape / cancel */
#define ASCII_DEL 0x7f    /* DEL — displayed as "^?" */
#define ASCII_CR 0x0d     /* ^M  — carriage return */
#define ASCII_CTRL_B 0x02 /* ^B  — page up */
#define ASCII_CTRL_D 0x04 /* ^D  — scroll down half-page / de-indent */
#define ASCII_CTRL_E 0x05 /* ^E  — scroll down one line */
#define ASCII_CTRL_F 0x06 /* ^F  — page down */
#define ASCII_CTRL_G 0x07 /* ^G  — reprint status line */
#define ASCII_CTRL_H 0x08 /* ^H  — backspace */
#define ASCII_CTRL_J 0x0a /* ^J  — line feed / move down */
#define ASCII_CTRL_L 0x0c /* ^L  — redraw screen */
#define ASCII_CTRL_R 0x12 /* ^R  — redo */
#define ASCII_CTRL_U 0x15 /* ^U  — scroll up half-page */
#define ASCII_CTRL_V 0x16 /* ^V  — insert next char literally */
#define ASCII_CTRL_Y 0x19 /* ^Y  — scroll up one line */

/* Any byte >= UTF8_MULTIBYTE_MIN is a UTF-8 lead or continuation byte */
#define UTF8_MULTIBYTE_MIN 0x80

/* mark[] indices.  mark[0..25] = user marks 'a'..'z' */
#define MARK_CONTEXT 26      /* position before last large motion */
#define MARK_PREV_CONTEXT 27 /* position before previous large motion */
#define MARK_LT 28           /* '< — start of last visual selection */
#define MARK_GT 29           /* '> — end   of last visual selection */

/* Extra bytes per screen row to hold ANSI escape-sequence overhead */
#define SCREEN_LINE_SLACK 32

/* CSI final-byte range (ECMA-48) */
#define CSI_FINAL_BYTE_MIN 0x40 /* '@' */
#define CSI_FINAL_BYTE_MAX 0x7e /* '~' */

/* Bracketed-paste mode CSI parameter values (\e[200~ / \e[201~). */
#define CSI_PASTE_BEGIN_PARAM 200
#define CSI_PASTE_END_PARAM 201

/* Cursor-position response (CPR): row and column are each packed into 15 bits. */
#define CSI_COORD_MASK 0x7fff

#define STATUS_BUFFER_LEN 200

#define VI_AUTOINDENT (1 << 0)
#define VI_EXPANDTAB (1 << 1)
#define VI_ERR_METHOD (1 << 2)
#define VI_IGNORECASE (1 << 3)
#define VI_SHOWMATCH (1 << 4)
#define VI_TABSTOP (1 << 5)
#define VI_CURSORSHAPE (1 << 6)
#define VI_NUMBER (1 << 7)
#define VI_RELATIVENUMBER (1 << 8)
#define VI_UNDOFILE (1 << 9)
#define VI_CURSORSHAPE_INSERT (1 << 10)

#define IS_AUTOINDENT(g) ((g)->setops & VI_AUTOINDENT)
#define IS_EXPANDTAB(g) ((g)->setops & VI_EXPANDTAB)
#define IS_ERR_METHOD(g) ((g)->setops & VI_ERR_METHOD)
#define IS_IGNORECASE(g) ((g)->setops & VI_IGNORECASE)
#define IS_SHOWMATCH(g) ((g)->setops & VI_SHOWMATCH)
#define IS_NUMBER(g) ((g)->setops & VI_NUMBER)
#define IS_RELATIVENUMBER(g) ((g)->setops & VI_RELATIVENUMBER)

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
	"cshp\0"           \
	"cursorshape\0"    \
	"nu\0"             \
	"number\0"         \
	"rnu\0"            \
	"relativenumber\0" \
	"uf\0"             \
	"undofile\0"       \
	"cshpi\0"          \
	"cursorshapeinsert\0"

#define SET_READONLY_FILE(flags) ((flags) |= 0x01)
#define SET_READONLY_MODE(flags) ((flags) |= 0x02)
#define UNSET_READONLY_FILE(flags) ((flags) &= 0xfe)

/* undo_push() operations */
#define UNDO_INS 0
#define UNDO_DEL 1
#define UNDO_INS_CHAIN 2
#define UNDO_DEL_CHAIN 3
#define UNDO_INS_QUEUED 4
#define UNDO_DEL_QUEUED 5
/* In-place byte swap: saves exact original bytes; undo restores with memcpy. */
#define UNDO_SWAP 6

/* Pass-through flags for functions that can be undone */
#define NO_UNDO 0
#define ALLOW_UNDO 1
#define ALLOW_UNDO_CHAIN 2
#define ALLOW_UNDO_QUEUED 3

#define UNDO_USE_SPOS 32
#define UNDO_EMPTY 64

/*
 * cmd_ctx — a fully-parsed Normal-mode command ready for execution.
 *
 * Built from struct parser by cmd_ctx_from_parser() after parse() returns
 * done with ok==1.  No handler reads from user input during execution.
 *
 * Fields:
 *   reg     - register name ('a'-'z', '"', …), or '\0'
 *   count   - operation count; 1 when the user typed no count prefix
 *   op      - primary ASCII operation key, or '\0' when raw_key is set
 *   op2     - second key for two-key ops (gg, zt, ZZ, …), or '\0'
 *   rcount  - range/motion count; 1 when no explicit range count
 *   rg      - range key (w/b/e/d/y/c/…), or '\0'
 *   anchor  - anchor byte (f/t target, text-object char, mark name), or '\0'
 *   str     - NUL-terminated string payload for :, /, ? (points into
 *             parser's b[] buffer — valid until next initparser() call)
 *   raw_key - KEYCODE_* or control char (0x01-0x1F); op is '\0' when set
 */
struct cmd_ctx {
	char reg;
	int count;
	char op;
	char op2;
	int rcount;
	char rg;
	char anchor;
	const char *str;
	int raw_key;
};

struct editor {
	/* --- Buffer (hot path — keep near top) --- */
	char *text, *end; /* heap slab bounds */
	char *dot;        /* current cursor position */
	int text_size;    /* allocated capacity */

	/* --- Mode & command dispatch --- */
	int editing;  /* >0 while editing a file */
	int cmd_mode; /* 0=command  1=insert  2=replace */
	int cmd_error;
	int cmdcnt;               /* repetition count */
	struct parser cmd_parser; /* Normal-mode command parser (MONRAS) */
	int cmdline_filecnt;
	int adding2q;                /* accumulating input into last_modifying_cmd */
	int lmc_len;                 /* length of last_modifying_cmd */
	struct cmd_ctx last_cmd_ctx; /* parser snapshot for '.' repeat */
	int has_last_cmd_ctx;        /* 1 = last_cmd_ctx is valid */
	char *ioq, *ioq_start;       /* replay queue for get_one_char */
	char *rstart;                /* start of text in Replace mode */

	/* --- Visual mode --- */
	int visual_mode; /* 0=off  1=charwise  2=linewise  3=block */
	char *visual_anchor;
	int vis_ai_pending;  /* 0=none; 'a'/'i'=collecting text-object anchor */
	int vis_reg_pending; /* 0=none; 1=saw '"', waiting for register char  */

	/* --- Settings (:set options) --- */
	int setops;
	int readonly_mode;
	int tabstop;
	int cshp; /* cursor shape */

	/* --- Screen & display --- */
	unsigned rows, columns;
	int crow, ccol;    /* cursor row/column on screen */
	int offset;        /* columns scrolled left */
	char *screenbegin; /* top-left of viewport into text[] */
	char *screen;      /* virtual screen buffer */
	int screensize;
	int screen_line_size;
	int have_status_msg; /* must be int — do not shrink */
	int last_status_cksum;
	char *refresh_last_screenbegin;
	int refresh_last_modified_count;
	int refresh_last_highlight_hash; /* hash of highlight_pattern at last render */
	int line_count_cache;
	int line_count_cache_stamp;

	/* --- Modified tracking --- */
	int modified_count;
	int last_modified_count;

	/* --- Search --- */
	int last_search_char; /* codepoint, not byte */
	int last_search_cmd;
	char *last_search_pattern;

	/* --- Highlight --- */
	char *highlight_pattern; /* pattern set by :run highlight; NULL = none */

	/* --- Insert / autoindent --- */
	int indent_col; /* column of recent autoindent, or 0 */
	int newindent;  /* -1 = use previous line's indent */

	/* --- Registers & marks --- */
	unsigned int ydreg; /* default yank/delete register index */
	char *reg[29];      /* a-z (0-25), D (26), U (27), shared (28) */
	char regtype[29];   /* WHOLE, MULTI, or PARTIAL for each register */
	char *mark[30];     /* a-z (0-25), context (26), prev-context (27), '<(28), '>(29) */

	/* --- Signals --- */
	volatile sig_atomic_t need_winch;
	volatile sig_atomic_t need_tstp;
	volatile sig_atomic_t need_int;

	/* --- Terminal --- */
	struct termios term_orig;
	int get_rowcol_error;

	/* --- Motion --- */
	int cindex; /* saved column for up/down motion */
	int keep_index;

	/* --- Files --- */
	char *current_filename;
	char *alt_filename;

	/* --- Undo --- */
	char undo_queue_state; /* UNDO_INS, UNDO_DEL, or UNDO_EMPTY */
	struct undo_object {
		struct undo_object *prev; /* LIFO stack link */
		int start;                /* offset where data is restored/deleted */
		int length;
		uint8_t u_type; /* 0=deleted  1=inserted  2=swapped */
		char undo_text[1];
	} *undo_stack_tail;
	struct undo_object *redo_stack_tail;
	char *undo_queue_spos; /* start position of queued operation */
	int undo_q;
	char undo_queue[UNDO_QUEUE_MAX];

	/* --- Session --- */
	time_t session_epoch; /* startup epoch; used to name the session yank file */
	char **initial_cmdv;
	int initial_cmdc;

	/* --- Restart --- */
	sigjmp_buf restart; /* int_handler() longjmps here */

	/* --- Scratch (promoted from file-scope statics) --- */
	char *dot_line;
	int scr_old_offset;
	int status_tot;

	/* --- Fixed-size buffers (large; keep at end) --- */
	char readbuffer[KEYCODE_BUFFER_SIZE];
	char status_buffer[STATUS_BUFFER_LEN];
	char last_modifying_cmd[128];
	char input_buf[128];
	char scr_out_buf[VI_MAX_LINE * 4 + 64];
};

/* --- Tiny helpers --- */
__attribute__((noreturn)) void die(const char *msg);
void show_usage(void);
void *xmalloc(size_t size);
void *xzalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);
ssize_t safe_read(int fd, void *buf, size_t count);
ssize_t full_read(int fd, void *buf, size_t len);
ssize_t full_write(int fd, const void *buf, size_t len);
int safe_poll(struct pollfd *pfd, nfds_t nfds, int timeout);
int get_terminal_width_height(int fd, unsigned *width, unsigned *height);

/* Minimal raw-mode helper. Keeps ISIG enabled (so ^C/^Z work). */
#define TERMIOS_RAW_CRNL 1

void set_termios_to_raw(int fd, struct termios *saved, int flags);
void tcsetattr_stdin_TCSANOW(const struct termios *saved);

/* memrchr / strchrnul are not available on all platforms (notably macOS). */
void *memrchr(const void *s, int c, size_t n);
char *strchrnul(const char *s, int c);

/* --- Path and file helpers --- */

char *xmalloc_open_read_close(const char *filename, size_t *sizep);

/* --- Small string helpers --- */

char *skip_whitespace(char *s);
char *skip_non_whitespace(char *s);
int index_in_strings(const char *strings, const char *key);

/* --- Key decoding --- */

#ifndef KEYCODE_BUFFER_SIZE
#define KEYCODE_BUFFER_SIZE 32
#endif

#define KEYCODE_UP 0x0101
#define KEYCODE_DOWN 0x0102
#define KEYCODE_LEFT 0x0103
#define KEYCODE_RIGHT 0x0104
#define KEYCODE_HOME 0x0105
#define KEYCODE_END 0x0106
#define KEYCODE_PAGEUP 0x0107
#define KEYCODE_PAGEDOWN 0x0108
#define KEYCODE_INSERT 0x0109
#define KEYCODE_DELETE 0x010A
#define KEYCODE_FUN1 0x0121
#define KEYCODE_FUN2 0x0122
#define KEYCODE_FUN3 0x0123
#define KEYCODE_FUN4 0x0124
#define KEYCODE_FUN5 0x0125
#define KEYCODE_FUN6 0x0126
#define KEYCODE_FUN7 0x0127
#define KEYCODE_FUN8 0x0128
#define KEYCODE_FUN9 0x0129
#define KEYCODE_FUN10 0x012A
#define KEYCODE_FUN11 0x012B
#define KEYCODE_FUN12 0x012C

#define KEYCODE_CURSOR_POS 0x0200
#define KEYCODE_PASTE_BEGIN 0x0201
#define KEYCODE_PASTE_END 0x0202

uint64_t safe_read_key(int fd, char *buffer, int timeout_ms);

/* --- Functions exported from vic.c --- */
void show_help(void);
void indicate_error(struct editor *g);
void reset_ydreg(struct editor *g);
int get_motion_char(struct editor *g);
void do_cmd(struct editor *g, int c, const struct cmd_ctx *ctx);
void edit_file(struct editor *g, char *fn);
void process_pending_signals(struct editor *g);
void sync_cursor(struct editor *g, char *d, int *row, int *col);
char *find_pair(struct editor *g, char *p, char c);
void showmatching(struct editor *g, char *p);

#endif /* VI_H */
