/*
 * test_color_yaml.c - unit tests for the YAML syntax colorizer and for the
 * file-name matching that selects it.
 *
 * Cross-line states (mirrors color_yaml.c):
 *   0      YAML_NORMAL
 *   1 + n  inside a block scalar introduced by a line indented n bytes
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_yaml;
#define YM colorizer_yaml.colorize
#define BLOCK(n) (1 + (n))

/* ---- mapping keys ------------------------------------------------------- */

static void
test_keys(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "key: value",   "KKK......."); CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "key:",         "KKK.");        CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "  nested: 1",  "..KKKKKK..N"); CHECK(st == 0);
    /* a plain key may contain blanks */
    st = CHECK_COLOR(YM, 0, "a b: c",       "KKK...");      CHECK(st == 0);
    /* quoted keys */
    st = CHECK_COLOR(YM, 0, "\"a b\": 1",   "KKKKK..N");    CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "'x': y",       "KKK...");      CHECK(st == 0);
    /* merge key */
    st = CHECK_COLOR(YM, 0, "<<: *base",    "KK..PPPPP");   CHECK(st == 0);
    /* a ':' not followed by whitespace does not close a key */
    st = CHECK_COLOR(YM, 0, "url: http://x", "KKK.........."); CHECK(st == 0);
}

static void
test_not_keys(void)
{
    int st;

    /* a bare scalar line has no key */
    st = CHECK_COLOR(YM, 0, "plain text",   "..........");  CHECK(st == 0);
    /* the ':' inside a value does not make the value a key */
    st = CHECK_COLOR(YM, 0, "k: a:b",       "K.....");      CHECK(st == 0);
}

/* ---- sequences ---------------------------------------------------------- */

static void
test_sequences(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "- item",        "P.....");      CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "  - name: foo", "..P.KKKK....."); CHECK(st == 0);
    /* nested sequence indicators on one line */
    st = CHECK_COLOR(YM, 0, "- - a",         "P.P..");       CHECK(st == 0);
    /* a dash with no blank after it is text, not an indicator */
    st = CHECK_COLOR(YM, 0, "-item",         ".....");       CHECK(st == 0);
    /* explicit key indicator */
    st = CHECK_COLOR(YM, 0, "? k",           "P..");         CHECK(st == 0);
}

/* ---- comments ----------------------------------------------------------- */

static void
test_comments(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "# note",       "CCCCCC");        CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "  # note",     "..CCCCCC");      CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "k: v # why",   "K....CCCCC");    CHECK(st == 0);
    /* a colon inside a comment does not turn the comment into a key */
    st = CHECK_COLOR(YM, 0, "# a: b",       "CCCCCC");        CHECK(st == 0);
    /* '#' without a leading blank is part of the scalar */
    st = CHECK_COLOR(YM, 0, "k: a#b",       "K.....");        CHECK(st == 0);
}

/* ---- scalars ------------------------------------------------------------ */

static void
test_quoted(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "k: \"a b\"",   "K..SSSSS");      CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "k: 'a b'",     "K..SSSSS");      CHECK(st == 0);
    /* backslash escape in a double-quoted scalar */
    st = CHECK_COLOR(YM, 0, "k: \"a\\\"b\"", "K..SSSSSS");    CHECK(st == 0);
    /* doubled quote in a single-quoted scalar */
    st = CHECK_COLOR(YM, 0, "k: 'a''b'",    "K..SSSSSS");     CHECK(st == 0);
    /* an unterminated quote does not leak into the next line */
    st = CHECK_COLOR(YM, 0, "k: \"open",    "K..SSSSS");      CHECK(st == 0);
}

static void
test_numbers_and_constants(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "port: 8080",   "KKKK..NNNN");    CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "r: -1.5e3",    "K..NNNNNN");     CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "m: 0x1f",      "K..NNNN");       CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "o: 0o755",     "K..NNNNN");      CHECK(st == 0);
    /* a version and a date are text, not numbers */
    st = CHECK_COLOR(YM, 0, "v: 1.2.3",     "K.......");       CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "d: 2026-09-12", "K............");  CHECK(st == 0);
    /* constants, in any case */
    st = CHECK_COLOR(YM, 0, "f: true",      "K..KKKK");       CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "f: False",     "K..KKKKK");      CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "n: null",      "K..KKKK");       CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "n: ~",         "K..K");          CHECK(st == 0);
    /* a word that merely starts like a constant is text */
    st = CHECK_COLOR(YM, 0, "s: nullable",  "K..........");     CHECK(st == 0);
}

static void
test_flow(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "k: [1, 2]",    "K...N..N.");    CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "k: [a, true]", "K......KKKK.");  CHECK(st == 0);
}

/* ---- anchors, aliases, tags --------------------------------------------- */

static void
test_nodes(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "k: &anc v",    "K..PPPP..");     CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "k: *anc",      "K..PPPP");       CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "k: !!str 1",   "K..PPPPP.N");    CHECK(st == 0);
}

/* ---- documents and directives ------------------------------------------- */

static void
test_documents(void)
{
    int st;

    st = CHECK_COLOR(YM, 0, "---",          "PPP");           CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "...",          "PPP");           CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "--- k: 1",     "PPP.K..N");      CHECK(st == 0);
    st = CHECK_COLOR(YM, 0, "%YAML 1.2",    "PPPPPPPPP");     CHECK(st == 0);
    /* only at column 0 */
    st = CHECK_COLOR(YM, 0, "  ---",        ".....");         CHECK(st == 0);
}

/* ---- block scalars ------------------------------------------------------ */

static void
test_block_scalars(void)
{
    int st;

    /* literal block: the introducing line records its own indent */
    st = CHECK_COLOR(YM, 0, "k: |", "K..P"); CHECK(st == BLOCK(0));
    st = CHECK_COLOR(YM, BLOCK(0), "  body: not a key", "SSSSSSSSSSSSSSSSS");
    CHECK(st == BLOCK(0));
    /* a blank line stays inside the block */
    st = CHECK_COLOR(YM, BLOCK(0), "", ""); CHECK(st == BLOCK(0));
    /* a line indented no further than the introducer ends the block */
    st = CHECK_COLOR(YM, BLOCK(0), "next: 1", "KKKK..N"); CHECK(st == 0);

    /* folded block, with chomping and comment after the indicator */
    st = CHECK_COLOR(YM, 0, "  k: >- # c", "..K..PP.CCC");
    CHECK(st == BLOCK(2));
    st = CHECK_COLOR(YM, BLOCK(2), "    text", "SSSSSSSS");
    CHECK(st == BLOCK(2));
    st = CHECK_COLOR(YM, BLOCK(2), "  other: 2", "..KKKKK..N"); CHECK(st == 0);

    /* a block scalar under a sequence entry */
    st = CHECK_COLOR(YM, 0, "- |", "P.P"); CHECK(st == BLOCK(0));
    st = CHECK_COLOR(YM, BLOCK(0), "  line", "SSSSSS"); CHECK(st == BLOCK(0));

    /* '|' with text after it is an ordinary scalar, not a block */
    st = CHECK_COLOR(YM, 0, "k: a | b", "K......."); CHECK(st == 0);
}

/* ---- file name matching ------------------------------------------------- */

static void
test_colorizer_find(void)
{
    CHECK(colorizer_find("config.yaml") == &colorizer_yaml);
    CHECK(colorizer_find("config.yml") == &colorizer_yaml);
    CHECK(colorizer_find("/etc/app/config.YAML") == &colorizer_yaml);
    CHECK(colorizer_find(".travis.yml") == &colorizer_yaml);
    /* names that are not YAML do not match */
    CHECK(colorizer_find("config.yamlx") == NULL);
    CHECK(colorizer_find("yaml") == NULL);
    /* other languages still resolve */
    CHECK(colorizer_find("main.c") != &colorizer_yaml);
    CHECK(colorizer_find("Dockerfile") != &colorizer_yaml);
}

/* ---- display width ------------------------------------------------------ */

static void
test_tabstop(void)
{
    /* YAML is written two spaces to the level */
    CHECK(colorizer_yaml.tabstop == 2);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keys();
    test_not_keys();
    test_sequences();
    test_comments();
    test_quoted();
    test_numbers_and_constants();
    test_flow();
    test_nodes();
    test_documents();
    test_block_scalars();
    test_colorizer_find();
    test_tabstop();
    SUMMARY();
}
