/*
 * input.c - key reading and input queue management.
 *
 * safe_read_key() — reads one keystroke from a file descriptor, decoding
 *                   ANSI/VT escape sequences into KEYCODE_* values.
 *
 * readit()        — reads the next keystroke or escape sequence from the
 *                   input queue (g->readbuffer / g->ioq), calling
 *                   safe_read_key for fresh bytes.  Handles bracketed-paste
 *                   accumulation and rejects oversized pastes.
 *
 * get_one_char()  — higher-level wrapper: drains macro/replay buffers
 *                   (g->ioq_start) before falling through to readit().
 */
#include "input.h"

#include "status.h"
#include "term.h"

static int
read_byte_timeout(int fd, unsigned char *out, int timeout_ms)
{
	/*
	 * == Read one byte from fd with a millisecond timeout ==
	 *
	 * Returns 1 if a byte was read, 0 on timeout (errno = EAGAIN), -1 on
	 * error.  Used to read escape sequence continuations with a short
	 * timeout so a bare ESC is not confused with the start of a sequence.
	 */
	struct pollfd pfd;
	int pr;
	ssize_t r;

	pfd.fd = fd;
	pfd.events = POLLIN;
	pr = safe_poll(&pfd, 1, timeout_ms);
	if (pr < 0)
		return -1;
	if (pr == 0) {
		errno = EAGAIN;
		return 0;
	}
	r = safe_read(fd, out, 1);
	if (r == 1)
		return 1;
	return -1;
}

static int
is_csi_final_byte(unsigned char c)
{
	/*
	 * == True if c is a CSI final byte (0x40–0x7E) ==
	 *
	 * CSI sequences end at the first byte in this range; bytes before it
	 * are parameter or intermediate bytes.
	 */
	return c >= CSI_FINAL_BYTE_MIN && c <= CSI_FINAL_BYTE_MAX;
}

static int
parse_csi_params(const char *s, int *params, int max_params)
{
	/*
	 * == Parse semicolon-separated integer parameters from a CSI string ==
	 *
	 * s is the parameter string (everything between ESC[ and the final
	 * byte).  Fills params[] with up to max_params values and returns the
	 * count.  Missing (empty) fields are stored as 0.
	 */
	int count = 0;
	int value = -1;

	while (*s) {
		unsigned char c = (unsigned char)*s++;
		if (isdigit(c)) {
			if (value < 0)
				value = 0;
			value = value * 10 + (c - '0');
			continue;
		}
		if (c == ';' || c == ':') {
			if (count < max_params)
				params[count++] = (value < 0) ? 0 : value;
			value = -1;
			continue;
		}
		if (c == '?' || c == '>' || c == '=' || c == ' ')
			continue;
	}

	if (count < max_params)
		params[count++] = (value < 0) ? 0 : value;

	return count;
}

uint64_t
safe_read_key(int fd, char *buffer, int timeout_ms)
{
	/*
	 * == Read one keystroke and decode it into a KEYCODE_* value ==
	 *
	 * Reads one byte with timeout_ms timeout.  Plain characters are
	 * returned as-is.  ESC followed by [ or O triggers escape-sequence
	 * decoding using an 80 ms inter-byte timeout to distinguish bare ESC
	 * from an ANSI/VT sequence.
	 *
	 * - CSI sequences (ESC [): parsed for cursor keys, navigation keys,
	 *   bracketed paste markers, and CPR responses.
	 * - SS3 sequences (ESC O): cursor and home/end keys.
	 * - Unrecognised sequences fall back to returning ASCII_ESC.
	 * - buffer receives any extra character following the ESC when the
	 *   second byte is not [ or O (Meta-key pass-through).
	 *
	 * Returns (uint64_t)-1 on timeout or read error.
	 */
	unsigned char ch;
	unsigned char c1;
	unsigned char c2;
	char seq[KEYCODE_BUFFER_SIZE];
	int params[8];
	int nparam;
	int len;
	int r;
	const int seq_timeout_ms = 80;
	char final;

	if (buffer)
		buffer[0] = 0;

	r = read_byte_timeout(fd, &ch, timeout_ms);
	if (r <= 0)
		return (uint64_t)-1;

	if (ch != ASCII_ESC)
		return ch;

	r = read_byte_timeout(fd, &c1, seq_timeout_ms);
	if (r <= 0)
		return ASCII_ESC;
	if (c1 != '[' && c1 != 'O') {
		if (buffer) {
			buffer[0] = (char)c1;
			buffer[1] = '\0';
		}
		return ASCII_ESC;
	}

	if (c1 == '[') {
		len = 0;
		while (len < (int)sizeof(seq) - 1) {
			r = read_byte_timeout(fd, &c2, seq_timeout_ms);
			if (r <= 0)
				return ASCII_ESC;
			seq[len++] = (char)c2;
			if (is_csi_final_byte(c2))
				break;
		}
		if (len <= 0 || !is_csi_final_byte((unsigned char)seq[len - 1]))
			return ASCII_ESC;

		seq[len] = '\0';

		final = seq[len - 1];
		seq[len - 1] = '\0';
		nparam = parse_csi_params(seq, params, ARRAY_SIZE(params));

		switch (final) {
		case 'A':
			return KEYCODE_UP;
		case 'B':
			return KEYCODE_DOWN;
		case 'C':
			return KEYCODE_RIGHT;
		case 'D':
			return KEYCODE_LEFT;
		case 'H':
			return KEYCODE_HOME;
		case 'F':
			return KEYCODE_END;
		case 'Z':
			return '\t';
		case 'R':
			if (nparam >= 2) {
				uint32_t rc = ((uint32_t)(params[0] & CSI_COORD_MASK) << 16) |
				              (uint32_t)(params[1] & CSI_COORD_MASK);
				return ((uint64_t)rc << 32) | (uint32_t)KEYCODE_CURSOR_POS;
			}
			break;
		case '~':
			if (nparam >= 1) {
				switch (params[0]) {
				case 1:
				case 7:
					return KEYCODE_HOME;
				case 2:
					return KEYCODE_INSERT;
				case 3:
					return KEYCODE_DELETE;
				case 4:
				case 8:
					return KEYCODE_END;
				case 5:
					return KEYCODE_PAGEUP;
				case 6:
					return KEYCODE_PAGEDOWN;
				case CSI_PASTE_BEGIN_PARAM:
					return KEYCODE_PASTE_BEGIN;
				case CSI_PASTE_END_PARAM:
					return KEYCODE_PASTE_END;
				}
			}
			break;
		default:
			break;
		}

		return ASCII_ESC;
	}

	if (c1 == 'O') {
		r = read_byte_timeout(fd, &c2, seq_timeout_ms);
		if (r <= 0)
			return ASCII_ESC;
		switch (c2) {
		case 'A':
			return KEYCODE_UP;
		case 'B':
			return KEYCODE_DOWN;
		case 'C':
			return KEYCODE_RIGHT;
		case 'D':
			return KEYCODE_LEFT;
		case 'H':
			return KEYCODE_HOME;
		case 'F':
			return KEYCODE_END;
		default:
			return ASCII_ESC;
		}
	}

	return ASCII_ESC;
}

int
readit(struct editor *g)
{
	/*
	 * == Read the next keystroke from readbuffer or the terminal ==
	 *
	 * If readbuffer has a character from a previous multi-byte key
	 * decode, consume it first.  Otherwise polls the terminal via
	 * safe_read_key.  Processes pending signals between attempts so
	 * SIGWINCH is handled promptly.  Dies if the terminal becomes
	 * unreadable.
	 */
	int c;

	if (g->readbuffer[0]) {
		c = (unsigned char)g->readbuffer[0];
		memmove(g->readbuffer, g->readbuffer + 1, strlen(g->readbuffer));
		return c;
	}

	fflush(NULL);

again:
	process_pending_signals(g);
	c = safe_read_key(STDIN_FILENO, g->readbuffer, 100);
	if (c == -1) {
		if (errno == EAGAIN)
			goto again;
		go_bottom_and_clear_to_eol(g);
		cookmode(g);
		die("can't read user input");
	}

	return c;
}

static void
queue_bracketed_paste(struct editor *g)
{
	/*
	 * == Accumulate a bracketed-paste payload into g->ioq ==
	 *
	 * Reads keystrokes until KEYCODE_PASTE_END, collecting them into a
	 * heap buffer.  Replaces g->ioq_start so that subsequent get_one_char
	 * calls drain the pasted text as if it were typed.  Pastes larger
	 * than 1 MiB are silently truncated and a status message is shown.
	 */
	char *paste;
	size_t cap = 256;
	size_t len = 0;
	int truncated = 0;

	paste = xmalloc(cap);
	for (;;) {
		int c = readit(g);

		if (c == KEYCODE_PASTE_END)
			break;
		if (c < 0)
			break;
		if (c >= 0x100)
			continue;

		if (len + 2 >= cap) {
			if (cap >= 1024 * 1024) {
				truncated = 1;
				continue;
			}
			cap *= 2;
			paste = xrealloc(paste, cap);
		}

		if (!truncated)
			paste[len++] = (char)c;
	}

	if (truncated)
		status_line_bold(g, "Bracketed paste truncated at 1 MiB");

	if (len == 0) {
		free(paste);
		return;
	}

	paste[len] = '\0';
	if (g->ioq_start != NULL)
		free(g->ioq_start);
	g->ioq = g->ioq_start = paste;
}

int
get_one_char(struct editor *g)
{
	/*
	 * == Get the next input character from the macro queue or the terminal ==
	 *
	 * Priority: ioq (macro/paste replay) first, then the terminal via
	 * readit.  When adding2q is set, each character is also appended to
	 * last_modifying_cmd so the last editing command can be replayed by
	 * the dot operator.  Bracketed-paste sequences are transparently
	 * intercepted and queued as plain text.
	 */
	int c;

get_from_queue:
	if (g->ioq_start != NULL) {
		c = (unsigned char)*g->ioq++;
		if (c == '\0') {
			free(g->ioq_start);
			g->ioq_start = NULL;
			goto get_from_tty;
		}
		if (g->adding2q) {
			if ((size_t)g->lmc_len >= ARRAY_SIZE(g->last_modifying_cmd) - 2) {
				g->adding2q = 0;
				g->lmc_len = 0;
			} else {
				g->last_modifying_cmd[g->lmc_len++] = c;
			}
		}
		return c;
	}

get_from_tty:
	if (!g->adding2q) {
		c = readit(g);
		if (c == KEYCODE_PASTE_BEGIN) {
			queue_bracketed_paste(g);
			goto get_from_queue;
		}
		return c;
	}

	c = readit(g);
	if (c == KEYCODE_PASTE_BEGIN) {
		queue_bracketed_paste(g);
		goto get_from_queue;
	}
	if (c < 0x100 &&
	    (size_t)g->lmc_len >= ARRAY_SIZE(g->last_modifying_cmd) - 2) {
		g->adding2q = 0;
		g->lmc_len = 0;
	} else if (c < 0x100) {
		g->last_modifying_cmd[g->lmc_len++] = c;
	}

	return c;
}
