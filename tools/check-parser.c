/*
 * check-parser.c — unit tests for src/parser.c
 *
 * Compile and run (from repo root):
 *   cc -std=c99 -Wall -Wextra -I src \
 *      tools/check-parser.c src/parser.c -o check-parser
 *   ./check-parser
 *
 * Exit 0 = all pass, non-zero = failures.
 */
#include <stdio.h>
#include <string.h>
#include "parser.h"

static int failures = 0;

/*
 * Feed a NUL-terminated string into the parser.  Stops as soon as parse()
 * returns 1 (done).  Sends a '\0' sentinel only when the string is exhausted
 * without a completion signal (e.g. for STG_STRING ops like ':' and '/').
 */
static void
feed(struct parser *s, const char *keys)
{
    int r = 0;
    initparser(s);
    while (*keys) {
        r = parse(s, (unsigned char)*keys);
        keys++;
        if (r)
            return; /* already done — do not send sentinel */
    }
    if (!r)
        parse(s, '\0'); /* terminate string payload */
}

/* Feed an int array; last element must be 0 (sentinel). */
static void
feed_ints(struct parser *s, const int *keys)
{
    initparser(s);
    while (*keys) {
        parse(s, *keys);
        keys++;
    }
}

#define CHECK(desc, expr)                                          \
    do {                                                           \
        if (!(expr)) {                                             \
            printf("FAIL: %s\n", desc);                           \
            failures++;                                            \
        } else {                                                   \
            printf("pass: %s\n", desc);                           \
        }                                                          \
    } while (0)

/* ------------------------------------------------------------------ */
/* Original single-key ops (regression)                                */
/* ------------------------------------------------------------------ */

static void
test_simple_motions(void)
{
    struct parser s;

    feed(&s, "h");
    CHECK("h: ok=1, op='h'", s.ok == 1 && s.op == 'h' && s.m == 0);

    feed(&s, "j");
    CHECK("j: ok=1, op='j'", s.ok == 1 && s.op == 'j' && s.m == 0);

    feed(&s, "k");
    CHECK("k: ok=1, op='k'", s.ok == 1 && s.op == 'k' && s.m == 0);

    feed(&s, "l");
    CHECK("l: ok=1, op='l'", s.ok == 1 && s.op == 'l' && s.m == 0);

    feed(&s, "$");
    CHECK("$: ok=1", s.ok == 1 && s.op == '$');

    feed(&s, "w");
    CHECK("w: ok=1", s.ok == 1 && s.op == 'w');

    feed(&s, "b");
    CHECK("b: ok=1", s.ok == 1 && s.op == 'b');

    feed(&s, "e");
    CHECK("e: ok=1", s.ok == 1 && s.op == 'e');

    feed(&s, "G");
    CHECK("G: ok=1", s.ok == 1 && s.op == 'G');
}

static void
test_count_prefixed(void)
{
    struct parser s;

    feed(&s, "3j");
    CHECK("3j: m=3", s.ok == 1 && s.op == 'j' && s.m == 3);

    feed(&s, "10k");
    CHECK("10k: m=10", s.ok == 1 && s.op == 'k' && s.m == 10);

    feed(&s, "2w");
    CHECK("2w: m=2", s.ok == 1 && s.op == 'w' && s.m == 2);

    feed(&s, "5G");
    CHECK("5G: m=5", s.ok == 1 && s.op == 'G' && s.m == 5);
}

/* ------------------------------------------------------------------ */
/* Operator + range (d/y/c)                                            */
/* ------------------------------------------------------------------ */

static void
test_operator_range(void)
{
    struct parser s;

    feed(&s, "dw");
    CHECK("dw: op='d', rg='w'", s.ok == 1 && s.op == 'd' && s.rg == 'w');

    feed(&s, "d2w");
    CHECK("d2w: n=2, rg='w'", s.ok == 1 && s.op == 'd' && s.n == 2 && s.rg == 'w');

    feed(&s, "dd");
    CHECK("dd: op='d', rg='d'", s.ok == 1 && s.op == 'd' && s.rg == 'd');

    feed(&s, "yy");
    CHECK("yy: op='y', rg='y'", s.ok == 1 && s.op == 'y' && s.rg == 'y');

    feed(&s, "cc");
    CHECK("cc: op='c', rg='c'", s.ok == 1 && s.op == 'c' && s.rg == 'c');

    feed(&s, "ciw");
    CHECK("ciw: op='c', rg='i', a='w'",
          s.ok == 1 && s.op == 'c' && s.rg == 'i' && s.a == 'w');

    feed(&s, "daw");
    CHECK("daw: op='d', rg='a', a='w'",
          s.ok == 1 && s.op == 'd' && s.rg == 'a' && s.a == 'w');

    feed(&s, "2dw");
    CHECK("2dw: m=2, op='d', rg='w'",
          s.ok == 1 && s.m == 2 && s.op == 'd' && s.rg == 'w');
}

/* ------------------------------------------------------------------ */
/* Shift operators (<< and >>)                                         */
/* ------------------------------------------------------------------ */

static void
test_shift(void)
{
    struct parser s;

    feed(&s, ">>");
    CHECK(">>: op='>', rg='>'", s.ok == 1 && s.op == '>' && s.rg == '>');

    feed(&s, "<<");
    CHECK("<<: op='<', rg='<'", s.ok == 1 && s.op == '<' && s.rg == '<');

    feed(&s, "3>>");
    CHECK("3>>: m=3", s.ok == 1 && s.op == '>' && s.rg == '>' && s.m == 3);
}

/* ------------------------------------------------------------------ */
/* Find-char / replace (anchor ops)                                    */
/* ------------------------------------------------------------------ */

static void
test_anchor_ops(void)
{
    struct parser s;

    feed(&s, "fx");
    CHECK("fx: op='f', a='x'", s.ok == 1 && s.op == 'f' && s.a == 'x');

    feed(&s, "T,");
    CHECK("T,: op='T', a=','", s.ok == 1 && s.op == 'T' && s.a == ',');

    feed(&s, "ra");
    CHECK("ra: op='r', a='a'", s.ok == 1 && s.op == 'r' && s.a == 'a');

    feed(&s, "3ra");
    CHECK("3ra: m=3, op='r', a='a'",
          s.ok == 1 && s.m == 3 && s.op == 'r' && s.a == 'a');

    feed(&s, "ma");
    CHECK("ma: op='m', a='a'", s.ok == 1 && s.op == 'm' && s.a == 'a');

    feed(&s, "'z");
    CHECK("'z: op=''', a='z'", s.ok == 1 && s.op == '\'' && s.a == 'z');
}

/* ------------------------------------------------------------------ */
/* Register prefix                                                      */
/* ------------------------------------------------------------------ */

static void
test_register(void)
{
    struct parser s;

    feed(&s, "\"ayy");
    CHECK("\"ayy: reg='a', op='y', rg='y'",
          s.ok == 1 && s.reg == 'a' && s.op == 'y' && s.rg == 'y');

    feed(&s, "\"bdw");
    CHECK("\"bdw: reg='b', op='d', rg='w'",
          s.ok == 1 && s.reg == 'b' && s.op == 'd' && s.rg == 'w');

    feed(&s, "\"ap");
    CHECK("\"ap: reg='a', op='p'",
          s.ok == 1 && s.reg == 'a' && s.op == 'p');
}

/* ------------------------------------------------------------------ */
/* New single-key ops added in commit 3                                */
/* ------------------------------------------------------------------ */

static void
test_new_single_key(void)
{
    struct parser s;

    feed(&s, "n");
    CHECK("n: ok=1", s.ok == 1 && s.op == 'n');

    feed(&s, "N");
    CHECK("N: ok=1", s.ok == 1 && s.op == 'N');

    feed(&s, "~");
    CHECK("~: ok=1", s.ok == 1 && s.op == '~');

    feed(&s, "J");
    CHECK("J: ok=1", s.ok == 1 && s.op == 'J');

    feed(&s, "p");
    CHECK("p: ok=1", s.ok == 1 && s.op == 'p');

    feed(&s, "P");
    CHECK("P: ok=1", s.ok == 1 && s.op == 'P');

    feed(&s, "X");
    CHECK("X: ok=1", s.ok == 1 && s.op == 'X');

    feed(&s, "x");
    CHECK("x: ok=1", s.ok == 1 && s.op == 'x');

    feed(&s, "s");
    CHECK("s: ok=1", s.ok == 1 && s.op == 's');

    feed(&s, "u");
    CHECK("u: ok=1", s.ok == 1 && s.op == 'u');

    feed(&s, "U");
    CHECK("U: ok=1", s.ok == 1 && s.op == 'U');

    feed(&s, ".");
    CHECK(".: ok=1", s.ok == 1 && s.op == '.');

    feed(&s, "%");
    CHECK("%%: ok=1", s.ok == 1 && s.op == '%');

    feed(&s, ";");
    CHECK(";: ok=1", s.ok == 1 && s.op == ';');

    feed(&s, ",");
    CHECK(",: ok=1", s.ok == 1 && s.op == ',');

    feed(&s, "H");
    CHECK("H: ok=1", s.ok == 1 && s.op == 'H');

    feed(&s, "L");
    CHECK("L: ok=1", s.ok == 1 && s.op == 'L');

    feed(&s, "M");
    CHECK("M: ok=1", s.ok == 1 && s.op == 'M');

    feed(&s, "|");
    CHECK("|: ok=1", s.ok == 1 && s.op == '|');

    feed(&s, "I");
    CHECK("I: ok=1", s.ok == 1 && s.op == 'I');

    feed(&s, "R");
    CHECK("R: ok=1", s.ok == 1 && s.op == 'R');

    feed(&s, "v");
    CHECK("v: ok=1", s.ok == 1 && s.op == 'v');

    feed(&s, "V");
    CHECK("V: ok=1", s.ok == 1 && s.op == 'V');

    feed(&s, "B");
    CHECK("B: ok=1", s.ok == 1 && s.op == 'B');

    feed(&s, "E");
    CHECK("E: ok=1", s.ok == 1 && s.op == 'E');

    feed(&s, "W");
    CHECK("W: ok=1", s.ok == 1 && s.op == 'W');

    feed(&s, "Y");
    CHECK("Y: ok=1", s.ok == 1 && s.op == 'Y');

    feed(&s, "*");
    CHECK("*: ok=1", s.ok == 1 && s.op == '*');

    feed(&s, "#");
    CHECK("#: ok=1", s.ok == 1 && s.op == '#');

    feed(&s, "(");
    CHECK("(: ok=1", s.ok == 1 && s.op == '(');

    feed(&s, ")");
    CHECK("): ok=1", s.ok == 1 && s.op == ')');

    feed(&s, "{");
    CHECK("{: ok=1", s.ok == 1 && s.op == '{');

    feed(&s, "}");
    CHECK("}: ok=1", s.ok == 1 && s.op == '}');

    feed(&s, "+");
    CHECK("+: ok=1", s.ok == 1 && s.op == '+');

    feed(&s, "-");
    CHECK("-: ok=1", s.ok == 1 && s.op == '-');

    feed(&s, "D");
    CHECK("D: ok=1", s.ok == 1 && s.op == 'D');

    /* count-prefixed new ops */
    feed(&s, "3p");
    CHECK("3p: m=3, op='p'", s.ok == 1 && s.m == 3 && s.op == 'p');

    feed(&s, "5J");
    CHECK("5J: m=5, op='J'", s.ok == 1 && s.m == 5 && s.op == 'J');
}

/* ------------------------------------------------------------------ */
/* Two-key ops (gg, zt, ZZ, ...)                                       */
/* ------------------------------------------------------------------ */

static void
test_two_key(void)
{
    struct parser s;

    feed(&s, "gg");
    CHECK("gg: op='g', op2='g'", s.ok == 1 && s.op == 'g' && s.op2 == 'g');

    feed(&s, "ZZ");
    CHECK("ZZ: op='Z', op2='Z'", s.ok == 1 && s.op == 'Z' && s.op2 == 'Z');

    feed(&s, "ZQ");
    CHECK("ZQ: op='Z', op2='Q'", s.ok == 1 && s.op == 'Z' && s.op2 == 'Q');

    feed(&s, "zt");
    CHECK("zt: op='z', op2='t'", s.ok == 1 && s.op == 'z' && s.op2 == 't');

    feed(&s, "zb");
    CHECK("zb: op='z', op2='b'", s.ok == 1 && s.op == 'z' && s.op2 == 'b');
}

/* ------------------------------------------------------------------ */
/* String ops (:, /, ?)                                                */
/* ------------------------------------------------------------------ */

static void
test_string_ops(void)
{
    struct parser s;
    int r;

    initparser(&s);
    parse(&s, ':');
    parse(&s, 'q');
    parse(&s, '!');
    r = parse(&s, '\0');
    CHECK(":q!: done, ok=1", r == 1 && s.ok == 1 && s.op == ':');
    CHECK(":q! buf", strcmp(s.b, "q!") == 0);

    initparser(&s);
    parse(&s, '/');
    parse(&s, 'f');
    parse(&s, 'o');
    parse(&s, 'o');
    r = parse(&s, '\0');
    CHECK("/foo: done, ok=1", r == 1 && s.ok == 1 && s.op == '/');
    CHECK("/foo buf", strcmp(s.b, "foo") == 0);
}

/* ------------------------------------------------------------------ */
/* KEYCODE_* (raw_key) ops                                             */
/* ------------------------------------------------------------------ */

static void
test_keycode(void)
{
    struct parser s;
    int keys[3];

    /* KEYCODE_UP = 0x0101 */
#define KEYCODE_UP   0x0101
#define KEYCODE_DOWN 0x0102
#define KEYCODE_LEFT 0x0103

    /* bare arrow key */
    keys[0] = KEYCODE_UP; keys[1] = 0;
    feed_ints(&s, keys);
    CHECK("KEYCODE_UP: ok=1, raw_key set",
          s.ok == 1 && s.raw_key == KEYCODE_UP && s.op == '\0');

    keys[0] = KEYCODE_DOWN; keys[1] = 0;
    feed_ints(&s, keys);
    CHECK("KEYCODE_DOWN: ok=1", s.ok == 1 && s.raw_key == KEYCODE_DOWN);

    /* count + arrow key: feed '3' then KEYCODE_DOWN then sentinel 0 */
    initparser(&s);
    parse(&s, '3');
    parse(&s, KEYCODE_DOWN);
    CHECK("3+KEYCODE_DOWN: ok=1, m=3",
          s.ok == 1 && s.raw_key == KEYCODE_DOWN && s.m == 3);

#undef KEYCODE_UP
#undef KEYCODE_DOWN
#undef KEYCODE_LEFT
}

/* ------------------------------------------------------------------ */
/* Control character ops                                               */
/* ------------------------------------------------------------------ */

static void
test_control_chars(void)
{
    struct parser s;
    int keys[2];

#define CTRL_F 0x06
#define CTRL_B 0x02
#define CTRL_R 0x12
#define CTRL_L 0x0C

    keys[0] = CTRL_F; keys[1] = 0;
    feed_ints(&s, keys);
    CHECK("CTRL_F: ok=1, raw_key=0x06",
          s.ok == 1 && s.raw_key == CTRL_F && s.op == '\0');

    keys[0] = CTRL_B; keys[1] = 0;
    feed_ints(&s, keys);
    CHECK("CTRL_B: ok=1, raw_key=0x02",
          s.ok == 1 && s.raw_key == CTRL_B);

    keys[0] = CTRL_R; keys[1] = 0;
    feed_ints(&s, keys);
    CHECK("CTRL_R: ok=1, raw_key=0x12",
          s.ok == 1 && s.raw_key == CTRL_R);

    /* count + ctrl */
    initparser(&s);
    parse(&s, '5');
    parse(&s, CTRL_F);
    CHECK("5+CTRL_F: ok=1, m=5",
          s.ok == 1 && s.raw_key == CTRL_F && s.m == 5);

#undef CTRL_F
#undef CTRL_B
#undef CTRL_R
#undef CTRL_L
}

/* ------------------------------------------------------------------ */
/* Prefix-less ops (STG_START: 0^iaAC)                                */
/* ------------------------------------------------------------------ */

static void
test_prefix_less(void)
{
    struct parser s;

    feed(&s, "i");
    CHECK("i: ok=1, m=1", s.ok == 1 && s.op == 'i' && s.m == 1);

    feed(&s, "a");
    CHECK("a: ok=1", s.ok == 1 && s.op == 'a');

    feed(&s, "A");
    CHECK("A: ok=1", s.ok == 1 && s.op == 'A');

    feed(&s, "^");
    CHECK("^: ok=1", s.ok == 1 && s.op == '^');
}

/* ------------------------------------------------------------------ */
/* Invalid sequences                                                    */
/* ------------------------------------------------------------------ */

static void
test_invalid(void)
{
    struct parser s;

    feed(&s, "dq");
    CHECK("dq: ok=0", s.ok == 0);

    initparser(&s);
    parse(&s, '"');
    parse(&s, '\0'); /* null after register prefix */
    CHECK("\\\" + NUL: ok=0", s.ok == 0);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int
main(void)
{
    test_simple_motions();
    test_count_prefixed();
    test_operator_range();
    test_shift();
    test_anchor_ops();
    test_register();
    test_new_single_key();
    test_two_key();
    test_string_ops();
    test_keycode();
    test_control_chars();
    test_prefix_less();
    test_invalid();

    if (failures == 0) {
        printf("All tests passed.\n");
        return 0;
    }
    printf("%d test(s) FAILED.\n", failures);
    return 1;
}
