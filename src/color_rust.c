/*
 * color_rust.c - syntax colorizer for Rust.
 *
 * One colorizer is exported: colorizer_rust (.rs).  It owns only the
 * keyword table and the struct lang_spec below; the scanning is done by
 * the generic engine in color_generic.c.
 *
 * Recognised tokens:
 *   Keywords      — the strict and reserved keywords, plus the primitive
 *                   type names (i32, f64, bool, str, ...), which the
 *                   language builds in
 *   Numbers       — decimal, hex (0x), octal (0o), binary (0b), float with
 *                   exponent, '_' digit separators, and type suffixes
 *                   (1u8, 2.0f32, 1_000usize)
 *   Attributes    — #[...] and #![...]
 *   Line comments — // ..., and so also the doc forms /// and //!
 *   Block comments  (slash-star ... star-slash), spanning lines
 *   String literals "...", b"...", r"..." — these may span lines
 *   Character literals '...' and b'...'
 *
 * Lifetimes and loop labels ('a, 'static, 'outer) are left uncoloured: a
 * quote opens a character literal only when one character and its closing
 * quote follow it, so 'a reads as a lifetime, while 'a' is a character.
 *
 * Library names are not keywords to Rust, so Option, Result, Some, None,
 * Ok, Err, Vec and String are coloured as the ordinary names they are.
 *
 * Two constructs are approximated, in both cases erring towards leaving
 * text uncoloured:
 *   - A nested block comment closes at the first star-slash, so the text
 *     from there to the outer close is scanned as code.
 *   - A hashed raw string (r#"..."#) is scanned as an ordinary string: the
 *     hashes colour as an attribute sigil, and a quote inside the body
 *     ends the string early.
 *
 * Cross-line state: see color_generic.h.  Block comments use
 * CS_BLOCK_CMT / CS_BLOCK_CMT_STAR, and a "..." left open at the end of a
 * line uses CS_STRING_BASE.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

/*
 * The strict keywords of the 2015, 2018 and 2021 editions, the weak
 * keywords (union, macro_rules), the reserved words the language keeps for
 * itself, and the primitive type names (sorted).
 */
static const char *const rust_keywords[] = {
    "Self",
    "abstract",
    "as",
    "async",
    "await",
    "become",
    "bool",
    "box",
    "break",
    "char",
    "const",
    "continue",
    "crate",
    "do",
    "dyn",
    "else",
    "enum",
    "extern",
    "f32",
    "f64",
    "false",
    "final",
    "fn",
    "for",
    "i128",
    "i16",
    "i32",
    "i64",
    "i8",
    "if",
    "impl",
    "in",
    "isize",
    "let",
    "loop",
    "macro",
    "macro_rules",
    "match",
    "mod",
    "move",
    "mut",
    "override",
    "priv",
    "pub",
    "ref",
    "return",
    "self",
    "static",
    "str",
    "struct",
    "super",
    "trait",
    "true",
    "try",
    "type",
    "typeof",
    "u128",
    "u16",
    "u32",
    "u64",
    "u8",
    "union",
    "unsafe",
    "unsized",
    "use",
    "usize",
    "virtual",
    "where",
    "while",
    "yield",
    NULL,
};

/*
 * "..." is backslash-escaped and may span lines, which is why cross_line
 * is set; the prefix letters cover b"..." and r"...".  '...' is a
 * character literal, so char_literal gates it: without that gate every
 * lifetime would open a string that runs to the end of the line.
 */
static const struct str_spec rust_strings[] = {
    {.open = '"', .escape = 1, .cross_line = 1, .prefixable = 1},
    {.open = '\'', .escape = 1, .prefixable = 1, .char_literal = 1},
    {0},
};

static const struct lang_spec rust_spec = {
    .keywords = rust_keywords,
    .line_comment = "//",
    .block_open = "/*",
    .block_close = "*/",
    .strings = rust_strings,
    /*
     * The suffix set holds the letters and digits a Rust numeric suffix is
     * built from (u8, i32, usize, f64), and dot_needs_digit keeps a range
     * expression such as 0..n from swallowing its first dot.
     */
    .num = {.hex = 1,
            .octal = 1,
            .binary = 1,
            .underscore_sep = 1,
            .int_suffixes = "iufsze0123456789",
            .dot_needs_digit = 1},
    .decorator_char = '#',
    .decorator_bracket = 1,
    .label_char = '\'',
};

static int
rust_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of Rust source ==
	 */
	return colorize_generic(state, line, len, attrs, &rust_spec);
}

static const char *const rust_extensions[] = {".rs", NULL};

/* rustfmt indents with four columns, so the file type declares 4. */
const struct colorizer colorizer_rust = {"rust", rust_extensions, NULL,
                                         rust_colorize, 4};
