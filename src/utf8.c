/* utf-8 stepping helpers. */

#include "utf8.h"

static int
iscont(unsigned char c)
{
	/*
	 * == Detect a UTF-8 continuation byte ==
	 *
	 * Returns non-zero when c has the bit pattern 10xxxxxx, meaning it is
	 * the 2nd, 3rd, or 4th byte of a multi-byte sequence — not a lead byte.
	 */
	return (c & 0xC0) == 0x80;
}

static size_t
u8len(unsigned char c)
{
	/*
	 * == Byte length of a UTF-8 sequence from its lead byte ==
	 *
	 * Returns 1–4 based on the high bits of c.  Returns 1 for continuation
	 * bytes and other invalid lead bytes so the caller always advances by at
	 * least one byte.
	 */
	if (c < 0x80)
		return 1;
	if ((c & 0xE0) == 0xC0)
		return 2;
	if ((c & 0xF0) == 0xE0)
		return 3;
	if ((c & 0xF8) == 0xF0)
		return 4;
	return 1;
}

const char *
stepfwd(const char *p, const char *e)
{
	/*
	 * == Step one UTF-8 codepoint forward ==
	 *
	 * Advances p by one codepoint within [p, e).  Validates that the
	 * expected continuation bytes are present; falls back to a one-byte
	 * step when the sequence is malformed or truncated at e.
	 *
	 * - Returns e when p >= e.
	 * - Never returns a pointer past e.
	 */
	const unsigned char *q;
	size_t len;
	size_t i;

	if (!p)
		return NULL;
	if (!e || p >= e)
		return e ? e : p;

	q = (const unsigned char *)p;
	len = u8len(*q);
	if ((size_t)(e - p) < len)
		return p + 1;

	for (i = 1; i < len; i++) {
		if (!iscont(q[i]))
			return p + 1;
	}

	return p + len;
}

const char *
stepbwd(const char *p, const char *s)
{
	/*
	 * == Step one UTF-8 codepoint backward ==
	 *
	 * Retreats p to the lead byte of the codepoint that precedes p by
	 * scanning back over continuation bytes (10xxxxxx).  Stops at s.
	 *
	 * - Returns s when p <= s.
	 */
	const unsigned char *q;
	const unsigned char *b;

	if (!s)
		return NULL;
	if (!p || p <= s)
		return s;

	b = (const unsigned char *)s;
	q = (const unsigned char *)p;
	for (--q; q > b && iscont(*q); --q)
		;
	return (const char *)q;
}
