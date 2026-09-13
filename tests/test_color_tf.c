/*
 * test_color_tf.c - unit tests for the Terraform (HCL) syntax colorizer and
 * for the file-name matching that selects it.
 *
 * Cross-line states (mirrors color_tf.c / color_generic.h):
 *   0  TF_NORMAL
 *   1  TF_BLOCK_CMT       (inside a block comment)
 *   2  TF_BLOCK_CMT_STAR  (inside a block comment, last byte was '*')
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_tf;
#define TF colorizer_tf.colorize
#define CMT 1
#define CMT_STAR 2

/* ---- keywords ----------------------------------------------------------- */

static void
test_block_types(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "resource \"aws_vpc\" \"main\" {",
                            "KKKKKKKK.SSSSSSSSS.SSSSSS..");
    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "variable \"x\" {", "KKKKKKKK.SSS..");   CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "output \"y\" {",   "KKKKKK.SSS..");     CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "module \"vpc\" {", "KKKKKK.SSSSS..");   CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "locals {",         "KKKKKK..");         CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "terraform {",      "KKKKKKKKK..");      CHECK(st == 0);
    /* indented block types are recognised too */
    st = CHECK_COLOR(TF, 0, "  provisioner \"local-exec\" {",
                            "..KKKKKKKKKKK.SSSSSSSSSSSS..");
    CHECK(st == 0);
    /* not a keyword */
    st = CHECK_COLOR(TF, 0, "ingress {", ".........");               CHECK(st == 0);
}

static void
test_constants_and_named_values(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "  enabled = true", "............KKKK"); CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  x = false",       "......KKKKK");      CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  x = null",        "......KKKK");       CHECK(st == 0);
    /* HCL is case-sensitive: True is an ordinary identifier */
    st = CHECK_COLOR(TF, 0, "  x = True",        "..........");       CHECK(st == 0);
    /* named values are keywords wherever they appear */
    st = CHECK_COLOR(TF, 0, "  name = var.name", ".........KKK.....");
    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  id = each.key",   ".......KKKK....");
    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  n = local.count", "......KKKKK.KKKKK");
    CHECK(st == 0);
}

static void
test_meta_arguments(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "  count = 3",    "..KKKKK...N");    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  for_each = s", "..KKKKKKKK....");  CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  depends_on = []",
                            "..KKKKKKKKKK.....");
    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  lifecycle {", "..KKKKKKKKK..");    CHECK(st == 0);
}

static void
test_expression_keywords(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "  v = [for x in y : x]",
                            ".......KKK...KK.......");
    CHECK(st == 0);
}

/* ---- comments ----------------------------------------------------------- */

static void
test_line_comments(void)
{
    int st;

    /* '#' is the idiomatic form */
    st = CHECK_COLOR(TF, 0, "# note",         "CCCCCC");        CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  # note",       "..CCCCCC");      CHECK(st == 0);
    /* '//' is accepted as well */
    st = CHECK_COLOR(TF, 0, "// note",        "CCCCCCC");       CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  // note",      "..CCCCCCC");     CHECK(st == 0);
    /* a comment after code, in either form */
    st = CHECK_COLOR(TF, 0, "  count = 1 # why",  "..KKKKK...N.CCCCC");
    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  count = 1 // why", "..KKKKK...N.CCCCCC");
    CHECK(st == 0);
    /* a single '/' is not a comment */
    st = CHECK_COLOR(TF, 0, "  a = b / c",    "...........");     CHECK(st == 0);
    /* '#' inside a string is not a comment */
    st = CHECK_COLOR(TF, 0, "  x = \"a # b\"", "......SSSSSSS");  CHECK(st == 0);
}

static void
test_block_comments(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "/* one line */", "CCCCCCCCCCCCCC"); CHECK(st == 0);
    /* spanning lines */
    st = CHECK_COLOR(TF, 0, "/* open",        "CCCCCCC");        CHECK(st == CMT);
    st = CHECK_COLOR(TF, CMT, "  still",      "CCCCCCC");        CHECK(st == CMT);
    st = CHECK_COLOR(TF, CMT, "  done */ x = 1",
                             "CCCCCCCCC.....N");
    CHECK(st == 0);
    /* a line ending on the close half keeps the STAR state */
    st = CHECK_COLOR(TF, 0, "/* a *",         "CCCCCC");         CHECK(st == CMT_STAR);
    st = CHECK_COLOR(TF, CMT_STAR, "/ x",     "C..");            CHECK(st == 0);
}

/* ---- strings ------------------------------------------------------------ */

static void
test_strings(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "  region = \"eu-west-1\"",
                            "...........SSSSSSSSSSS");
    CHECK(st == 0);
    /* escaped quote inside the string */
    st = CHECK_COLOR(TF, 0, "  x = \"a\\\"b\"", "......SSSSSS"); CHECK(st == 0);
    /* an unterminated quote does not leak into the next line */
    st = CHECK_COLOR(TF, 0, "  x = \"open",   "......SSSSS");   CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  y = 1",        "......N");       CHECK(st == 0);
}

static void
test_interpolation(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "  n = \"${var.x}\"",
                            "......SPPPPPPPPS");
    CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  n = \"a-${x}-b\"",
                            "......SSSPPPPSSS");
    CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(TF, 0, "  count = 3",      "..KKKKK...N");   CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  x = 1.5",        "......NNN");     CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  x = 1e6",        "......NNN");     CHECK(st == 0);
    /* a digit inside an identifier is not a number */
    st = CHECK_COLOR(TF, 0, "  x = t2micro",    "............."); CHECK(st == 0);
}

/* ---- here-documents (a documented limitation) --------------------------- */

static void
test_heredoc_is_not_tracked(void)
{
    int st;

    /*
     * The delimiter word cannot be carried in the integer cross-line state,
     * so the body of a here-document is coloured as though it were HCL.
     * These checks pin the behaviour rather than endorse it.
     */
    st = CHECK_COLOR(TF, 0, "  policy = <<EOT", "................"); CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "    count me in",   "....KKKKK....KK");  CHECK(st == 0);
    st = CHECK_COLOR(TF, 0, "  EOT",             ".....");            CHECK(st == 0);
}

/* ---- file name matching ------------------------------------------------- */

static void
test_colorizer_find(void)
{
    CHECK(colorizer_find("main.tf") == &colorizer_tf);
    CHECK(colorizer_find("terraform.tfvars") == &colorizer_tf);
    CHECK(colorizer_find("/srv/infra/main.tf") == &colorizer_tf);
    /* the dependency lock file, and the other tools that adopted HCL */
    CHECK(colorizer_find(".terraform.lock.hcl") == &colorizer_tf);
    CHECK(colorizer_find("nomad.hcl") == &colorizer_tf);
    /* case is ignored on the extension */
    CHECK(colorizer_find("MAIN.TF") == &colorizer_tf);
    /* a name that only starts like the extension does not match */
    CHECK(colorizer_find("main.tfstate") == NULL);
    CHECK(colorizer_find("maintf") == NULL);
    /* other languages still resolve */
    CHECK(colorizer_find("main.c") != &colorizer_tf);
    CHECK(colorizer_find("a.yaml") != &colorizer_tf);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_block_types();
    test_constants_and_named_values();
    test_meta_arguments();
    test_expression_keywords();
    test_line_comments();
    test_block_comments();
    test_strings();
    test_interpolation();
    test_numbers();
    test_heredoc_is_not_tracked();
    test_colorizer_find();
    SUMMARY();
}
