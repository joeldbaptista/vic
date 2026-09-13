/*
 * color_tf.c - syntax colorizer for Terraform (HCL) files.
 *
 * Recognised tokens:
 *   Keywords      — block types (resource, variable, module, ...), the
 *                   named values (var, local, each, count, path, self),
 *                   the meta-arguments (for_each, depends_on, lifecycle,
 *                   ...), the expression words (for, in, if, else) and
 *                   the constants true, false and null
 *   Interpolation — ${...} and $word — highlighted as ATTR_PREPROC
 *   Comments      — '#' and '//' to end of line, and block comments
 *                   (slash-star ... star-slash), which span lines
 *   Strings       — "..." with backslash escapes and ${...} inside
 *   Numbers       — decimal integers and floats, with an optional exponent
 *
 * Scanning is done by the generic engine in color_generic.c; see
 * color_generic.h for the cross-line state encoding.
 *
 * Two constructs are deliberately not tracked.  A here-document
 * (<<EOT ... EOT) needs the delimiter word carried from one line to the
 * next, and the colorize_fn contract carries a plain int, so the body of a
 * here-document is coloured as though it were HCL code.  A template
 * directive (%{ if ... }) is left alone as well: the engine recognises one
 * sigil character, and ${...} is by far the more common of the two.
 */
#include "color.h"
#include "color_generic.h"
#include <stddef.h>

/*
 * HCL is case-sensitive, so the table is matched literally.  None of these
 * words is reserved by the grammar — they are ordinary identifiers that
 * Terraform gives a meaning to — so the table lists the ones Terraform
 * itself defines, and an attribute that happens to reuse one of the names
 * is coloured as a keyword too.
 */
static const char *const tf_keywords[] = {
    "check",
    "connection",
    "content",
    "count",
    "data",
    "depends_on",
    "dynamic",
    "each",
    "else",
    "endfor",
    "endif",
    "false",
    "for",
    "for_each",
    "if",
    "import",
    "in",
    "lifecycle",
    "local",
    "locals",
    "module",
    "moved",
    "null",
    "output",
    "path",
    "provider",
    "providers",
    "provisioner",
    "removed",
    "resource",
    "self",
    "terraform",
    "true",
    "var",
    "variable",
    NULL,
};

/*
 * HCL has one quoted form.  It does not span lines — a multi-line literal
 * is written as a here-document instead — so cross_line stays clear.
 */
static const struct str_spec tf_strings[] = {
    {.open = '"', .escape = 1, .expand_sigil = 1},
    {0},
};

static const struct lang_spec tf_spec = {
    .keywords = tf_keywords,
    .line_comment = "#",
    .line_comment2 = "//",
    .block_open = "/*",
    .block_close = "*/",
    .strings = tf_strings,
    .num = {.word_boundary_guard = 1},
    .sigil_char = '$',
};

static int
tf_colorize(int state, const char *line, int len, char *attrs)
{
	/*
	 * == Colorize one line of Terraform (HCL) ==
	 */
	return colorize_generic(state, line, len, attrs, &tf_spec);
}

/*
 * .tf is a configuration file and .tfvars a variable file; both are HCL.
 * .hcl covers the dependency lock file (.terraform.lock.hcl) and the other
 * tools that adopted the same language.
 */
static const char *const tf_extensions[] = {".tf", ".tfvars", ".hcl", NULL};

/*
 * Terraform's own formatter, terraform fmt, indents two spaces to the
 * level and never emits a tab, so the tabstop only governs how a tab typed
 * by hand is displayed.  Two keeps that consistent with formatted files.
 */
const struct colorizer colorizer_tf = {"terraform", tf_extensions, NULL,
                                       tf_colorize, 2};
