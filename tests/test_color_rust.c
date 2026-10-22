/*
 * test_color_rust.c - unit tests for the Rust syntax colorizer and for the
 * file-name matching that selects it.
 *
 * Cross-line states used here:
 *   0  CS_NORMAL
 *   1  CS_BLOCK_CMT
 *   2  CS_BLOCK_CMT_STAR
 *   8  CS_STRING_BASE + 0 (a "..." left open at the end of the line)
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_rust;
#define RUST colorizer_rust.colorize

/* ---- keywords ----------------------------------------------------------- */

static void
test_keywords(void)
{
    int st;

    st = CHECK_COLOR(RUST, 0, "fn main() {",        "KK.........");        CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "pub struct S;",      "KKK.KKKKKK...");      CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "impl Trait for S {", "KKKK.......KKK...."); CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "match x {",          "KKKKK....");          CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "async fn f() {}",    "KKKKK.KK.......");    CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "unsafe { *p = 0; }", "KKKKKK........N...");  CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "self.x / 2",         "KKKK.....N");         CHECK(st == 0);
    /* the primitive type names are built into the language */
    st = CHECK_COLOR(RUST, 0, "let n: u32 = 0;",    "KKK....KKK...N.");    CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "s: &str",            "....KKK");            CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "let v: Vec<u8> = vec![1, 2];",
                              "KKK........KK.........N..N..");             CHECK(st == 0);
    /* the weak keywords count too */
    st = CHECK_COLOR(RUST, 0, "macro_rules! m {",  "KKKKKKKKKKK.....");   CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "union U { a: u8 }", "KKKKK........KK..");  CHECK(st == 0);
    /* library names are not keywords to Rust */
    st = CHECK_COLOR(RUST, 0, "Some(Vec::new())",   "................");   CHECK(st == 0);
    /* a name that only contains a keyword is not one */
    st = CHECK_COLOR(RUST, 0, "format(x)",          ".........");          CHECK(st == 0);
    /* a macro name is an ordinary name, and the '!' is punctuation */
    st = CHECK_COLOR(RUST, 0, "println!(\"hi\")",   ".........SSSS.");     CHECK(st == 0);
}

/* ---- strings ------------------------------------------------------------ */

static void
test_strings(void)
{
    int st;

    st = CHECK_COLOR(RUST, 0, "\"hi\"",             "SSSS");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "\"a\\\"b\"",         "SSSSSS");             CHECK(st == 0);
    /* the b and r prefixes belong to the literal */
    st = CHECK_COLOR(RUST, 0, "b\"bytes\"",         "SSSSSSSS");           CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "r\"a\\b\"",          "SSSSSS");             CHECK(st == 0);
    /* '//' and '#' inside a string are part of it */
    st = CHECK_COLOR(RUST, 0, "let u = \"//#\";",   "KKK.....SSSSS.");     CHECK(st == 0);
    /* a Rust string may span lines, so state 8 carries it over */
    st = CHECK_COLOR(RUST, 0, "let s = \"first",    "KKK.....SSSSSS");     CHECK(st == 8);
    st = CHECK_COLOR(RUST, 8, "middle",             "SSSSSS");             CHECK(st == 8);
    st = CHECK_COLOR(RUST, 8, "last\";",            "SSSSS.");             CHECK(st == 0);
    /*
     * A hashed raw string is approximated: the hashes colour as an
     * attribute sigil, and the quote inside the body ends the string.
     */
    st = CHECK_COLOR(RUST, 0, "let s = r#\"raw \"q\" t\"#;",
                              "KKK......PSSSSSS.SSSSP.");                  CHECK(st == 0);
}

/* ---- character literals, lifetimes and labels --------------------------- */

static void
test_chars_and_lifetimes(void)
{
    int st;

    st = CHECK_COLOR(RUST, 0, "let c = 'x';",       "KKK.....SSS.");       CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "'\\n'",              "SSSS");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "'\\''",              "SSSS");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "b'0'",               "SSSS");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "if c == 'a' && d == 'b' {",
                              "KK......SSS.........SSS..");                CHECK(st == 0);
    /* a lifetime opens no literal, so it does not run to end of line */
    st = CHECK_COLOR(RUST, 0, "fn f<'a>(s: &'a str) {",
                              "KK..............KKK...");                   CHECK(st == 0);
    /* the name of a lifetime is not looked up as a keyword either */
    st = CHECK_COLOR(RUST, 0, "let r: &'static str;",
                              "KKK.............KKK.");                     CHECK(st == 0);
    /* two lifetimes on one line stay uncoloured */
    st = CHECK_COLOR(RUST, 0, "impl<'a, 'b> T {",   "KKKK............");   CHECK(st == 0);
    /* a loop label, and a break naming it */
    st = CHECK_COLOR(RUST, 0, "'outer: loop {",     "........KKKK..");     CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "break 'outer;",      "KKKKK........");      CHECK(st == 0);
}

/* ---- comments ----------------------------------------------------------- */

static void
test_comments(void)
{
    int st;

    st = CHECK_COLOR(RUST, 0, "// note",            "CCCCCCC");            CHECK(st == 0);
    /* the doc forms are '//' lines too */
    st = CHECK_COLOR(RUST, 0, "/// doc",            "CCCCCCC");            CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "//! inner",          "CCCCCCCCC");          CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "let x = 1; // tail", "KKK.....N..CCCCCCC"); CHECK(st == 0);
    /* a block comment over three lines */
    st = CHECK_COLOR(RUST, 0, "/* open",            "CCCCCCC");            CHECK(st == 1);
    st = CHECK_COLOR(RUST, 1, "middle",             "CCCCCC");             CHECK(st == 1);
    st = CHECK_COLOR(RUST, 1, "end */ x;",          "CCCCCC...");          CHECK(st == 0);
    /* a line that ends on the first byte of the close sequence */
    st = CHECK_COLOR(RUST, 0, "/* note *",          "CCCCCCCCC");          CHECK(st == 2);
    st = CHECK_COLOR(RUST, 2, "/ more",             "C.....");             CHECK(st == 0);
    /* a nested comment closes at the first close, as documented */
    st = CHECK_COLOR(RUST, 0, "/* a /* b */ c */",  "CCCCCCCCCCCC.....");  CHECK(st == 0);
}

/* ---- attributes --------------------------------------------------------- */

static void
test_attributes(void)
{
    int st;

    st = CHECK_COLOR(RUST, 0, "#[derive(Debug)]",   "PPPPPPPPPPPPPPPP");   CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "#![allow(dead)]",    "PPPPPPPPPPPPPPP");    CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "#[test]",            "PPPPPPP");            CHECK(st == 0);
    /* an attribute is coloured wherever it sits, not only at line start */
    st = CHECK_COLOR(RUST, 0, "    #[inline]",      "....PPPPPPPPP");      CHECK(st == 0);
    /* code after the attribute is still scanned */
    st = CHECK_COLOR(RUST, 0, "#[test] fn f() {}",  "PPPPPPP.KK.......");  CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(RUST, 0, "42",                 "NN");                 CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "3.14",               "NNNN");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "1e10",               "NNNN");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "0xff",               "NNNN");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "0o17",               "NNNN");               CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "0b1010",             "NNNNNN");             CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "1_000_000",          "NNNNNNNNN");          CHECK(st == 0);
    /* a type suffix belongs to the literal */
    st = CHECK_COLOR(RUST, 0, "1u8",                "NNN");                CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "2.0f32",             "NNNNNN");             CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "9usize",             "NNNNNN");             CHECK(st == 0);
    st = CHECK_COLOR(RUST, 0, "const K: f64 = 1.5e-3;",
                              "KKKKK....KKK...NNNNNN.");                   CHECK(st == 0);
    /* a range keeps its dots out of the literal */
    st = CHECK_COLOR(RUST, 0, "for i in 0..10 {",   "KKK...KK.N..NN..");   CHECK(st == 0);
    /* a digit inside a name is part of the name */
    st = CHECK_COLOR(RUST, 0, "let a1 = 2;",        "KKK......N.");        CHECK(st == 0);
}

/* ---- file name matching ------------------------------------------------- */

static void
test_colorizer_find(void)
{
    CHECK(colorizer_find("main.rs") == &colorizer_rust);
    CHECK(colorizer_find("src/lib.rs") == &colorizer_rust);
    CHECK(colorizer_find("/home/u/p/src/bin/tool.rs") == &colorizer_rust);
    /* case is ignored */
    CHECK(colorizer_find("MAIN.RS") == &colorizer_rust);
    /* a bare name with no extension does not match */
    CHECK(colorizer_find("rs") == NULL);
    /* other languages still resolve */
    CHECK(colorizer_find("main.c") != &colorizer_rust);
    CHECK(colorizer_find("Cargo.toml") != &colorizer_rust);
}

/* ---- tab width ---------------------------------------------------------- */

static void
test_tabstop(void)
{
    /* rustfmt indents with four columns */
    CHECK(colorizer_rust.tabstop == 4);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_keywords();
    test_strings();
    test_chars_and_lifetimes();
    test_comments();
    test_attributes();
    test_numbers();
    test_colorizer_find();
    test_tabstop();
    SUMMARY();
}
