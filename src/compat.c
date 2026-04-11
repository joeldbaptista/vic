/*
 * compat.c - portability and utility shims.
 *
 *   - abort-on-OOM wrappers: xmalloc, xzalloc, xrealloc, xstrdup, xstrndup,
 *     xasprintf
 *   - POSIX I/O helpers: safe_read, full_read, full_write, safe_poll
 *   - Terminal helpers: get_terminal_width_height, set_termios_to_raw,
 *     tcsetattr_stdin_TCSANOW
 *   - Portability shims: strchrnul, memrchr, xmalloc_open_read_close
 *   - String helpers: skip_whitespace, skip_non_whitespace, index_in_strings
 */
#include "vic.h"

__attribute__((noreturn)) void
die(const char *msg)
{
	/*
	 * == Print an error message and exit with status 1 ==
	 *
	 * Used for unrecoverable errors (failed syscalls, OOM).  Prints to
	 * stderr with a "vi: " prefix, then calls exit(1).
	 */
	if (msg)
		fprintf(stderr, "vi: %s\n", msg);
	exit(1);
}

void
show_usage(void)
{
	/*
	 * == Print a one-line usage summary to stderr ==
	 */
	fprintf(stderr, "usage: vi "
	                "[-c CMD] "
	                "[-R] "
	                "[-H] [FILE]...\n");
}

void *
xmalloc(size_t size)
{
	/*
	 * == malloc that aborts on OOM ==
	 */
	void *p = malloc(size);
	if (!p) {
		fprintf(stderr, "vi: out of memory\n");
		exit(1);
	}
	return p;
}

void *
xzalloc(size_t size)
{
	/*
	 * == calloc(1, size) that aborts on OOM ==
	 */
	void *p = calloc(1, size);
	if (!p) {
		fprintf(stderr, "vi: out of memory\n");
		exit(1);
	}
	return p;
}

void *
xrealloc(void *ptr, size_t size)
{
	/*
	 * == realloc that aborts on OOM ==
	 */
	void *p = realloc(ptr, size);
	if (!p) {
		fprintf(stderr, "vi: out of memory\n");
		exit(1);
	}
	return p;
}

char *
xstrdup(const char *s)
{
	/*
	 * == strdup that aborts on OOM; returns NULL for NULL input ==
	 */
	char *p;

	if (!s)
		return NULL;
	p = strdup(s);
	if (!p) {
		fprintf(stderr, "vi: out of memory\n");
		exit(1);
	}
	return p;
}

char *
xstrndup(const char *s, size_t n)
{
	/*
	 * == strndup that aborts on OOM; returns NULL for NULL input ==
	 */
	char *p;

	if (!s)
		return NULL;
	p = strndup(s, n);
	if (!p) {
		fprintf(stderr, "vi: out of memory\n");
		exit(1);
	}
	return p;
}

ssize_t
safe_read(int fd, void *buf, size_t count)
{
	/*
	 * == read() that retries on EINTR ==
	 *
	 * Returns the result of read(), with EINTR transparently restarted.
	 * Other errors (EIO, EBADF, etc.) are returned to the caller as-is.
	 */
	for (;;) {
		ssize_t r = read(fd, buf, count);
		if (r < 0 && errno == EINTR)
			continue;
		return r;
	}
}

ssize_t
full_read(int fd, void *buf, size_t len)
{
	/*
	 * == Read exactly len bytes, looping until done or EOF/error ==
	 *
	 * Returns the total number of bytes read, which may be less than len
	 * if EOF is reached before the buffer is full.  Returns -1 on error.
	 */
	uint8_t *p = (uint8_t *)buf;
	size_t total = 0;

	while (total < len) {
		ssize_t r = safe_read(fd, p + total, len - total);
		if (r < 0)
			return -1;
		if (r == 0)
			break;
		total += (size_t)r;
	}
	return (ssize_t)total;
}

ssize_t
full_write(int fd, const void *buf, size_t len)
{
	/*
	 * == Write exactly len bytes, looping until done or error ==
	 *
	 * Retries on EINTR; returns -1 on any other write error.
	 */
	const uint8_t *p = (const uint8_t *)buf;
	size_t total = 0;

	while (total < len) {
		ssize_t w = write(fd, p + total, len - total);
		if (w < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		total += (size_t)w;
	}
	return (ssize_t)total;
}

int
safe_poll(struct pollfd *pfd, nfds_t nfds, int timeout)
{
	/*
	 * == poll() that retries on EINTR ==
	 */
	for (;;) {
		int r = poll(pfd, nfds, timeout);
		if (r < 0 && errno == EINTR)
			continue;
		return r;
	}
}

int
get_terminal_width_height(int fd, unsigned *width, unsigned *height)
{
	/*
	 * == Query terminal dimensions via TIOCGWINSZ ==
	 *
	 * Writes columns to *width and rows to *height if non-NULL.  Returns
	 * 0 on success, -1 if the ioctl fails or reports zero dimensions.
	 */
	struct winsize ws;

	if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
		if (width)
			*width = ws.ws_col;
		if (height)
			*height = ws.ws_row;
		return 0;
	}
	return -1;
}

void
set_termios_to_raw(int fd, struct termios *saved, int flags)
{
	/*
	 * == Switch a terminal fd to raw mode ==
	 *
	 * Saves the current termios in *saved and applies a raw-mode config:
	 * ECHO and ICANON off, signals kept (ISIG on), VMIN=1/VTIME=0.
	 * Pass TERMIOS_RAW_CRNL to keep ICRNL (map CR to NL on input), which
	 * is needed for the PTY test harness.
	 */
	struct termios t;

	if (tcgetattr(fd, saved) != 0)
		die("tcgetattr failed");

	t = *saved;
	t.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN);
	t.c_iflag &= (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	if (flags & TERMIOS_RAW_CRNL)
		t.c_iflag |= ICRNL;
	t.c_oflag &= (tcflag_t) ~(OPOST);
	t.c_cflag |= (CS8);
	t.c_lflag |= ISIG;
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSANOW, &t) != 0)
		die("tcsetattr failed");
}

void
tcsetattr_stdin_TCSANOW(const struct termios *saved)
{
	/*
	 * == Restore stdin termios from a saved snapshot ==
	 *
	 * Called on exit/suspend to return the terminal to cooked mode.
	 * Dies if the restore fails (terminal in unusable state otherwise).
	 */
	if (tcsetattr(STDIN_FILENO, TCSANOW, saved) != 0)
		die("tcsetattr restore failed");
}

void *
memrchr(const void *s, int c, size_t n)
{
	/*
	 * == Reverse search for byte c in the first n bytes of s ==
	 *
	 * Portability shim — not available on all platforms (e.g., musl/Alpine).
	 * Returns a pointer to the last occurrence of c, or NULL if not found.
	 */
	const unsigned char *p = (const unsigned char *)s;
	size_t i;

	for (i = n; i > 0; i--) {
		if (p[i - 1] == (unsigned char)c)
			return (void *)(p + (i - 1));
	}
	return NULL;
}

char *
strchrnul(const char *s, int c)
{
	/*
	 * == Like strchr, but returns pointer to '\0' instead of NULL ==
	 *
	 * Portability shim for systems without strchrnul (POSIX extension).
	 * Always returns a valid pointer into s.
	 */
	while (*s != '\0' && (unsigned char)*s != (unsigned char)c)
		s++;
	return (char *)s;
}

char *
xmalloc_open_read_close(const char *filename, size_t *sizep)
{
	/*
	 * == Read an entire file into a heap buffer ==
	 *
	 * Opens filename, reads all content into a NUL-terminated xmalloc'd
	 * buffer, and closes the file.  If sizep is non-NULL, stores the byte
	 * count (excluding the NUL).  Returns NULL if the file cannot be
	 * opened or read.  The caller is responsible for free()ing the buffer.
	 */
	int fd;
	struct stat st;
	size_t cap;
	char *buf;
	size_t len = 0;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return NULL;

	if (fstat(fd, &st) != 0) {
		close(fd);
		return NULL;
	}

	cap = (st.st_size > 0) ? (size_t)st.st_size : 0;
	buf = xmalloc(cap + 1);
	while (1) {
		ssize_t r = safe_read(fd, buf + len, cap - len);
		if (r < 0) {
			free(buf);
			close(fd);
			return NULL;
		}
		if (r == 0)
			break;
		len += (size_t)r;
		if (len == cap) {
			cap = cap ? (cap * 2) : 1024;
			buf = xrealloc(buf, cap + 1);
		}
	}
	buf[len] = '\0';
	if (sizep)
		*sizep = len;
	close(fd);
	return buf;
}

char *
skip_whitespace(char *s)
{
	/*
	 * == Advance s past any leading whitespace characters ==
	 */
	while (*s && isspace((unsigned char)*s))
		s++;
	return s;
}

char *
skip_non_whitespace(char *s)
{
	/*
	 * == Advance s past any leading non-whitespace characters ==
	 */
	while (*s && !isspace((unsigned char)*s))
		s++;
	return s;
}

int
index_in_strings(const char *strings, const char *key)
{
	/*
	 * == Find key in a NUL-NUL-terminated string table ==
	 *
	 * strings is a sequence of NUL-terminated strings packed together and
	 * terminated by an extra NUL (like a double-NUL-terminated list).
	 * Returns the zero-based index of the first match, or -1 if not found.
	 */
	int idx = 0;
	const char *p = strings;

	if (!strings || !key)
		return -1;
	while (*p) {
		size_t len = strlen(p);
		if (strcmp(p, key) == 0)
			return idx;
		p += len + 1;
		idx++;
	}
	return -1;
}
