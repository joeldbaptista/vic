/*
 * color.c - colorizer registry and file-name lookup.
 *
 * colorizer_find reduces a path to its base name and does a linear scan
 * through colorizer_table, matching on the dot-extension and on the base
 * name itself.  The table is short (one entry per supported language), so
 * the scan is O(n) on names, not files.
 *
 * To add a new language colorizer: declare it extern and add its address
 * to colorizer_table[].  A colorizer may match on extensions, on base
 * names, or on both.
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
extern const struct colorizer colorizer_docker;
extern const struct colorizer colorizer_yaml;
extern const struct colorizer colorizer_tf;

static const struct colorizer *const colorizer_table[] = {
    &colorizer_c,
    &colorizer_cpp,
    &colorizer_sh,
    &colorizer_md,
    &colorizer_sql,
    &colorizer_py,
    &colorizer_docker,
    &colorizer_yaml,
    &colorizer_tf,
    NULL,
};

static int
ext_match(const char *a, const char *b)
{
	/*
	 * == Case-insensitive comparison of two file extension strings ==
	 *
	 * Both a and b must include the leading dot (e.g. ".c", ".C").
	 * Returns 1 if they are identical under tolower(), 0 otherwise.
	 */
	while (*a && *b) {
		if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
			return 0;
		a++;
		b++;
	}
	return *a == '\0' && *b == '\0';
}

static int
base_match(const char *base, const char *name)
{
	/*
	 * == Case-insensitive match of a file base name against a table entry ==
	 *
	 * Returns 1 when base is name ("Dockerfile"), or name followed by a
	 * dotted suffix ("Dockerfile.dev"); 0 otherwise.  The second form is
	 * how a project distinguishes several Dockerfiles in one directory.
	 */
	while (*base && *name) {
		if (tolower((unsigned char)*base) != tolower((unsigned char)*name))
			return 0;
		base++;
		name++;
	}
	if (*name)
		return 0;
	return *base == '\0' || *base == '.';
}

const struct colorizer *
colorizer_find(const char *filename)
{
	/*
	 * == Find the colorizer for a file, based on its name ==
	 *
	 * Strips any directory prefix, then does a linear scan through
	 * colorizer_table.  Each colorizer is tested twice: its extensions[]
	 * list against the last '.' component of the base name, and its
	 * basenames[] list against the whole base name (a Dockerfile normally
	 * has no extension to match on).  Returns the matching colorizer, or
	 * NULL when nothing matches.
	 */
	const char *base, *slash, *ext;
	int i;

	if (!filename)
		return NULL;
	slash = strrchr(filename, '/');
	base = slash ? slash + 1 : filename;
	ext = strrchr(base, '.');

	for (i = 0; colorizer_table[i]; i++) {
		const char *const *e;

		if (ext) {
			for (e = colorizer_table[i]->extensions; *e; e++) {
				if (ext_match(ext, *e))
					return colorizer_table[i];
			}
		}
		if (colorizer_table[i]->basenames) {
			for (e = colorizer_table[i]->basenames; *e; e++) {
				if (base_match(base, *e))
					return colorizer_table[i];
			}
		}
	}
	return NULL;
}
