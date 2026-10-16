/*
 * test_parser.c - unit tests for parser.c
 *
 * parser.c is the Normal-mode command parser: a state machine fed one
 * key at a time that accumulates a register prefix, a count, an
 * operator, a second key, a range count, a range key, an anchor and a
 * string payload.  It includes nothing but parser.h, so this test links
 * parser.c alone and needs no stubs.  vic.h is included only for the
 * KEYCODE_* names; nothing from it is linked.
 *
 * parse() returns 0 while it wants more input and 1 when the command is
 * finished; s->ok then says whether what it parsed was a valid command.
 * A command is therefore tested by feeding its keys in order and
 * checking both how many keys it took and the fields it filled in.
 *
 * The field names come from the MONRAS model named in parser.h:
 *   reg  register prefix        m    operator count
 *   op   operator               op2  second key of a two-key operator
 *   n    range count            rg   range (motion) key
 *   a    anchor byte            b[]  string payload for : / ?
 *   raw_key  a key with no ASCII operator, such as an arrow key
 *
 * A count of 0 is indistinguishable from no count, because m and n both
 * start at 0.  That is the parser's own convention: cmd_ctx_from_parser
 * in vic.c turns 0 into 1.
 */
#include "testutil.h"
#include "parser.h"
#include "vic.h"
#include <string.h>

static struct parser s;

/*
 * Feed n bytes of keys into a fresh parser, one at a time, stopping as
 * soon as parse() reports completion.  Returns the number of keys
 * consumed, or -1 when the parser wanted more than were supplied.
 * The length is explicit because '\0' is a meaningful key: it terminates
 * a string payload and rejects an anchor.
 */
static int
feed_n(const char *keys, size_t n)
{
    size_t i;

    initparser(&s);
    for (i = 0; i < n; i++)
        if (parse(&s, (unsigned char)keys[i]) == 1)
            return (int)i + 1;
    return -1;
}

/* The same for a NUL-terminated sequence of ordinary keys. */
static int
feed(const char *keys)
{
    return feed_n(keys, strlen(keys));
}

/* ---- initparser ---------------------------------------------------------- */

static void
test_initparser(void)
{
    memset(&s, 0xff, sizeof(s));
    CHECK(initparser(&s) == 0);
    /* Every field starts empty, so a half-typed command cannot leak into
     * the next one. */
    CHECK(s.reg == '\0');
    CHECK(s.m == 0);
    CHECK(s.op == '\0');
    CHECK(s.op2 == '\0');
    CHECK(s.n == 0);
    CHECK(s.rg == '\0');
    CHECK(s.a == '\0');
    CHECK(s.stg == STG_START);
    CHECK(s.ok == 0);
    CHECK(s.k == 0);
    CHECK(s.raw_key == 0);
    CHECK(s.b[0] == '\0');
}

static void
test_initparser_null(void)
{
    CHECK(initparser(NULL) == -1);
}

/* ---- single-key commands ------------------------------------------------- */

static void
test_simple_operators(void)
{
    /* The complete set of keys that finish a command on their own at the
     * operator stage. */
    static const char ops[] = "hjkl$wbexoOG"
                              "nN~JpPXsS"
                              "uU."
                              "%;,HLM|"
                              "IRvVBEWY"
                              "*#(){}-D ";
    size_t i;

    for (i = 0; i < sizeof(ops) - 1; i++) {
        char keys[2];

        keys[0] = ops[i];
        keys[1] = '\0';
        CHECK(feed(keys) == 1);
        CHECK(s.ok == 1);
        CHECK(s.op == ops[i]);
        /* No count given, so m stays 0. */
        CHECK(s.m == 0);
        CHECK(s.rg == '\0');
        CHECK(s.a == '\0');
    }
}

static void
test_start_stage_operators(void)
{
    /* These are recognised at the very first stage rather than at the
     * operator stage, and they set the count to 1 as they go. */
    static const char ops[] = "^iaAC";
    size_t i;

    for (i = 0; i < sizeof(ops) - 1; i++) {
        char keys[2];

        keys[0] = ops[i];
        keys[1] = '\0';
        CHECK(feed(keys) == 1);
        CHECK(s.ok == 1);
        CHECK(s.op == ops[i]);
        CHECK(s.m == 1);
    }
}

static void
test_zero_is_a_motion_not_a_count(void)
{
    /* A leading '0' is "go to the start of the line", so it completes at
     * once and leaves the count unset.  That is what lets vic.c tell it
     * apart from the 0 in a count like 10j. */
    CHECK(feed("0") == 1);
    CHECK(s.ok == 1);
    CHECK(s.op == '0');
    CHECK(s.m == 0);
}

/* ---- counts -------------------------------------------------------------- */

static void
test_count_before_an_operator(void)
{
    CHECK(feed("3x") == 2);
    CHECK(s.ok == 1);
    CHECK(s.op == 'x');
    CHECK(s.m == 3);
}

static void
test_multi_digit_count(void)
{
    CHECK(feed("12j") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'j');
    CHECK(s.m == 12);

    CHECK(feed("100G") == 4);
    CHECK(s.m == 100);
}

static void
test_zero_inside_a_count(void)
{
    /* Only a *leading* zero is the motion; after a non-zero digit it is
     * an ordinary digit. */
    CHECK(feed("10x") == 3);
    CHECK(s.m == 10);
    CHECK(s.op == 'x');
}

/* ---- operators that take a range ---------------------------------------- */

static void
test_operator_with_a_range(void)
{
    /* The complete set of operators that wait for a motion. */
    static const char ops[] = "dyc<>!";
    size_t i;

    for (i = 0; i < sizeof(ops) - 1; i++) {
        char keys[3];

        keys[0] = ops[i];
        keys[1] = 'w';
        keys[2] = '\0';
        CHECK(feed(keys) == 2);
        CHECK(s.ok == 1);
        CHECK(s.op == ops[i]);
        CHECK(s.rg == 'w');
    }
}

static void
test_doubled_operator(void)
{
    /* dd, yy, cc: the operator's own key is a valid range. */
    CHECK(feed("dd") == 2);
    CHECK(s.ok == 1 && s.op == 'd' && s.rg == 'd');

    CHECK(feed("yy") == 2);
    CHECK(s.ok == 1 && s.op == 'y' && s.rg == 'y');

    CHECK(feed("cc") == 2);
    CHECK(s.ok == 1 && s.op == 'c' && s.rg == 'c');

    CHECK(feed(">>") == 2);
    CHECK(s.ok == 1 && s.op == '>' && s.rg == '>');
}

static void
test_range_counts(void)
{
    /* d3j: the 3 belongs to the motion, not to the operator. */
    CHECK(feed("d3j") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'd');
    CHECK(s.n == 3);
    CHECK(s.m == 0);
    CHECK(s.rg == 'j');
}

static void
test_both_counts(void)
{
    /* 2d3j: both counts are kept, and vic.c multiplies them. */
    CHECK(feed("2d3j") == 4);
    CHECK(s.ok == 1);
    CHECK(s.m == 2);
    CHECK(s.n == 3);
    CHECK(s.op == 'd');
    CHECK(s.rg == 'j');
}

static void
test_linewise_ranges(void)
{
    /* The linewise motions, including both Enter spellings. */
    static const char rgs[] = "GHL+-jk\r\n";
    size_t i;

    for (i = 0; i < sizeof(rgs) - 1; i++) {
        char keys[3];

        keys[0] = 'd';
        keys[1] = rgs[i];
        keys[2] = '\0';
        CHECK(feed_n(keys, 2) == 2);
        CHECK(s.ok == 1);
        CHECK(s.rg == rgs[i]);
    }
}

static void
test_characterwise_ranges(void)
{
    static const char rgs[] = "wWeEbB^$%hn|{} l";
    size_t i;

    for (i = 0; i < sizeof(rgs) - 1; i++) {
        char keys[3];

        keys[0] = 'd';
        keys[1] = rgs[i];
        keys[2] = '\0';
        CHECK(feed_n(keys, 2) == 2);
        CHECK(s.ok == 1);
        CHECK(s.rg == rgs[i]);
    }
}

static void
test_backspace_and_delete_ranges(void)
{
    char keys[2];

    keys[0] = 'd';
    keys[1] = 0x08; /* BS */
    CHECK(feed_n(keys, 2) == 2);
    CHECK(s.ok == 1 && s.rg == 0x08);

    keys[1] = 0x7f; /* DEL */
    CHECK(feed_n(keys, 2) == 2);
    CHECK(s.ok == 1 && s.rg == 0x7f);
}

static void
test_zero_cannot_be_a_range(void)
{
    /*
     * DEFECT, pinned rather than endorsed.  '0' is listed as a valid
     * range key, but the range-count stage consumes digits first and
     * never passes a '0' through, so d0 leaves the parser waiting
     * instead of deleting to the start of the line.  Feeding a third key
     * shows where the '0' went: it was read as a count of zero.
     */
    CHECK(feed("d0") == -1); /* never completes */
    CHECK(feed("d0w") == 3);
    CHECK(s.ok == 1);
    CHECK(s.rg == 'w');
    CHECK(s.n == 0);
}

static void
test_unknown_range_is_rejected(void)
{
    CHECK(feed("dZ") == 2);
    CHECK(s.ok == 0);
    /* The operator was still captured, so the caller can report what was
     * being attempted. */
    CHECK(s.op == 'd');
    CHECK(s.rg == '\0');
}

/* ---- anchors ------------------------------------------------------------- */

static void
test_find_char_anchors(void)
{
    /* f F t T and r each take exactly one more key. */
    static const char ops[] = "fFtTr";
    size_t i;

    for (i = 0; i < sizeof(ops) - 1; i++) {
        char keys[3];

        keys[0] = ops[i];
        keys[1] = 'x';
        keys[2] = '\0';
        CHECK(feed(keys) == 2);
        CHECK(s.ok == 1);
        CHECK(s.op == ops[i]);
        CHECK(s.a == 'x');
    }
}

static void
test_mark_anchors(void)
{
    /* m sets a mark and ' jumps to one; both need the mark's name. */
    CHECK(feed("ma") == 2);
    CHECK(s.ok == 1 && s.op == 'm' && s.a == 'a');

    CHECK(feed("'a") == 2);
    CHECK(s.ok == 1 && s.op == '\'' && s.a == 'a');
}

static void
test_range_anchors(void)
{
    /* dfx: the anchor belongs to the motion this time. */
    CHECK(feed("dfx") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'd' && s.rg == 'f' && s.a == 'x');

    /* d'a: delete to a mark. */
    CHECK(feed("d'a") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'd' && s.rg == '\'' && s.a == 'a');
}

static void
test_text_object_anchors(void)
{
    /* diw and da( — the i/a is the range and the object is the anchor. */
    CHECK(feed("diw") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'd' && s.rg == 'i' && s.a == 'w');

    CHECK(feed("da(") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'd' && s.rg == 'a' && s.a == '(');

    CHECK(feed("ci\"") == 3);
    CHECK(s.ok == 1);
    CHECK(s.op == 'c' && s.rg == 'i' && s.a == '"');
}

static void
test_text_object_with_counts(void)
{
    CHECK(feed("2d3i(") == 5);
    CHECK(s.ok == 1);
    CHECK(s.m == 2 && s.n == 3);
    CHECK(s.op == 'd' && s.rg == 'i' && s.a == '(');
}

static void
test_anchor_cannot_be_nul(void)
{
    /* A NUL where an anchor was expected means the key was never
     * delivered, so the command is rejected rather than taking '\0' as
     * the target character. */
    CHECK(feed_n("f\0", 2) == 2);
    CHECK(s.ok == 0);
    CHECK(s.a == '\0');
}

static void
test_anchor_accepts_any_other_byte(void)
{
    /* f takes whatever byte follows, punctuation and digits included. */
    CHECK(feed("f;") == 2);
    CHECK(s.ok == 1 && s.a == ';');

    CHECK(feed("f5") == 2);
    CHECK(s.ok == 1 && s.a == '5');

    CHECK(feed("f ") == 2);
    CHECK(s.ok == 1 && s.a == ' ');
}

/* ---- two-key operators --------------------------------------------------- */

static void
test_g_prefix(void)
{
    /* g accepts g, * and #. */
    CHECK(feed("gg") == 2);
    CHECK(s.ok == 1 && s.op == 'g' && s.op2 == 'g');

    CHECK(feed("g*") == 2);
    CHECK(s.ok == 1 && s.op2 == '*');

    CHECK(feed("g#") == 2);
    CHECK(s.ok == 1 && s.op2 == '#');
}

static void
test_g_prefix_rejects_others(void)
{
    CHECK(feed("gq") == 2);
    CHECK(s.ok == 0);
    CHECK(s.op == 'g');
    CHECK(s.op2 == '\0');
}

static void
test_z_prefix(void)
{
    static const char seconds[] = "ztb.-";
    size_t i;

    for (i = 0; i < sizeof(seconds) - 1; i++) {
        char keys[3];

        keys[0] = 'z';
        keys[1] = seconds[i];
        keys[2] = '\0';
        CHECK(feed(keys) == 2);
        CHECK(s.ok == 1);
        CHECK(s.op2 == seconds[i]);
    }
}

static void
test_z_accepts_enter(void)
{
    /* z is the one prefix that also takes Enter, in either spelling. */
    CHECK(feed("z\r") == 2);
    CHECK(s.ok == 1 && s.op2 == '\r');

    CHECK(feed("z\n") == 2);
    CHECK(s.ok == 1 && s.op2 == '\n');
}

static void
test_other_prefixes_reject_enter(void)
{
    /* g and Z do not, which is what the per-prefix flag is for. */
    CHECK(feed("g\r") == 2);
    CHECK(s.ok == 0);

    CHECK(feed("Z\r") == 2);
    CHECK(s.ok == 0);
}

static void
test_zz_prefix(void)
{
    CHECK(feed("ZZ") == 2);
    CHECK(s.ok == 1 && s.op == 'Z' && s.op2 == 'Z');

    CHECK(feed("ZQ") == 2);
    CHECK(s.ok == 1 && s.op2 == 'Q');

    CHECK(feed("Zx") == 2);
    CHECK(s.ok == 0);
}

static void
test_two_key_with_a_count(void)
{
    CHECK(feed("5gg") == 3);
    CHECK(s.ok == 1);
    CHECK(s.m == 5 && s.op == 'g' && s.op2 == 'g');
}

/* ---- string payloads: : / ? --------------------------------------------- */

static void
test_ex_command_string(void)
{
    /* The string runs until a NUL, which the caller sends when the user
     * presses Enter. */
    CHECK(feed_n(":wq\0", 4) == 4);
    CHECK(s.ok == 1);
    CHECK(s.op == ':');
    CHECK(s.m == 1);
    CHECK(strcmp(s.b, "wq") == 0);
}

static void
test_search_strings(void)
{
    CHECK(feed_n("/foo\0", 5) == 5);
    CHECK(s.ok == 1);
    CHECK(s.op == '/');
    CHECK(strcmp(s.b, "foo") == 0);

    CHECK(feed_n("?bar\0", 5) == 5);
    CHECK(s.ok == 1);
    CHECK(s.op == '?');
    CHECK(strcmp(s.b, "bar") == 0);
}

static void
test_empty_string_payload(void)
{
    /* Enter straight after the prompt character: a valid command with an
     * empty payload, which search.c reads as "repeat the last pattern". */
    CHECK(feed_n(":\0", 2) == 2);
    CHECK(s.ok == 1);
    CHECK(s.op == ':');
    CHECK(s.b[0] == '\0');

    CHECK(feed_n("/\0", 2) == 2);
    CHECK(s.ok == 1);
    CHECK(s.b[0] == '\0');
}

static void
test_string_keeps_spaces_and_punctuation(void)
{
    CHECK(feed_n(":s/a b/c d/g\0", 13) == 13);
    CHECK(s.ok == 1);
    CHECK(strcmp(s.b, "s/a b/c d/g") == 0);
}

static void
test_string_is_truncated_not_overrun(void)
{
    char keys[300];
    int i;
    int r;

    /* The payload buffer is 128 bytes.  A longer command is cut to fit
     * and stays NUL-terminated rather than running off the end. */
    keys[0] = ':';
    for (i = 1; i < 250; i++)
        keys[i] = 'x';
    keys[250] = '\0';
    r = feed_n(keys, 251);
    CHECK(r == 251);
    CHECK(s.ok == 1);
    CHECK(strlen(s.b) == sizeof(s.b) - 1);
    CHECK(s.b[sizeof(s.b) - 1] == '\0');
}

static void
test_search_is_not_accepted_as_a_range(void)
{
    /*
     * DEFECT, pinned rather than endorsed.  d/pattern deletes up to the
     * next match in vi, and range.c does classify '/' and '?' as range
     * keys (it gives them buftype MULTI).  The parser's range set omits
     * both, so the command is rejected after two keys and range.c's
     * handling for them is unreachable through the parser.
     */
    CHECK(feed("d/") == 2);
    CHECK(s.ok == 0);
    CHECK(s.op == 'd');
    CHECK(s.rg == '\0');

    CHECK(feed("d?") == 2);
    CHECK(s.ok == 0);

    /* The same keys as whole commands are fine; only the range use is
     * refused. */
    CHECK(feed_n("/foo\0", 5) == 5);
    CHECK(s.ok == 1 && s.op == '/');
}

/* ---- registers ----------------------------------------------------------- */

static void
test_register_prefix(void)
{
    CHECK(feed("\"ayy") == 4);
    CHECK(s.ok == 1);
    CHECK(s.reg == 'a');
    CHECK(s.op == 'y' && s.rg == 'y');
}

static void
test_register_names(void)
{
    /* Letters, digits and the named specials are all accepted. */
    static const char names[] = "azAZ09\"-+#*:.%/";
    size_t i;

    for (i = 0; i < sizeof(names) - 1; i++) {
        char keys[4];

        keys[0] = '"';
        keys[1] = names[i];
        keys[2] = 'p';
        keys[3] = '\0';
        CHECK(feed(keys) == 3);
        CHECK(s.ok == 1);
        CHECK(s.reg == names[i]);
        CHECK(s.op == 'p');
    }
}

static void
test_register_rejects_other_characters(void)
{
    CHECK(feed("\"!p") == 2);
    CHECK(s.ok == 0);
    CHECK(s.reg == '\0');
}

static void
test_register_cannot_be_nul(void)
{
    CHECK(feed_n("\"\0", 2) == 2);
    CHECK(s.ok == 0);
}

static void
test_plus_is_a_register_shorthand(void)
{
    /* +yy is "+yy without the quote. */
    CHECK(feed("+yy") == 3);
    CHECK(s.ok == 1);
    CHECK(s.reg == '+');
    CHECK(s.op == 'y' && s.rg == 'y');
}

static void
test_register_with_a_count(void)
{
    CHECK(feed("\"a3dd") == 5);
    CHECK(s.ok == 1);
    CHECK(s.reg == 'a');
    CHECK(s.m == 3);
    CHECK(s.op == 'd' && s.rg == 'd');
}

static void
test_register_then_zero_is_a_count(void)
{
    /*
     * The leading-zero motion is only recognised at the first stage, so
     * after a register prefix the parser is already counting and the
     * same '0' becomes a count digit.  "a0 therefore waits for an
     * operator instead of jumping to the line start.
     */
    CHECK(feed("\"a0") == -1);
    CHECK(feed("\"a0x") == 4);
    CHECK(s.ok == 1);
    CHECK(s.op == 'x');
    CHECK(s.m == 0);
}

/* ---- raw keys ------------------------------------------------------------ */

static void
test_control_keys_are_raw(void)
{
    /* A control character has no ASCII operator, so it is reported
     * through raw_key with op left empty. */
    int c;

    for (c = 0x01; c < 0x20; c++) {
        char keys[1];

        keys[0] = (char)c;
        CHECK(feed_n(keys, 1) == 1);
        CHECK(s.ok == 1);
        CHECK(s.raw_key == c);
        CHECK(s.op == '\0');
    }
}

static void
test_keycodes_are_raw(void)
{
    /* Every keycode the terminal layer actually produces.  The list is
     * exhaustive over the KEYCODE_* values in vic.h. */
    /* Every KEYCODE_* in vic.h except KEYCODE_CURSOR_POS, which the next
     * test covers because it is mis-parsed. */
    static const int codes[] = {
        KEYCODE_UP,      KEYCODE_DOWN,    KEYCODE_LEFT,   KEYCODE_RIGHT,
        KEYCODE_HOME,    KEYCODE_END,     KEYCODE_PAGEUP, KEYCODE_PAGEDOWN,
        KEYCODE_INSERT,  KEYCODE_DELETE,  KEYCODE_FUN1,   KEYCODE_FUN2,
        KEYCODE_FUN3,    KEYCODE_FUN4,    KEYCODE_FUN5,   KEYCODE_FUN6,
        KEYCODE_FUN7,    KEYCODE_FUN8,    KEYCODE_FUN9,   KEYCODE_FUN10,
        KEYCODE_FUN11,   KEYCODE_FUN12,
        KEYCODE_PASTE_BEGIN, KEYCODE_PASTE_END
    };
    size_t i;

    for (i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        initparser(&s);
        CHECK(parse(&s, codes[i]) == 1);
        CHECK(s.ok == 1);
        CHECK(s.raw_key == codes[i]);
        CHECK(s.op == '\0');
    }
}

static void
test_keycode_with_a_zero_low_byte(void)
{
    /*
     * DEFECT, pinned rather than endorsed.  in_set guards against a NUL
     * key by testing the int, but strchr still narrows its argument to
     * char.  A keycode whose low byte is zero therefore narrows to '\0'
     * and matches the terminator of every set it is tested against.  The
     * first such test is the one for "^iaAC" at the start stage, so the
     * key is taken for an operator: op is set to the narrowed '\0', the
     * count is set to 1, and raw_key is never filled in.
     *
     * KEYCODE_CURSOR_POS (0x0200) is affected, and so is the boundary
     * value PARSER_RAW_KEY_MIN (0x100) itself.  Reaching it needs a
     * cursor-position report to arrive while the edit loop is reading
     * keys: edit_file consumes the solicited one directly through
     * safe_read_key during the terminal-size query, so a report has to
     * be late or unsolicited to land in the parser.
     */
    initparser(&s);
    CHECK(parse(&s, KEYCODE_CURSOR_POS) == 1);
    CHECK(s.ok == 1);      /* should be a raw key, not a valid operator */
    CHECK(s.raw_key == 0); /* should be KEYCODE_CURSOR_POS */
    CHECK(s.op == '\0');
    CHECK(s.m == 1);       /* the start-stage operators set a count of 1 */

    initparser(&s);
    CHECK(parse(&s, PARSER_RAW_KEY_MIN) == 1);
    CHECK(s.raw_key == 0);
    CHECK(s.op == '\0');

    /*
     * The neighbouring values behave correctly, which isolates the cause
     * to the narrowing rather than to the range test itself.
     */
    initparser(&s);
    CHECK(parse(&s, KEYCODE_CURSOR_POS + 1) == 1);
    CHECK(s.ok == 1);
    CHECK(s.raw_key == KEYCODE_CURSOR_POS + 1);

    initparser(&s);
    CHECK(parse(&s, PARSER_RAW_KEY_MIN + 7) == 1);
    CHECK(s.ok == 1);
    CHECK(s.raw_key == PARSER_RAW_KEY_MIN + 7);
}

static void
test_raw_key_keeps_its_count(void)
{
    /* 3<Down> is three lines down, so the count must survive. */
    initparser(&s);
    CHECK(parse(&s, '3') == 0);
    CHECK(parse(&s, PARSER_RAW_KEY_MIN) == 1);
    CHECK(s.ok == 1);
    CHECK(s.m == 3);
    CHECK(s.raw_key == PARSER_RAW_KEY_MIN);
}

/* ---- rejected input ------------------------------------------------------ */

static void
test_unknown_operator(void)
{
    CHECK(feed("q") == 1);
    CHECK(s.ok == 0);
    CHECK(s.op == '\0');
}

static void
test_rejected_operator_moves_the_count(void)
{
    /*
     * On an unrecognised operator the parser moves the count it had
     * collected into n and sets m to -1.  vic.c uses that to tell a
     * genuinely countless command from a rejected one.
     */
    CHECK(feed("5q") == 2);
    CHECK(s.ok == 0);
    CHECK(s.n == 5);
    CHECK(s.m == -1);
}

static void
test_high_bytes_are_rejected(void)
{
    /* A UTF-8 lead byte is not a command: it is below the raw-key range
     * but matches no operator. */
    char keys[1];

    keys[0] = (char)0xc3;
    CHECK(feed_n(keys, 1) == 1);
    CHECK(s.ok == 0);
}

static void
test_nul_at_the_operator_stage(void)
{
    /* A NUL is not a control key here: the raw-key test requires c > 0,
     * so it falls through and is rejected. */
    CHECK(feed_n("\0", 1) == 1);
    CHECK(s.ok == 0);
    CHECK(s.raw_key == 0);
}

/* ---- the parser is reusable --------------------------------------------- */

static void
test_consecutive_commands(void)
{
    /* Each command starts from a clean state, so nothing from the first
     * leaks into the second. */
    CHECK(feed("\"a3dfx") == 6);
    CHECK(s.ok == 1);
    CHECK(s.reg == 'a' && s.m == 3 && s.op == 'd' && s.rg == 'f' && s.a == 'x');

    CHECK(feed("j") == 1);
    CHECK(s.ok == 1);
    CHECK(s.op == 'j');
    CHECK(s.reg == '\0');
    CHECK(s.m == 0);
    CHECK(s.rg == '\0');
    CHECK(s.a == '\0');
}

static void
test_partial_command_returns_zero(void)
{
    /* Every prefix of a multi-key command must ask for more input rather
     * than completing early. */
    initparser(&s);
    CHECK(parse(&s, '"') == 0);
    CHECK(parse(&s, 'a') == 0);
    CHECK(parse(&s, '3') == 0);
    CHECK(parse(&s, 'd') == 0);
    CHECK(parse(&s, '2') == 0);
    CHECK(parse(&s, 'i') == 0);
    CHECK(parse(&s, '(') == 1);
    CHECK(s.ok == 1);
    CHECK(s.reg == 'a' && s.m == 3 && s.op == 'd' && s.n == 2);
    CHECK(s.rg == 'i' && s.a == '(');
}

/* ---- in_set -------------------------------------------------------------- */

static void
test_in_set(void)
{
    CHECK(in_set('a', "abc") != 0);
    CHECK(in_set('c', "abc") != 0);
    CHECK(in_set('z', "abc") == 0);
    CHECK(in_set('a', "") == 0);
}

static void
test_in_set_rejects_nul(void)
{
    /*
     * strchr matches the terminator, so a bare strchr would report that
     * '\0' is a member of every set.  in_set exists to stop that, and
     * several stages rely on it to reject a missing key.
     */
    CHECK(in_set('\0', "abc") == 0);
    CHECK(in_set('\0', "") == 0);
}

int
main(void)
{
    test_initparser();
    test_initparser_null();

    test_simple_operators();
    test_start_stage_operators();
    test_zero_is_a_motion_not_a_count();

    test_count_before_an_operator();
    test_multi_digit_count();
    test_zero_inside_a_count();

    test_operator_with_a_range();
    test_doubled_operator();
    test_range_counts();
    test_both_counts();
    test_linewise_ranges();
    test_characterwise_ranges();
    test_backspace_and_delete_ranges();
    test_zero_cannot_be_a_range();
    test_unknown_range_is_rejected();

    test_find_char_anchors();
    test_mark_anchors();
    test_range_anchors();
    test_text_object_anchors();
    test_text_object_with_counts();
    test_anchor_cannot_be_nul();
    test_anchor_accepts_any_other_byte();

    test_g_prefix();
    test_g_prefix_rejects_others();
    test_z_prefix();
    test_z_accepts_enter();
    test_other_prefixes_reject_enter();
    test_zz_prefix();
    test_two_key_with_a_count();

    test_ex_command_string();
    test_search_strings();
    test_empty_string_payload();
    test_string_keeps_spaces_and_punctuation();
    test_string_is_truncated_not_overrun();
    test_search_is_not_accepted_as_a_range();

    test_register_prefix();
    test_register_names();
    test_register_rejects_other_characters();
    test_register_cannot_be_nul();
    test_plus_is_a_register_shorthand();
    test_register_with_a_count();
    test_register_then_zero_is_a_count();

    test_control_keys_are_raw();
    test_keycodes_are_raw();
    test_keycode_with_a_zero_low_byte();
    test_raw_key_keeps_its_count();

    test_unknown_operator();
    test_rejected_operator_moves_the_count();
    test_high_bytes_are_rejected();
    test_nul_at_the_operator_stage();

    test_consecutive_commands();
    test_partial_command_returns_zero();

    test_in_set();
    test_in_set_rejects_nul();

    SUMMARY();
}
