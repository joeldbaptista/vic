/*
 * color.c - colorizer registry and extension lookup.
 *
 * colorizer_find extracts the dot-extension from a filename and does
 * a linear scan through colorizer_table.  The table is short (one entry
 * per supported language), so the scan is O(n) on extensions, not files.
 *
 * To add a new language colorizer: declare it extern and add its address
 * to colorizer_table[].
 */
#include "color.h"
#include <ctype.h>
#include <string.h>

/* Built-in colorizers — each defined in its own color_<lang>.c */
extern const struct colorizer colorizer_c;
extern const struct colorizer colorizer_cpp;
extern const struct colorizer colorizer_sh;
extern const struct colorizer colorizer_md;
extern const struct colorizer colorizer_sql;
extern const struct colorizer colorizer_py;

static const struct colorizer *const colorizer_table[] = {
    &colorizer_c,
    &colorizer_cpp,
    &colorizer_sh,
    &colorizer_md,
    &colorizer_sql,
    &colorizer_py,
    NULL,
};

/* Case-insensitive extension comparison (extensions include leading dot). */
static int
ext_match(const char *a, const char *b)
{
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

const struct colorizer *
colorizer_find(const char *filename)
{
	const char *ext;
	int i;

	if (!filename)
		return NULL;
	ext = strrchr(filename, '.');
	if (!ext)
		return NULL;

	for (i = 0; colorizer_table[i]; i++) {
		const char *const *e;
		for (e = colorizer_table[i]->extensions; *e; e++) {
			if (ext_match(ext, *e))
				return colorizer_table[i];
		}
	}
	return NULL;
}
