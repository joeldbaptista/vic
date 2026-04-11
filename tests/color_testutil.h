/*
 * color_testutil.h - helpers for colorizer unit tests.
 *
 * Depends on testutil.h (must be included first).
 *
 * CHECK_COLOR(fn, state, line, want)
 *   Colorizes `line` starting from `state`, checks that every byte's
 *   attribute matches the corresponding character in `want`, and returns
 *   the new cross-line state.
 *
 * Attribute encoding in `want` strings (one char per source byte):
 *   '.'  ATTR_NORMAL
 *   'C'  ATTR_COMMENT
 *   'S'  ATTR_STRING
 *   'P'  ATTR_PREPROC
 *   'K'  ATTR_KEYWORD
 *   'N'  ATTR_NUMBER
 *
 * Example:
 *   int st = CHECK_COLOR(fn, 0, "int x;", "KKK...");
 *   CHECK(st == 0);
 */
#ifndef TESTS_COLOR_TESTUTIL_H
#define TESTS_COLOR_TESTUTIL_H

#include "testutil.h"
#include "color.h"
#include <stdio.h>
#include <string.h>

static char
attr_char(int a)
{
    switch (a) {
    case ATTR_NORMAL:  return '.';
    case ATTR_COMMENT: return 'C';
    case ATTR_STRING:  return 'S';
    case ATTR_PREPROC: return 'P';
    case ATTR_KEYWORD: return 'K';
    case ATTR_NUMBER:  return 'N';
    default:           return '?';
    }
}

static int
attr_decode(char c)
{
    switch (c) {
    case '.': return ATTR_NORMAL;
    case 'C': return ATTR_COMMENT;
    case 'S': return ATTR_STRING;
    case 'P': return ATTR_PREPROC;
    case 'K': return ATTR_KEYWORD;
    case 'N': return ATTR_NUMBER;
    default:  return ATTR_NORMAL;
    }
}

static int
check_color_impl(colorize_fn fn, int state, const char *line,
                 const char *want, const char *file, int lineno)
{
    int len = (int)strlen(line);
    int wlen = (int)strlen(want);
    char attrs[512];
    char got[513];
    int i, ok;
    int new_state;

    if (len != wlen) {
        fprintf(stderr,
                "FATAL %s:%d: line len %d != want len %d\n"
                "  line: \"%s\"\n"
                "  want: \"%s\"\n",
                file, lineno, len, wlen, line, want);
        exit(1);
    }

    memset(attrs, ATTR_NORMAL, sizeof(attrs));
    new_state = fn(state, line, len, attrs);
    t_run++;

    ok = 1;
    for (i = 0; i < len; i++) {
        if ((int)(unsigned char)attrs[i] != attr_decode(want[i])) {
            ok = 0;
            break;
        }
    }

    if (!ok) {
        t_fail++;
        for (i = 0; i < len; i++)
            got[i] = attr_char((int)(unsigned char)attrs[i]);
        got[len] = '\0';
        fprintf(stderr, "  FAIL %s:%d\n", file, lineno);
        fprintf(stderr, "    line: \"%s\"\n", line);
        fprintf(stderr, "     got: %s\n", got);
        fprintf(stderr, "    want: %s\n", want);
    }

    return new_state;
}

#define CHECK_COLOR(fn, st, line, want) \
    check_color_impl((fn), (st), (line), (want), __FILE__, __LINE__)

#endif /* TESTS_COLOR_TESTUTIL_H */
