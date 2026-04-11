/*
 * test_utf8.c - unit tests for stepfwd() and stepbwd() in utf8.c.
 *
 * Each test_* function focuses on one aspect of the stepping behaviour.
 * All edge cases for the buffer boundary and malformed sequences are covered
 * because codepoint navigation is the foundation of every cursor motion.
 */
#include "testutil.h"
#include "utf8.h"
#include <stddef.h>

/* ---- stepfwd ------------------------------------------------------------ */

static void
test_stepfwd_ascii(void)
{
    const char *s = "abc";
    const char *e = s + 3;

    CHECK(stepfwd(s,     e) == s + 1);
    CHECK(stepfwd(s + 1, e) == s + 2);
    CHECK(stepfwd(s + 2, e) == s + 3);
}

static void
test_stepfwd_2byte(void)
{
    /* U+00E9 LATIN SMALL LETTER E WITH ACUTE: 0xC3 0xA9 */
    const char s[] = "\xc3\xa9!";
    const char *e  = s + 3;

    CHECK(stepfwd(s,     e) == s + 2); /* step over é */
    CHECK(stepfwd(s + 2, e) == s + 3); /* step over ! */
}

static void
test_stepfwd_3byte(void)
{
    /* U+4E2D CJK UNIFIED IDEOGRAPH 中: 0xE4 0xB8 0xAD */
    const char s[] = "\xe4\xb8\xad!";
    const char *e  = s + 4;

    CHECK(stepfwd(s,     e) == s + 3); /* step over 中 */
    CHECK(stepfwd(s + 3, e) == s + 4); /* step over ! */
}

static void
test_stepfwd_4byte(void)
{
    /* U+1F600 GRINNING FACE 😀: 0xF0 0x9F 0x98 0x80 */
    const char s[] = "\xf0\x9f\x98\x80!";
    const char *e  = s + 5;

    CHECK(stepfwd(s,     e) == s + 4); /* step over 😀 */
    CHECK(stepfwd(s + 4, e) == s + 5); /* step over ! */
}

static void
test_stepfwd_at_end(void)
{
    const char s[] = "a";
    const char *e  = s + 1;

    /* p == e: return e */
    CHECK(stepfwd(e, e) == e);

    /* p beyond e: still returns e */
    CHECK(stepfwd(e + 1, e) == e);
}

static void
test_stepfwd_null_end(void)
{
    const char s[] = "a";

    /* NULL end pointer: return p unchanged */
    CHECK(stepfwd(s, NULL) == s);
}

static void
test_stepfwd_continuation_as_lead(void)
{
    /* 0x80 is a continuation byte, not a valid lead byte: advance by 1 */
    const char s[] = "\x80\x80";
    const char *e  = s + 2;

    CHECK(stepfwd(s, e) == s + 1);
}

static void
test_stepfwd_truncated_sequence(void)
{
    /* 2-byte lead (0xC3) but end is only 1 byte away: advance by 1 */
    const char s[] = "\xc3";
    const char *e  = s + 1;

    CHECK(stepfwd(s, e) == s + 1);

    /* 3-byte lead, only 2 bytes available */
    const char t[] = "\xe4\xb8";
    const char *te = t + 2;

    CHECK(stepfwd(t, te) == t + 1);
}

static void
test_stepfwd_bad_continuation(void)
{
    /* 2-byte lead followed by ASCII (not a continuation byte): advance by 1 */
    const char s[] = "\xc3\x41"; /* 0xC3 then 'A' */
    const char *e  = s + 2;

    CHECK(stepfwd(s, e) == s + 1);
}

/* ---- stepbwd ------------------------------------------------------------ */

static void
test_stepbwd_ascii(void)
{
    const char *s = "abc";

    CHECK(stepbwd(s + 3, s) == s + 2);
    CHECK(stepbwd(s + 2, s) == s + 1);
    CHECK(stepbwd(s + 1, s) == s);
}

static void
test_stepbwd_2byte(void)
{
    /* é (0xC3 0xA9) followed by '!' */
    const char s[] = "\xc3\xa9!";

    CHECK(stepbwd(s + 2, s) == s);     /* step back over é */
    CHECK(stepbwd(s + 3, s) == s + 2); /* step back over ! */
}

static void
test_stepbwd_3byte(void)
{
    /* 中 (0xE4 0xB8 0xAD) followed by '!' */
    const char s[] = "\xe4\xb8\xad!";

    CHECK(stepbwd(s + 3, s) == s);     /* step back over 中 */
    CHECK(stepbwd(s + 4, s) == s + 3); /* step back over ! */
}

static void
test_stepbwd_4byte(void)
{
    /* 😀 (0xF0 0x9F 0x98 0x80) followed by '!' */
    const char s[] = "\xf0\x9f\x98\x80!";

    CHECK(stepbwd(s + 4, s) == s);     /* step back over 😀 */
    CHECK(stepbwd(s + 5, s) == s + 4); /* step back over ! */
}

static void
test_stepbwd_at_start(void)
{
    const char *s = "a";

    /* p == s: return s */
    CHECK(stepbwd(s, s) == s);
}

static void
test_stepbwd_null_start(void)
{
    const char s[] = "a";

    /* NULL start pointer: return NULL */
    CHECK(stepbwd(s + 1, NULL) == NULL);
}

static void
test_stepbwd_mixed(void)
{
    /* ASCII 'x', then 中, then ASCII 'y' */
    const char s[] = "x\xe4\xb8\xady";
    const char *e  = s + 5;

    CHECK(stepbwd(e,     s) == s + 4); /* back over 'y' */
    CHECK(stepbwd(s + 4, s) == s + 1); /* back over 中 */
    CHECK(stepbwd(s + 1, s) == s);     /* back over 'x' */
}

/* ---- round-trip --------------------------------------------------------- */

static void
test_roundtrip(void)
{
    /* Step forward then backward should return to the same position. */
    const char s[] = "a\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80z";
    const char *e  = s + sizeof(s) - 1; /* exclude NUL */
    const char *p  = s;

    /* collect forward positions */
    const char *pos[6];
    int n = 0;
    while (p < e)  { pos[n++] = p; p = stepfwd(p, e); }
    CHECK(n == 5); /* a, é, 中, 😀, z */

    /* step backward and verify positions in reverse */
    p = e;
    p = stepbwd(p, s); CHECK(p == pos[4]);
    p = stepbwd(p, s); CHECK(p == pos[3]);
    p = stepbwd(p, s); CHECK(p == pos[2]);
    p = stepbwd(p, s); CHECK(p == pos[1]);
    p = stepbwd(p, s); CHECK(p == pos[0]);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_stepfwd_ascii();
    test_stepfwd_2byte();
    test_stepfwd_3byte();
    test_stepfwd_4byte();
    test_stepfwd_at_end();
    test_stepfwd_null_end();
    test_stepfwd_continuation_as_lead();
    test_stepfwd_truncated_sequence();
    test_stepfwd_bad_continuation();
    test_stepbwd_ascii();
    test_stepbwd_2byte();
    test_stepbwd_3byte();
    test_stepbwd_4byte();
    test_stepbwd_at_start();
    test_stepbwd_null_start();
    test_stepbwd_mixed();
    test_roundtrip();
    SUMMARY();
}
