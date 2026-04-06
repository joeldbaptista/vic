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
	if (msg)
		fprintf(stderr, "vi: %s\n", msg);
	exit(1);
}

void
show_usage(void)
{
	fprintf(stderr, "usage: vi "
	                "[-c CMD] "
	                "[-R] "
	                "[-H] [FILE]...\n");
}

void *
xmalloc(size_t size)
{
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
	if (tcsetattr(STDIN_FILENO, TCSANOW, saved) != 0)
		die("tcsetattr restore failed");
}

void *
memrchr(const void *s, int c, size_t n)
{
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
	while (*s != '\0' && (unsigned char)*s != (unsigned char)c)
		s++;
	return (char *)s;
}

char *
xmalloc_open_read_close(const char *filename, size_t *sizep)
{
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
	while (*s && isspace((unsigned char)*s))
		s++;
	return s;
}

char *
skip_non_whitespace(char *s)
{
	while (*s && !isspace((unsigned char)*s))
		s++;
	return s;
}

int
index_in_strings(const char *strings, const char *key)
{
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
