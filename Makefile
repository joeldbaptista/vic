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

tools/check-pty: tools/check-pty.c
	$(CC) -std=c99 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
	    -Wall -Wextra -O2 -o tools/check-pty tools/check-pty.c

check-star-hash-pty: vic tools/check-pty
	./tools/check-pty --vi ./vic --filter star,hash,gstar,ghash

check-regression-pty: vic tools/check-pty
	./tools/check-pty --vi ./vic

check-asan-pty: vic-asan tools/check-pty
	ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./tools/check-pty --vi ./vic-asan

check-ubsan-pty: vic-ubsan tools/check-pty
	UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 ./tools/check-pty --vi ./vic-ubsan

check-sanitizers-pty: check-asan-pty check-ubsan-pty

UNIT_TESTS = tests/test_utf8 tests/test_color_c tests/test_color_py \
             tests/test_color_sh tests/test_color_md tests/test_color_sql

tests/test_utf8: tests/test_utf8.c src/utf8.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_utf8.c src/utf8.c

tests/test_color_c: tests/test_color_c.c src/color_c.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_c.c src/color_c.c

tests/test_color_py: tests/test_color_py.c src/color_py.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_py.c src/color_py.c

tests/test_color_sh: tests/test_color_sh.c src/color_sh.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_sh.c src/color_sh.c

tests/test_color_md: tests/test_color_md.c src/color_md.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_md.c src/color_md.c

tests/test_color_sql: tests/test_color_sql.c src/color_sql.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_sql.c src/color_sql.c

check-unit: $(UNIT_TESTS)
	@failed=0; \
	for t in $(UNIT_TESTS); do \
	    ./$$t || failed=1; \
	done; \
	exit $$failed

frmt:
	clang-format -i src/*.c src/*.h tools/*.c

index:
	grep -n -E '^[a-zA-Z][a-zA-Z0-9_]+\(' src/*.c > functions.idx

install: clean vic
	mv vic /usr/bin/vic
	chmod a+x /usr/bin/vic

deploy:
	rsync -av --include='src/***' --include='data/***' --include='tools/***' \
	    --include='Makefile' --exclude='*' ./ w01:~/vic/
	ssh w01 "cd ~/vic && make clean && make"

clean:
	rm -f vic vic-asan vic-ubsan tools/check-pty $(UNIT_TESTS)
