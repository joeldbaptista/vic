/*
 * check-pty.c — PTY-driven regression test runner for vic.
 *
 * Spawns vic in a pseudo-terminal, sends key sequences, writes changes to
 * disk with :write, then asserts final file content.  Some tests inspect
 * raw or ANSI-stripped terminal output instead of file content.
 *
 * Usage:
 *   check-pty [--vi <path>] [--tmp-dir <dir>] [--filter <n1,n2,...>]
 *
 * --filter limits the run to test names that contain any of the listed
 * substrings (comma-separated).  With no filter all tests run.
 *
 * Compile:
 *   cc -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2 \
 *      -o tools/check-pty tools/check-pty.c
 *
 * Exit 0 = all pass, 1 = at least one failure, 2 = usage error.
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* time                                                                 */
/* ------------------------------------------------------------------ */

static double
now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ------------------------------------------------------------------ */
/* dynamic byte buffer                                                  */
/* ------------------------------------------------------------------ */

struct buf {
	char *data;
	size_t len;
	size_t cap;
};

static void
buf_init(struct buf *b)
{
	b->data = malloc(4096);
	b->len = 0;
	b->cap = 4096;
	b->data[0] = '\0';
}

static void
buf_append(struct buf *b, const char *src, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		b->cap = (b->len + n + 1) * 2;
		b->data = realloc(b->data, b->cap);
	}
	memcpy(b->data + b->len, src, n);
	b->len += n;
	b->data[b->len] = '\0';
}

static void
buf_free(struct buf *b)
{
	free(b->data);
}

/* ------------------------------------------------------------------ */
/* xmemmem — memmem is not in POSIX                                    */
/* ------------------------------------------------------------------ */

static void *
xmemmem(const void *hay, size_t hlen, const void *ndl, size_t nlen)
{
	const char *h = hay;
	const char *n = ndl;
	size_t i;

	if (nlen == 0)
		return (void *)hay;
	for (i = 0; i + nlen <= hlen; i++)
		if (memcmp(h + i, n, nlen) == 0)
			return (void *)(h + i);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* PTY                                                                  */
/* ------------------------------------------------------------------ */

/*
 * open_master_pty — open and configure a PTY master.
 *
 * Returns: master fd on success, -1 on error.
 */
static int
open_master_pty(void)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0) {
		perror("posix_openpt");
		return -1;
	}
	if (grantpt(master) < 0 || unlockpt(master) < 0) {
		perror("grantpt/unlockpt");
		close(master);
		return -1;
	}
	/* Non-blocking so the drain loop after child exit does not hang. */
	fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
	return master;
}

/*
 * spawn_vic — fork and exec vic with its stdio wired to the PTY slave.
 *
 * Parameters:
 *   master    - PTY master fd (open in parent)
 *   vi_path   - path to vic binary
 *   file_path - file argument passed to vic
 * Returns: child pid on success, -1 on error.
 */
static pid_t
spawn_vic(int master, const char *vi_path, const char *file_path)
{
	char slave_name[64];
	const char *sn;
	pid_t pid;
	int slave;

	sn = ptsname(master);
	if (!sn) {
		perror("ptsname");
		return -1;
	}
	strncpy(slave_name, sn, sizeof(slave_name) - 1);
	slave_name[sizeof(slave_name) - 1] = '\0';

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}
	if (pid == 0) {
		if (setsid() < 0)
			_exit(1);
		slave = open(slave_name, O_RDWR);
		if (slave < 0)
			_exit(1);
		if (ioctl(slave, TIOCSCTTY, 0) < 0)
			_exit(1);
		dup2(slave, STDIN_FILENO);
		dup2(slave, STDOUT_FILENO);
		dup2(slave, STDERR_FILENO);
		if (slave > STDERR_FILENO)
			close(slave);
		close(master);
		execlp(vi_path, vi_path, file_path, (char *)NULL);
		_exit(127);
	}
	return pid;
}

/*
 * pump_output — read from the PTY master for at most `secs` seconds,
 * appending output to `b`.
 *
 * Returns: child exit code if the process has exited, -1 if still running.
 */
static int
pump_output(int master, pid_t pid, struct buf *b, double secs)
{
	double deadline = now_sec() + secs;
	char tmp[4096];
	ssize_t n;
	int wstatus;

	for (;;) {
		double remaining = deadline - now_sec();
		struct timeval tv;
		fd_set rfds;

		if (remaining <= 0)
			break;

		tv.tv_sec = 0;
		tv.tv_usec = (long)(remaining > 0.03 ? 30000 : remaining * 1e6);

		FD_ZERO(&rfds);
		FD_SET(master, &rfds);

		if (select(master + 1, &rfds, NULL, NULL, &tv) > 0) {
			n = read(master, tmp, sizeof(tmp));
			if (n > 0)
				buf_append(b, tmp, (size_t)n);
			else if (n < 0 && errno != EINTR && errno != EAGAIN)
				break; /* EIO: slave closed */
		}

		if (waitpid(pid, &wstatus, WNOHANG) == pid) {
			/* Drain any remaining output. */
			while ((n = read(master, tmp, sizeof(tmp))) > 0)
				buf_append(b, tmp, (size_t)n);
			if (WIFEXITED(wstatus))
				return WEXITSTATUS(wstatus);
			if (WIFSIGNALED(wstatus))
				return 128 + WTERMSIG(wstatus);
			return 1;
		}
	}

	if (waitpid(pid, &wstatus, WNOHANG) == pid) {
		if (WIFEXITED(wstatus))
			return WEXITSTATUS(wstatus);
		if (WIFSIGNALED(wstatus))
			return 128 + WTERMSIG(wstatus);
	}
	return -1;
}

/*
 * wait_startup — pump output until `needle` appears in `b` or `timeout`
 * elapses.
 */
static void
wait_startup(int master, pid_t pid, struct buf *b,
             const char *needle, double timeout)
{
	double deadline = now_sec() + timeout;
	size_t nlen = strlen(needle);

	while (now_sec() < deadline) {
		pump_output(master, pid, b, 0.05);
		if (b->len >= nlen && xmemmem(b->data, b->len, needle, nlen))
			break;
	}
}

/*
 * finish — terminate the process if still running and return its exit code.
 *
 * Parameters:
 *   rc       - return value from pump_output (-1 if still running)
 *   pid      - child pid
 *   timed_out - set to 1 if process had to be killed
 * Returns: decoded exit code.
 */
static int
finish(int rc, pid_t pid, int *timed_out)
{
	int wstatus;

	*timed_out = (rc < 0);
	if (*timed_out) {
		kill(pid, SIGTERM);
		waitpid(pid, &wstatus, 0);
		return 143;
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* write_all — write entire buffer, retrying on EINTR                  */
/* ------------------------------------------------------------------ */

static void
write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	ssize_t n;

	while (len > 0) {
		n = write(fd, p, len);
		if (n <= 0)
			break;
		p += n;
		len -= (size_t)n;
	}
}

/* ------------------------------------------------------------------ */
/* file utilities                                                       */
/* ------------------------------------------------------------------ */

static int
write_file(const char *path, const char *content)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	fputs(content, f);
	fclose(f);
	return 0;
}

/*
 * read_file — read entire file into a heap string.
 *
 * Returns: heap-allocated null-terminated string, or NULL on error.
 *          Caller frees.
 */
static char *
read_file(const char *path)
{
	FILE *f;
	long sz;
	char *buf;

	f = fopen(path, "rb");
	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	fread(buf, 1, (size_t)sz, f);
	buf[sz] = '\0';
	fclose(f);
	return buf;
}

/* ------------------------------------------------------------------ */
/* ANSI escape stripping                                               */
/* ------------------------------------------------------------------ */

/*
 * strip_ansi — remove ANSI/VT escape sequences; replace \r with \n.
 *
 * Returns: heap-allocated null-terminated string.  Caller frees.
 */
static char *
strip_ansi(const char *src, size_t srclen)
{
	char *dst = malloc(srclen + 1);
	size_t di = 0;
	size_t i = 0;

	while (i < srclen) {
		unsigned char c = (unsigned char)src[i];
		if (c == 0x1b && i + 1 < srclen) {
			unsigned char nx = (unsigned char)src[i + 1];
			if (nx == '[') {
				/* CSI sequence: skip to final byte (0x40-0x7e). */
				i += 2;
				while (i < srclen) {
					unsigned char b = (unsigned char)src[i++];
					if (b >= 0x40 && b <= 0x7e)
						break;
				}
			} else if ((nx >= 0x40 && nx <= 0x5a) ||
			           (nx >= 0x5c && nx <= 0x5f)) {
				i += 2; /* two-byte ESC sequence */
			} else {
				dst[di++] = (char)c;
				i++;
			}
		} else if (c == '\r') {
			dst[di++] = '\n';
			i++;
		} else {
			dst[di++] = (char)c;
			i++;
		}
	}
	dst[di] = '\0';
	return dst;
}

/* ------------------------------------------------------------------ */
/* helpers                                                              */
/* ------------------------------------------------------------------ */

static const char *
basename_of(const char *path)
{
	const char *p = strrchr(path, '/');
	return p ? p + 1 : path;
}

#define STARTUP_TIMEOUT 2.50  /* wait up to 2.5s for filename in output */
#define STARTUP_SETTLE  0.60  /* additional settle after filename appears */
#define FINISH_TIMEOUT  5.00  /* wait up to 5s for process to exit */

/* ------------------------------------------------------------------ */
/* file-content test runner                                             */
/* ------------------------------------------------------------------ */

/*
 * run_file_case — spawn vic, send keys, assert file content matches expected.
 *
 * Parameters:
 *   name     - test name (used for temp file and output labels)
 *   vi_path  - path to vic binary
 *   tmp_dir  - directory for temp files
 *   keys     - key bytes to send after startup
 *   keys_len - number of bytes in keys
 *   sample   - initial file content
 *   expected - expected file content after vic exits
 * Returns: 1 on pass, 0 on fail.
 */
static int
run_file_case(const char *name, const char *vi_path, const char *tmp_dir,
              const unsigned char *keys, size_t keys_len,
              const char *sample, const char *expected)
{
	char path[512];
	int master, rc, timed_out, ok;
	pid_t pid;
	struct buf out;
	char *got;
	char *stripped;
	size_t tail;

	snprintf(path, sizeof(path), "%s/vic_%s.txt", tmp_dir, name);

	if (write_file(path, sample) < 0) {
		fprintf(stderr, "[%s] failed to write temp file\n", name);
		return 0;
	}

	master = open_master_pty();
	if (master < 0)
		return 0;
	pid = spawn_vic(master, vi_path, path);
	if (pid < 0) {
		close(master);
		return 0;
	}

	buf_init(&out);
	wait_startup(master, pid, &out, basename_of(path), STARTUP_TIMEOUT);
	pump_output(master, pid, &out, STARTUP_SETTLE);
	write_all(master, keys, keys_len);
	rc = finish(pump_output(master, pid, &out, FINISH_TIMEOUT), pid, &timed_out);
	close(master);

	got = read_file(path);
	ok = got && strcmp(got, expected) == 0;

	printf("[%s] rc=%d timed_out=%s\n", name, rc, timed_out ? "True" : "False");
	if (ok) {
		printf("[%s] PASS\n", name);
	} else {
		printf("[%s] FAIL\n", name);
		printf("[%s] expected:\n%s", name, expected);
		printf("[%s] got:\n%s", name, got ? got : "(null)");
		stripped = strip_ansi(out.data, out.len);
		tail = strlen(stripped);
		tail = tail > 1200 ? tail - 1200 : 0;
		if (stripped[0])
			printf("[%s] output tail:\n%s\n", name, stripped + tail);
		free(stripped);
	}
	printf("[%s] file: %s\n", name, path);

	buf_free(&out);
	free(got);
	return ok;
}

/* ------------------------------------------------------------------ */
/* output-inspection test cases                                         */
/* ------------------------------------------------------------------ */

static int
run_insert_live_redraw(const char *vi_path, const char *tmp_dir)
{
	const char *name = "insert-live-redraw";
	char path[512];
	int master, rc, timed_out, ok;
	pid_t pid;
	struct buf out;
	size_t before;
	char *mid;

	snprintf(path, sizeof(path), "%s/vic_insert_live_redraw.txt", tmp_dir);
	write_file(path, "zzzz\n");

	master = open_master_pty();
	if (master < 0)
		return 0;
	pid = spawn_vic(master, vi_path, path);
	if (pid < 0) {
		close(master);
		return 0;
	}

	buf_init(&out);
	wait_startup(master, pid, &out, basename_of(path), STARTUP_TIMEOUT);
	pump_output(master, pid, &out, STARTUP_SETTLE);

	before = out.len;
	write_all(master, (const unsigned char *)"iXYZ", 4);
	pump_output(master, pid, &out, 0.40);

	mid = strip_ansi(out.data + before, out.len - before);
	ok = strstr(mid, "XYZ") != NULL;
	free(mid);

	write_all(master, (const unsigned char *)"\x1b:q!\r", 5);
	rc = finish(pump_output(master, pid, &out, FINISH_TIMEOUT), pid, &timed_out);
	close(master);

	printf("[%s] rc=%d timed_out=%s\n", name, rc, timed_out ? "True" : "False");
	if (ok)
		printf("[%s] PASS\n", name);
	else {
		printf("[%s] FAIL\n", name);
		printf("[%s] expected to see typed text before Esc\n", name);
	}
	printf("[%s] file: %s\n", name, path);

	buf_free(&out);
	return ok;
}

static int
run_number_toggle(const char *vi_path, const char *tmp_dir)
{
	const char *name = "set-nu-rnu-screen";
	char path[512];
	int master, rc, timed_out, ok;
	pid_t pid;
	struct buf out;
	char *rendered;
	regex_t re_nu, re_tilde;
	int has_nu_effect, no_numbered_tilde;
	const char *keys = ":set nu\r:set nonu rnu\r:q!\r";
	size_t tail;

	snprintf(path, sizeof(path), "%s/vic_set_nu_rnu.txt", tmp_dir);
	write_file(path, "alpha\nbeta\ngamma\n");

	master = open_master_pty();
	if (master < 0)
		return 0;
	pid = spawn_vic(master, vi_path, path);
	if (pid < 0) {
		close(master);
		return 0;
	}

	buf_init(&out);
	wait_startup(master, pid, &out, basename_of(path), STARTUP_TIMEOUT);
	pump_output(master, pid, &out, STARTUP_SETTLE);
	write_all(master, (const unsigned char *)keys, strlen(keys));
	rc = finish(pump_output(master, pid, &out, FINISH_TIMEOUT), pid, &timed_out);
	close(master);

	rendered = strip_ansi(out.data, out.len);

	regcomp(&re_nu,    "(^|[^0-9])1 +alpha",  REG_EXTENDED | REG_NEWLINE);
	regcomp(&re_tilde, "[0-9]+[[:space:]]+~",  REG_EXTENDED | REG_NEWLINE);
	has_nu_effect     = (regexec(&re_nu,    rendered, 0, NULL, 0) == 0);
	no_numbered_tilde = (regexec(&re_tilde, rendered, 0, NULL, 0) != 0);
	regfree(&re_nu);
	regfree(&re_tilde);

	ok = has_nu_effect && no_numbered_tilde;

	printf("[%s] rc=%d timed_out=%s\n", name, rc, timed_out ? "True" : "False");
	if (ok) {
		printf("[%s] PASS\n", name);
	} else {
		printf("[%s] FAIL\n", name);
		printf("[%s] has_nu_effect=%d no_numbered_tilde=%d\n",
		       name, has_nu_effect, no_numbered_tilde);
		tail = strlen(rendered);
		tail = tail > 800 ? tail - 800 : 0;
		printf("[%s] rendered tail:\n%s\n", name, rendered + tail);
	}
	printf("[%s] file: %s\n", name, path);

	free(rendered);
	buf_free(&out);
	return ok;
}

static int
run_visual_highlight(const char *vi_path, const char *tmp_dir)
{
	const char *name = "visual-highlight-render";
	const char *inv_on  = "\x1b[7m";
	const char *inv_off = "\x1b[m";
	size_t on_len  = 4; /* \x1b [ 7 m */
	size_t off_len = 3; /* \x1b [ m   */
	char path[512];
	int master, rc, timed_out, ok;
	int has_inverse = 0, no_trailing_space = 0;
	pid_t pid;
	struct buf out;
	const char *p;
	size_t remaining;

	snprintf(path, sizeof(path), "%s/vic_visual_highlight.txt", tmp_dir);
	write_file(path, "abcde\n");

	master = open_master_pty();
	if (master < 0)
		return 0;
	pid = spawn_vic(master, vi_path, path);
	if (pid < 0) {
		close(master);
		return 0;
	}

	buf_init(&out);
	wait_startup(master, pid, &out, basename_of(path), STARTUP_TIMEOUT);
	pump_output(master, pid, &out, STARTUP_SETTLE);
	write_all(master, (const unsigned char *)"v$", 2);
	pump_output(master, pid, &out, 0.35);

	/* Find an inverse-video segment containing "abcde". */
	p = out.data;
	remaining = out.len;
	while (remaining > on_len) {
		char *seg_start = xmemmem(p, remaining, inv_on, on_len);
		char *seg_end;
		size_t seg_len;
		char *seg;

		if (!seg_start)
			break;
		seg_start += on_len;
		remaining -= (size_t)(seg_start - p);
		seg_end = xmemmem(seg_start, remaining, inv_off, off_len);
		if (!seg_end)
			break;
		seg_len = (size_t)(seg_end - seg_start);
		seg = malloc(seg_len + 1);
		memcpy(seg, seg_start, seg_len);
		seg[seg_len] = '\0';
		if (strstr(seg, "abcde")) {
			has_inverse = 1;
			no_trailing_space = (strchr(seg, ' ') == NULL);
		}
		free(seg);
		remaining -= seg_len + off_len;
		p = seg_end + off_len;
	}

	write_all(master, (const unsigned char *)"\x1b:q!\r", 5);
	rc = finish(pump_output(master, pid, &out, FINISH_TIMEOUT), pid, &timed_out);
	close(master);

	ok = has_inverse && no_trailing_space;

	printf("[%s] rc=%d timed_out=%s\n", name, rc, timed_out ? "True" : "False");
	if (ok) {
		printf("[%s] PASS\n", name);
	} else {
		printf("[%s] FAIL\n", name);
		printf("[%s] has_inverse=%d no_trailing_space=%d\n",
		       name, has_inverse, no_trailing_space);
	}
	printf("[%s] file: %s\n", name, path);

	buf_free(&out);
	return ok;
}

static int
run_visual_esc_clear(const char *vi_path, const char *tmp_dir)
{
	const char *name = "visual-esc-clears-highlight";
	const char *inv_on = "\x1b[7m";
	size_t on_len = 4;
	char path[512];
	int master, rc, timed_out, ok;
	pid_t pid;
	struct buf out;
	size_t before_esc;

	snprintf(path, sizeof(path), "%s/vic_visual_esc_clear.txt", tmp_dir);
	write_file(path, "abcde\n");

	master = open_master_pty();
	if (master < 0)
		return 0;
	pid = spawn_vic(master, vi_path, path);
	if (pid < 0) {
		close(master);
		return 0;
	}

	buf_init(&out);
	wait_startup(master, pid, &out, basename_of(path), STARTUP_TIMEOUT);
	pump_output(master, pid, &out, STARTUP_SETTLE);
	write_all(master, (const unsigned char *)"v$", 2);
	pump_output(master, pid, &out, 0.30);

	before_esc = out.len;
	write_all(master, (const unsigned char *)"\x1b", 1);
	pump_output(master, pid, &out, 0.40);

	/* No inverse-video sequence should appear after the ESC. */
	ok = (xmemmem(out.data + before_esc, out.len - before_esc,
	              inv_on, on_len) == NULL);

	write_all(master, (const unsigned char *)":q!\r", 4);
	rc = finish(pump_output(master, pid, &out, FINISH_TIMEOUT), pid, &timed_out);
	close(master);

	printf("[%s] rc=%d timed_out=%s\n", name, rc, timed_out ? "True" : "False");
	if (ok)
		printf("[%s] PASS\n", name);
	else {
		printf("[%s] FAIL\n", name);
		printf("[%s] expected no inverse-video after Esc\n", name);
	}
	printf("[%s] file: %s\n", name, path);

	buf_free(&out);
	return ok;
}

/* ------------------------------------------------------------------ */
/* test case table                                                      */
/* ------------------------------------------------------------------ */

struct tc {
	const char *name;
	const unsigned char *keys;
	size_t keys_len;
	const char *sample;
	const char *expected;
};

/* TC: build a struct tc from a string literal for keys. */
#define TC(n, k, s, e) \
	{ n, (const unsigned char *)(k), sizeof(k) - 1, s, e }

static const struct tc cases[] = {
	/* * # g* g# word search */
	TC("star",  "*x:write\r",   "foo one\nfoo two\nfoo three\n", "foo one\noo two\nfoo three\n"),
	TC("hash",  "jj#x:write\r", "foo one\nfoo two\nfoo three\n", "foo one\noo two\nfoo three\n"),
	TC("gstar", "g*x:write\r",  "foo\nzfooz\nfoo\n",             "foo\nzooz\nfoo\n"),
	TC("ghash", "G0g#x:write\r","foo\nzfooz\nfoo\n",             "foo\nzooz\nfoo\n"),
	/* j/k and arrow keys land on first non-blank */
	TC("j-first-nonblank",    "jiX\x1b:write\r",       "top\n    mid\nbottom\n", "top\n    Xmid\nbottom\n"),
	TC("k-first-nonblank",    "GkiX\x1b:write\r",      "top\n    mid\nbottom\n", "top\n    Xmid\nbottom\n"),
	TC("down-first-nonblank", "\x1b[BiX\x1b:write\r",  "top\n    mid\nbottom\n", "top\n    Xmid\nbottom\n"),
	TC("up-first-nonblank",   "G\x1b[AiX\x1b:write\r", "top\n    mid\nbottom\n", "top\n    Xmid\nbottom\n"),
	/* visual mode */
	TC("visual-char-delete",      "vld:write\r",       "abcde\n",    "cde\n"),
	TC("visual-line-delete",      "Vjd:write\r",       "one\ntwo\nthree\n", "three\n"),
	TC("visual-put-replace",      "ywj0vllp:write\r",  "foo\nbar\n", "foo\nfoo\n"),
	TC("visual-textobj-vi-paren", "f(lvi(d:write\r",   "x (abc) y\n","x () y\n"),
	TC("visual-textobj-va-paren", "f(lva(d:write\r",   "x (abc) y\n","x  y\n"),
	TC("visual-textobj-viw",      "wviwd:write\r",     "one two three\n", "one  three\n"),
	TC("visual-textobj-vi-quote", "f\"lvi\"d:write\r", "x \"abc\" y\n", "x \"\" y\n"),
	TC("visual-textobj-vit",      "f>lvitd:write\r",   "<p>hello</p>\n", "<p></p>\n"),
	/* count-prefixed motion */
	TC("count-motion-3j",  "3jx:write\r",    "one\ntwo\nthree\nfour\nfive\n", "one\ntwo\nthree\nour\nfive\n"),
	/* operator + motion */
	TC("operator-dw",   "dw:write\r",    "foo bar\n",     "bar\n"),
	TC("operator-d2w",  "d2w:write\r",   "foo bar baz\n", "baz\n"),
	TC("operator-dd",   "dd:write\r",    "one\ntwo\nthree\n", "two\nthree\n"),
	TC("operator-yyp",  "yyp:write\r",   "foo\nbar\n",    "foo\nfoo\nbar\n"),
	TC("operator-cc",   "ccnew\x1b:write\r", "foo\nbar\n","new\nbar\n"),
	/* named registers */
	TC("register-yank-put", "\"ayyj\"ap:write\r", "foo\nbar\n", "foo\nbar\nfoo\n"),
	/* shift */
	TC("shift-right", ">>:write\r", "foo\n",   "\tfoo\n"),
	TC("shift-left",  "<<:write\r", "\tfoo\n", "foo\n"),
	/* ZZ write-and-quit */
	TC("zz-write-quit", "xZZ", "hello\n", "ello\n"),
	/* dot repeat */
	TC("dot-repeat-dw",           "dw.:write\r",          "foo bar baz\n", "baz\n"),
	TC("dot-repeat-dd",           "dd.:write\r",          "one\ntwo\nthree\n", "three\n"),
	TC("dot-repeat-shift",        ">>.:write\r",          "foo\n",         "\t\tfoo\n"),
	TC("dot-repeat-replace-char", "r,l.:write\r",         "abcde\n",       ",,cde\n"),
	TC("dot-repeat-cc",           "ccnew\x1bj.:write\r",  "old\ntext\n",   "new\nnew\n"),
	TC("dot-repeat-ciw",          "wciwnew\x1b" "0.:write\r","foo bar\n",   "new new\n"),
	/* undo / redo */
	TC("undo-x",  "xu:write\r",     "hello\n",    "hello\n"),
	TC("undo-dd", "ddu:write\r",    "one\ntwo\n", "one\ntwo\n"),
	TC("redo-x",  "xu\x12:write\r", "hello\n",    "ello\n"),
	/* search */
	TC("search-forward",  "/bar\rx:write\r",    "foo\nbar\nbaz\n",     "foo\nar\nbaz\n"),
	TC("search-backward", "G?foo\rx:write\r",   "foo\nbar\nfoo\n",     "oo\nbar\nfoo\n"),
	TC("search-n-repeat", "/bar\rnx:write\r",   "foo\nbar\nbaz\nbar\n","foo\nbar\nbaz\nar\n"),
	/* marks */
	TC("mark-set-jump",   "mamajj'ax:write\r",  "one\ntwo\nthree\n",  "ne\ntwo\nthree\n"),
	TC("colon-mark",      ":mark a\rjj'ax:write\r", "one\ntwo\nthree\n", "ne\ntwo\nthree\n"),
	TC("colon-mark-addr", ":2mark b\r'bx:write\r",  "one\ntwo\nthree\n", "one\nwo\nthree\n"),
	/* replace mode */
	TC("replace-mode", "RXY\x1b:write\r", "abcde\n", "XYcde\n"),
	/* clipboard register */
	TC("visual-shared-yank-put", "viw+yj$+p:write\r", "abc\ndef\n", "abc\ndefabc\n"),
	/* :r! */
	TC("read-shell",      ":r!echo hello\r:write\r",       "first\nlast\n", "first\nhello\nlast\n"),
	TC("read-shell-addr", ":1r!echo inserted\r:write\r",   "aaa\nbbb\n",    "aaa\ninserted\nbbb\n"),
};

/* ------------------------------------------------------------------ */
/* filter                                                               */
/* ------------------------------------------------------------------ */

/*
 * matches_filter — return 1 if `name` contains any comma-separated token
 * in `filter`, or if filter is NULL/empty.
 */
static int
matches_filter(const char *name, const char *filter)
{
	char tmp[256];
	char *tok;

	if (!filter || !*filter)
		return 1;
	strncpy(tmp, filter, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	tok = strtok(tmp, ",");
	while (tok) {
		if (strstr(name, tok))
			return 1;
		tok = strtok(NULL, ",");
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(int argc, char *argv[])
{
	const char *vi_path = "./vic";
	const char *tmp_dir = "/tmp";
	const char *filter  = NULL;
	size_t i, n;
	int all_ok = 1;

	for (i = 1; i < (size_t)argc; i++) {
		if (strcmp(argv[i], "--vi") == 0 && i + 1 < (size_t)argc)
			vi_path = argv[++i];
		else if (strcmp(argv[i], "--tmp-dir") == 0 && i + 1 < (size_t)argc)
			tmp_dir = argv[++i];
		else if (strcmp(argv[i], "--filter") == 0 && i + 1 < (size_t)argc)
			filter = argv[++i];
	}

	if (access(vi_path, X_OK) != 0) {
		fprintf(stderr, "error: vi binary not found: %s\n", vi_path);
		return 2;
	}

	n = sizeof(cases) / sizeof(cases[0]);
	for (i = 0; i < n; i++) {
		if (!matches_filter(cases[i].name, filter))
			continue;
		all_ok = run_file_case(
		    cases[i].name, vi_path, tmp_dir,
		    cases[i].keys, cases[i].keys_len,
		    cases[i].sample, cases[i].expected
		) && all_ok;
	}

	if (matches_filter("insert-live-redraw", filter))
		all_ok = run_insert_live_redraw(vi_path, tmp_dir) && all_ok;
	if (matches_filter("set-nu-rnu-screen", filter))
		all_ok = run_number_toggle(vi_path, tmp_dir) && all_ok;
	if (matches_filter("visual-highlight-render", filter))
		all_ok = run_visual_highlight(vi_path, tmp_dir) && all_ok;
	if (matches_filter("visual-esc-clears-highlight", filter))
		all_ok = run_visual_esc_clear(vi_path, tmp_dir) && all_ok;

	return all_ok ? 0 : 1;
}
