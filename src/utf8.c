/* utf-8 stepping helpers. */

#include "utf8.h"

static int
iscont(unsigned char c)
{
	return (c & 0xC0) == 0x80;
}

static size_t
u8len(unsigned char c)
{
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
