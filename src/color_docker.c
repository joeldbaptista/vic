/*
 * color_docker.c - syntax colorizer for Dockerfiles.
 *
 * Recognised tokens:
 *   Instructions  — FROM RUN CMD LABEL EXPOSE ENV ADD COPY ENTRYPOINT
 *                   VOLUME USER WORKDIR ARG ONBUILD STOPSIGNAL HEALTHCHECK
 *                   SHELL MAINTAINER, matched case-insensitively but only
 *                   as the first token of a logical line
 *   Variables     — $word, ${...} — highlighted as ATTR_PREPROC
 *   Comments      — '#' (parser directives such as "# syntax=..." included)
 *   Strings       — "..." (with $-expansion inside) and '...' (literal)
 *   Numbers       — bare integer literals, e.g. the port of EXPOSE
 *
 * Scanning is done by the generic engine in color_generic.c; see
 * color_generic.h for the cross-line state encoding.  An instruction is
 * only an instruction at the head of a logical line, so the spec sets
 * lang_spec.keywords_bol: a line continued with a trailing backslash keeps
 * its successor from opening a new instruction, which is what stops the
 * shell text inside a multi-line RUN from being coloured as Dockerfile
 * keywords.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

/*
 * Instructions are stored lower-case and matched after folding the source
 * token (lang_spec.keywords_ci); Docker itself accepts any case, though
 * upper-case is the convention.  This is the complete instruction set.
 */
static const char *const docker_keywords[] = {
    "add",
    "arg",
    "cmd",
    "copy",
    "entrypoint",
    "env",
    "expose",
    "from",
    "healthcheck",
    "label",
    "maintainer",
    "onbuild",
    "run",
    "shell",
    "stopsignal",
    "user",
    "volume",
    "workdir",
    NULL,
};

/*
 * Neither form spans lines, so neither sets cross_line: a Dockerfile
 * continues a line with a trailing backslash, not with an open quote.
 */
static const struct str_spec docker_strings[] = {
    {.open = '"', .escape = 1, .expand_sigil = 1},
    {.open = '\''},
    {0},
};

static const struct lang_spec docker_spec = {
    .keywords = docker_keywords,
    .keywords_ci = 1,
    .keywords_bol = 1,
    .line_comment = "#",
    .strings = docker_strings,
    .num = {.word_boundary_guard = 1},
    .sigil_char = '$',
};

static int
docker_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of a Dockerfile ==
	 */
	return colorize_generic(state, line, len, attrs, &docker_spec);
}

static const char *const docker_extensions[] = {
    ".dockerfile", ".containerfile", NULL};

/*
 * A Dockerfile usually carries no extension at all, so the registry also
 * matches these base names: exactly, or with a suffix after a dot
 * ("Dockerfile.dev").  The extensions above cover the other convention,
 * where the name is the suffix ("dev.Dockerfile").
 */
static const char *const docker_basenames[] = {
    "Dockerfile", "Containerfile", NULL};

const struct colorizer colorizer_docker = {
    "docker", docker_extensions, docker_basenames, docker_colorize, 4};
