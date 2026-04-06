CC=cc

# BusyBox vi source assumes unsigned chars and uses a few GNU/POSIX APIs.
CFLAGS=-std=c99 -D_POSIX_C_SOURCE=200809L -funsigned-char -Werror -Wall -Wextra -O2
SAN_COMMON=-std=c99 -D_POSIX_C_SOURCE=200809L -funsigned-char -Wall -Wextra -O1 -g -fno-omit-frame-pointer

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CFLAGS += -D_DARWIN_C_SOURCE
endif

vic: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/run.c src/parser.c
	$(CC) $(CFLAGS) -o vic src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/run.c src/parser.c

vic-asan: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/run.c src/parser.c
	$(CC) $(SAN_COMMON) -fsanitize=address -o vic-asan src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/run.c src/parser.c

vic-ubsan: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/run.c src/parser.c
	$(CC) $(SAN_COMMON) -fsanitize=undefined -o vic-ubsan src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/run.c src/parser.c

check-star-hash-pty: vic
	python3 tools/check-star-hash-pty.py --vi ./vic

check-regression-pty: vic
	python3 tools/check-regression-pty.py --vi ./vic

check-asan-pty: vic-asan
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 python3 tools/check-regression-pty.py --vi ./vic-asan

check-ubsan-pty: vic-ubsan
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 python3 tools/check-regression-pty.py --vi ./vic-ubsan

check-sanitizers-pty: check-asan-pty check-ubsan-pty

frmt:
	clang-format -i src/*.c src/*.h tools/*.c

index:
	grep -n -E '^[a-zA-Z][a-zA-Z0-9_]+\(' src/*.c > functions.idx

install: clean vic
	mv vic /usr/bin/vic
	chmod a+x /usr/bin/vic

clean:
	rm -f vic vic-asan vic-ubsan
