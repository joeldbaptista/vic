CC=cc

CFLAGS=-std=c99 -D_POSIX_C_SOURCE=200809L -funsigned-char -Werror -Wall -Wextra -O2
SAN_COMMON=-std=c99 -D_POSIX_C_SOURCE=200809L -funsigned-char -Wall -Wextra -O1 -g -fno-omit-frame-pointer

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CFLAGS += -D_DARWIN_C_SOURCE
endif
 vic: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c
	$(CC) $(CFLAGS) -o vic src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c

vic-static: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c
	$(CC) $(CFLAGS) -static -o vic-static src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c

vic-asan: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c
	$(CC) $(SAN_COMMON) -fsanitize=address -o vic-asan src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c

vic-ubsan: src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c
	$(CC) $(SAN_COMMON) -fsanitize=undefined -o vic-ubsan src/vic.c src/utf8.c src/compat.c src/term.c src/input.c src/undo.c src/search.c src/screen.c src/motion.c src/operator.c src/editcmd.c src/textobj.c src/range.c src/scan.c src/visual.c src/wordmotion.c src/buffer.c src/pt.c src/text.c src/session.c src/excore.c src/ex.c src/line.c src/codepoint.c src/context.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/run.c src/parser.c src/regex.c

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

UNIT_TESTS = tests/test_pt tests/test_utf8 tests/test_line tests/test_codepoint \
             tests/test_text tests/test_buffer \
             tests/test_context tests/test_scan tests/test_wordmotion \
             tests/test_textobj tests/test_motion \
             tests/test_compat tests/test_excore tests/test_undo \
             tests/test_range tests/test_search \
             tests/test_editcmd tests/test_operator tests/test_status \
             tests/test_visual tests/test_run \
             tests/test_session tests/test_screen \
             tests/test_ex tests/test_vic \
             tests/test_color_c tests/test_color_py \
             tests/test_color_sh tests/test_color_md tests/test_color_sql \
             tests/test_color_docker tests/test_color_yaml \
             tests/test_color_tf tests/test_color_make tests/test_color_awk

tests/test_pt: tests/test_pt.c src/pt.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_pt.c src/pt.c src/compat.c

pt-test-run: tests/pt-test-run.c src/pt.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/pt-test-run.c src/pt.c src/compat.c

tests/test_utf8: tests/test_utf8.c src/utf8.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_utf8.c src/utf8.c

# line.c calls memrchr, which compat.c supplies on platforms lacking it.
tests/test_line: tests/test_line.c src/line.c src/compat.c src/pt.c src/text.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_line.c src/line.c src/compat.c src/pt.c src/text.c

tests/test_compat: tests/test_compat.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_compat.c src/compat.c

# excore.c needs status_line_bold and the three cursor-shape setters.
tests/test_excore: tests/test_excore.c src/excore.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_excore.c src/excore.c src/compat.c

# undo.c is tested against the real buffer.c: an undo entry is only
# correct if replaying it restores the text.
tests/test_undo: tests/test_undo.c src/undo.c src/buffer.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_undo.c src/undo.c src/buffer.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# range.c performs its motions through do_cmd (vic.c), which the test
# replaces with a programmable move; textobj.c is linked for real.
tests/test_range: tests/test_range.c src/range.c src/textobj.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_range.c src/range.c src/textobj.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# search.c is tested against the real vendored regex engine.
tests/test_search: tests/test_search.c src/search.c src/regex.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_search.c src/search.c src/regex.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# editcmd.c is tested against the real buffer.c, motion.c and undo.c.
tests/test_editcmd: tests/test_editcmd.c src/editcmd.c src/buffer.c src/motion.c src/undo.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_editcmd.c src/editcmd.c src/buffer.c src/motion.c src/undo.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# operator.c needs most of the editing core, which is linked for real so
# each operator can be checked by the text and register it leaves behind.
tests/test_operator: tests/test_operator.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_operator.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# status.c needs the terminal output functions, which the test stubs.
tests/test_status: tests/test_status.c src/status.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_status.c src/status.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# visual.c drives the operators, so the editing core is linked for real.
tests/test_visual: tests/test_visual.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_visual.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# run.c rewrites the buffer through buffer.c, which is linked for real so
# each transformation can also be undone.
tests/test_run: tests/test_run.c src/run.c src/buffer.c src/undo.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_run.c src/run.c src/buffer.c src/undo.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# session.c seeds per-file options from the colorizer registry, so the
# whole registry is linked for real.
tests/test_session: tests/test_session.c src/session.c src/buffer.c src/undo.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_session.c src/session.c src/buffer.c src/undo.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# screen.c renders with the real colorizers and the real visual mode.
tests/test_screen: tests/test_screen.c src/screen.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_screen.c src/screen.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# ex.c sits on top of nearly the whole editor, which is linked for real:
# an ex command is only correct if it leaves the right text behind.
tests/test_ex: tests/test_ex.c src/ex.c src/excore.c src/run.c src/session.c src/screen.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/context.c src/scan.c src/wordmotion.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_ex.c src/ex.c src/excore.c src/run.c src/session.c src/screen.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/context.c src/scan.c src/wordmotion.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# vic.c defines main(), so it is compiled separately with main renamed
# and the test supplies its own.
tests/test_vic_nomain.o: src/vic.c
	$(CC) $(CFLAGS) -I src -Dmain=vic_main -c -o $@ src/vic.c

tests/test_vic: tests/test_vic.c tests/test_vic_nomain.o src/parser.c src/ex.c src/excore.c src/run.c src/session.c src/screen.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/context.c src/scan.c src/wordmotion.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_vic.c tests/test_vic_nomain.o src/parser.c src/ex.c src/excore.c src/run.c src/session.c src/screen.c src/visual.c src/operator.c src/buffer.c src/motion.c src/undo.c src/range.c src/textobj.c src/editcmd.c src/search.c src/regex.c src/status.c src/context.c src/scan.c src/wordmotion.c src/color.c src/color_generic.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_awk.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# context.c touches only the marks, dot and text_len.
tests/test_context: tests/test_context.c src/context.c src/pt.c src/text.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_context.c src/context.c src/pt.c src/text.c src/compat.c

tests/test_scan: tests/test_scan.c src/scan.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_scan.c src/scan.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# wordmotion.c needs only skip_thing, so scan.c is linked for real.
tests/test_wordmotion: tests/test_wordmotion.c src/wordmotion.c src/scan.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_wordmotion.c src/wordmotion.c src/scan.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# text.c calls nothing but pt.c and xmalloc, so the test needs no stubs.
tests/test_text: tests/test_text.c src/text.c src/pt.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_text.c src/text.c src/pt.c src/compat.c

# textobj.c needs find_pair (defined in vic.c) and indicate_error; the
# test supplies both rather than linking the editor.
tests/test_textobj: tests/test_textobj.c src/textobj.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_textobj.c src/textobj.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# motion.c needs get_one_char, indicate_error and undo_queue_commit, which
# the test stubs; line.c and codepoint.c are linked for real.
tests/test_motion: tests/test_motion.c src/motion.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_motion.c src/motion.c src/pt.c src/text.c src/codepoint.c src/line.c src/utf8.c src/compat.c

# buffer.c calls into undo.c, status.c, input.c and search.c; the test
# supplies stubs for those symbols rather than linking the whole editor.
tests/test_buffer: tests/test_buffer.c src/buffer.c src/pt.c src/text.c src/line.c src/codepoint.c src/utf8.c src/compat.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_buffer.c src/buffer.c src/pt.c src/text.c src/line.c src/codepoint.c src/utf8.c src/compat.c

tests/test_codepoint: tests/test_codepoint.c src/codepoint.c src/line.c src/utf8.c src/compat.c src/pt.c src/text.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_codepoint.c src/codepoint.c src/line.c src/utf8.c src/compat.c src/pt.c src/text.c

tests/test_color_c: tests/test_color_c.c src/color_c.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_c.c src/color_c.c src/color_generic.c

tests/test_color_py: tests/test_color_py.c src/color_py.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_py.c src/color_py.c src/color_generic.c

tests/test_color_sh: tests/test_color_sh.c src/color_sh.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_sh.c src/color_sh.c src/color_generic.c

tests/test_color_md: tests/test_color_md.c src/color_md.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_md.c src/color_md.c

tests/test_color_sql: tests/test_color_sql.c src/color_sql.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_sql.c src/color_sql.c src/color_generic.c

# The Dockerfile test also exercises colorizer_find(), so it links the whole
# registry rather than one colorizer.
tests/test_color_docker: tests/test_color_docker.c src/color_docker.c src/color_yaml.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_tf.c src/color_make.c src/color_awk.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_docker.c src/color_docker.c src/color_yaml.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_tf.c src/color_make.c src/color_awk.c src/color_generic.c

# The YAML test also exercises colorizer_find(), so it links the whole
# registry rather than one colorizer.
tests/test_color_yaml: tests/test_color_yaml.c src/color_yaml.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_tf.c src/color_make.c src/color_awk.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_yaml.c src/color_yaml.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_tf.c src/color_make.c src/color_awk.c src/color_generic.c

# The Terraform test also exercises colorizer_find(), so it links the whole
# registry rather than one colorizer.
tests/test_color_tf: tests/test_color_tf.c src/color_tf.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_make.c src/color_awk.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_tf.c src/color_tf.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_make.c src/color_awk.c src/color_generic.c

# The makefile test also exercises colorizer_find(), so it links the whole
# registry rather than one colorizer.
tests/test_color_make: tests/test_color_make.c src/color_make.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_awk.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_make.c src/color_make.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_awk.c src/color_generic.c

# The awk test also exercises colorizer_find(), so it links the whole
# registry rather than one colorizer.
tests/test_color_awk: tests/test_color_awk.c src/color_awk.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_generic.c
	$(CC) $(CFLAGS) -I tests -I src -o $@ tests/test_color_awk.c src/color_awk.c src/color.c src/color_c.c src/color_sh.c src/color_md.c src/color_sql.c src/color_py.c src/color_docker.c src/color_yaml.c src/color_tf.c src/color_make.c src/color_generic.c

check-unit: $(UNIT_TESTS)
	@failed=0; \
	for t in $(UNIT_TESTS); do \
	    ./$$t || failed=1; \
	done; \
	exit $$failed

CHECKS = check-unit check-star-hash-pty check-regression-pty \
         check-asan-pty check-ubsan-pty check-sanitizers-pty

check: $(CHECKS)

.PHONY: check $(CHECKS)

frmt:
	clang-format -i src/*.c src/*.h tools/*.c

index:
	grep -n -E '^[a-zA-Z][a-zA-Z0-9_]+\(' src/*.c > functions.idx

PREFIX=/usr/local

install: clean vic install-man
	mkdir -p $(PREFIX)/bin
	install -m 755 vic $(PREFIX)/bin/vic

install-man: vic.1
	mkdir -p $(PREFIX)/share/man/man1
	cp vic.1 $(PREFIX)/share/man/man1/vic.1

deploy:
	rsync -av --include='src/***' --include='data/***' --include='tools/***' \
	    --include='Makefile' --include='vic.1' --exclude='*' ./ w01:~/vic/
	ssh w01 "cd ~/vic && make clean && make"

clean:
	rm -f vic vic-static vic-asan vic-ubsan tools/check-pty tests/test_vic_nomain.o $(UNIT_TESTS)
