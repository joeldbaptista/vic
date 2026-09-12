/*
 * test_color_docker.c - unit tests for the Dockerfile syntax colorizer and
 * for the file-name matching that selects it.
 *
 * Cross-line states (mirrors color_docker.c / color_generic.h):
 *   0  DOCKER_NORMAL
 *   4  DOCKER_LINE_CONT  (previous line ended with a backslash)
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_docker;
#define DK colorizer_docker.colorize
#define CONT 4

/* ---- instructions ------------------------------------------------------- */

static void
test_instructions(void)
{
    int st;

    st = CHECK_COLOR(DK, 0, "FROM alpine",  "KKKK......."); CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "RUN make",     "KKK.....");    CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "COPY a b",     "KKKK....");    CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "WORKDIR /app", "KKKKKKK.....");CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "ENTRYPOINT x", "KKKKKKKKKK.."); CHECK(st == 0);
    /* lower and mixed case — Docker accepts any case */
    st = CHECK_COLOR(DK, 0, "from alpine",  "KKKK......."); CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "From alpine",  "KKKK......."); CHECK(st == 0);
    /* leading whitespace is allowed before an instruction */
    st = CHECK_COLOR(DK, 0, "  RUN x",      "..KKK..");     CHECK(st == 0);
    /* not an instruction */
    st = CHECK_COLOR(DK, 0, "SELECT x",     "........");    CHECK(st == 0);
}

static void
test_instruction_only_at_line_head(void)
{
    int st;

    /* an instruction word later on the line is just an argument */
    st = CHECK_COLOR(DK, 0, "RUN env x",       "KKK......");    CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "COPY --from=b /x /y",
                            "KKKK...............");
    CHECK(st == 0);
    /* FROM ... AS name: AS is not highlighted, it is not an instruction */
    st = CHECK_COLOR(DK, 0, "FROM go AS build", "KKKK............"); CHECK(st == 0);
}

/* ---- line continuation -------------------------------------------------- */

static void
test_continuation(void)
{
    int st;

    /* trailing backslash: state → DOCKER_LINE_CONT */
    st = CHECK_COLOR(DK, 0, "RUN apk add \\", "KKK.........."); CHECK(st == CONT);
    /* the continuation line carries shell text, not instructions */
    st = CHECK_COLOR(DK, CONT, "    env FOO=1 \\", "............N.."); CHECK(st == CONT);
    st = CHECK_COLOR(DK, CONT, "    add-user x",   "..............");  CHECK(st == 0);
    /* after the logical line ends, instructions are recognised again */
    st = CHECK_COLOR(DK, 0, "USER app", "KKKK....");  CHECK(st == 0);
}

/* ---- comments ----------------------------------------------------------- */

static void
test_comments(void)
{
    int st;

    st = CHECK_COLOR(DK, 0, "# note",            "CCCCCC");        CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "#",                 "C");             CHECK(st == 0);
    /* parser directive — a comment as far as highlighting is concerned */
    st = CHECK_COLOR(DK, 0, "# syntax=docker/dockerfile:1",
                            "CCCCCCCCCCCCCCCCCCCCCCCCCCCC");
    CHECK(st == 0);
    /* comment after an instruction */
    st = CHECK_COLOR(DK, 0, "RUN x # why", "KKK...CCCCC"); CHECK(st == 0);
}

/* ---- strings ------------------------------------------------------------ */

static void
test_strings(void)
{
    int st;

    st = CHECK_COLOR(DK, 0, "CMD [\"sh\"]",  "KKK..SSSS."); CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "RUN 'a b'",     "KKK.SSSSS");  CHECK(st == 0);
    /* escaped quote inside a double-quoted string */
    st = CHECK_COLOR(DK, 0, "RUN \"a\\\"b\"", "KKK.SSSSSS"); CHECK(st == 0);
    /* an unterminated quote does not leak into the next line */
    st = CHECK_COLOR(DK, 0, "RUN \"open",    "KKK.SSSSS");  CHECK(st == 0);
}

/* ---- variables ---------------------------------------------------------- */

static void
test_variables(void)
{
    int st;

    st = CHECK_COLOR(DK, 0, "RUN $HOME",       "KKK.PPPPP");      CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "RUN ${VER:-1}",   "KKK.PPPPPPPPP");  CHECK(st == 0);
    /* expansion inside a double-quoted string */
    st = CHECK_COLOR(DK, 0, "RUN \"x $V y\"",  "KKK.SSSPPSSS");   CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(DK, 0, "EXPOSE 8080",    "KKKKKK.NNNN");   CHECK(st == 0);
    st = CHECK_COLOR(DK, 0, "EXPOSE 53/udp",  "KKKKKK.NN...."); CHECK(st == 0);
}

/* ---- file name matching ------------------------------------------------- */

static void
test_colorizer_find(void)
{
    /* the usual spellings, with and without a directory prefix */
    CHECK(colorizer_find("Dockerfile") == &colorizer_docker);
    CHECK(colorizer_find("dockerfile") == &colorizer_docker);
    CHECK(colorizer_find("/srv/app/Dockerfile") == &colorizer_docker);
    CHECK(colorizer_find("Containerfile") == &colorizer_docker);
    /* suffixed and prefixed variants */
    CHECK(colorizer_find("Dockerfile.dev") == &colorizer_docker);
    CHECK(colorizer_find("dev.Dockerfile") == &colorizer_docker);
    CHECK(colorizer_find("build.containerfile") == &colorizer_docker);
    /* names that only start like the base name do not match */
    CHECK(colorizer_find("Dockerfiles") == NULL);
    CHECK(colorizer_find("Dockerfile-old") == NULL);
    /* a dotted directory does not supply an extension to the file in it */
    CHECK(colorizer_find("/srv/app.d/Dockerfile") == &colorizer_docker);
    CHECK(colorizer_find("/srv/app.c/notes") == NULL);
    /* other languages still resolve */
    CHECK(colorizer_find("main.c") != NULL);
    CHECK(colorizer_find("main.c") != &colorizer_docker);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_instructions();
    test_instruction_only_at_line_head();
    test_continuation();
    test_comments();
    test_strings();
    test_variables();
    test_numbers();
    test_colorizer_find();
    SUMMARY();
}
