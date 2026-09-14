/*
 * test_color_make.c - unit tests for the makefile syntax colorizer and for
 * the file-name matching that selects it.
 *
 * Cross-line state is a bit set (mirrors color_make.c):
 *   0  MK_NORMAL   no rule in effect, previous line complete
 *   1  MK_RULE     a rule is in effect, so a tab line is its recipe
 *   2  MK_CONT     the previous line ended with a continuation backslash
 *   4  MK_COMMENT  set beside MK_CONT when that line ended in a comment
 *   8  MK_DEFINE   inside a define ... endef body
 */
#include "color_testutil.h"

extern const struct colorizer colorizer_make;
#define MK colorizer_make.colorize
#define RULE 1
#define CONT 2
#define CMT (CONT | 4)
#define DEF 8

/* ---- assignments -------------------------------------------------------- */

static void
test_assignments(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "CC = cc",             "PP.....");             CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "CFLAGS := -O2 -Wall", "PPPPPP.............");  CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "PREFIX ?= /usr/local","PPPPPP..............");  CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "LDLIBS += -lm",       "PPPPPP.......");        CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "VER != git describe", "PPP................");   CHECK(st == 0);
    /* ::= is an assignment, even though it opens with a colon */
    st = CHECK_COLOR(MK, 0, "OBJ ::= $(SRC)",      "PPP.....PPPPPP");       CHECK(st == 0);
}

/* ---- rules and targets -------------------------------------------------- */

static void
test_rules(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "all: main.o util.o", "KKK...............");
    CHECK(st == RULE);
    st = CHECK_COLOR(MK, 0, ".PHONY: all clean",  "KKKKKK...........");
    CHECK(st == RULE);
    /* a pattern rule and a double-colon rule */
    st = CHECK_COLOR(MK, 0, "%.o: %.c",           "KKK.....");
    CHECK(st == RULE);
    st = CHECK_COLOR(MK, 0, "lib.a:: $(OBJ)",     "KKKKK...PPPPPP");
    CHECK(st == RULE);
    /* a variable reference inside the target list keeps its own colour */
    st = CHECK_COLOR(MK, 0, "$(BIN): $(OBJ)",     "PPPPPP..PPPPPP");
    CHECK(st == RULE);
    /* an indented target is still a target */
    st = CHECK_COLOR(MK, 0, "  install: all",     "..KKKKKKK.....");
    CHECK(st == RULE);
    /* a target-specific variable: the colon comes first, so this is a rule */
    st = CHECK_COLOR(MK, 0, "debug: CFLAGS += -g", "KKKKK..PPPPPP......");
    CHECK(st == RULE);
    /* the colon of a substitution reference does not open a rule */
    st = CHECK_COLOR(MK, 0, "OBJ = $(SRC:.c=.o)",  "PPP...PPPPPPPPPPPP");
    CHECK(st == 0);
}

/* ---- directives --------------------------------------------------------- */

static void
test_directives(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "include config.mk", "KKKKKKK..........");  CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "-include deps.mk",  "KKKKKKKK........");   CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "ifeq ($(CC),gcc)",  "KKKK..PPPPP.....");   CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "endif",             "KKKKK");              CHECK(st == 0);
    /* the argument of a directive that names a variable is a variable */
    st = CHECK_COLOR(MK, 0, "ifdef VERBOSE",     "KKKKK.PPPPPPP");      CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "export CC",         "KKKKKK.PP");          CHECK(st == 0);
    /* a directive that prefixes an assignment */
    st = CHECK_COLOR(MK, 0, "export CC = cc",    "KKKKKK.PP.....");     CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "override FLAGS := -g",
                            "KKKKKKKK.PPPPP......");
    CHECK(st == 0);
    /* "else" may carry a second directive */
    st = CHECK_COLOR(MK, 0, "else ifdef MINGW",  "KKKK.KKKKK.PPPPP");   CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "else",              "KKKK");               CHECK(st == 0);
}

static void
test_directive_word_used_as_a_name(void)
{
    int st;

    /*
     * A makefile may name a target or a variable after a directive.  The
     * colon or the assignment operator that follows settles which reading
     * applies, so "export:" is a rule and "include = x" an assignment.
     */
    st = CHECK_COLOR(MK, 0, "export:",     "KKKKKK.");     CHECK(st == RULE);
    st = CHECK_COLOR(MK, 0, "include = x", "PPPPPPP....");  CHECK(st == 0);
}

static void
test_define(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "define greeting", "KKKKKK.PPPPPPPP");
    CHECK(st == DEF);
    /* the body is text, so a colon in it does not open a rule */
    st = CHECK_COLOR(MK, DEF, "  hi: $(USER)", "......PPPPPPP");
    CHECK(st == DEF);
    st = CHECK_COLOR(MK, DEF, "endef", "KKKKK");
    CHECK(st == 0);
}

/* ---- recipes ------------------------------------------------------------ */

static void
test_recipes(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "foo:", "KKK.");
    CHECK(st == RULE);
    st = CHECK_COLOR(MK, RULE, "\t$(CC) -o $@ $<", ".PPPPP....PP.PP");
    CHECK(st == RULE);
    /* the prefixes that make itself reads before handing the line to a shell */
    st = CHECK_COLOR(MK, RULE, "\t@echo hi", ".P.......");        CHECK(st == RULE);
    st = CHECK_COLOR(MK, RULE, "\t-rm -f x", ".P.......");        CHECK(st == RULE);
    st = CHECK_COLOR(MK, RULE, "\t+$(MAKE) -C sub", ".PPPPPPPP.......");
    CHECK(st == RULE);
    /* quoted text, with make's own expansion inside it */
    st = CHECK_COLOR(MK, RULE, "\t@echo 'x $@'", ".P.....SSSPPS");
    CHECK(st == RULE);
    /* a blank line and a comment do not end the recipe */
    st = CHECK_COLOR(MK, RULE, "", "");                           CHECK(st == RULE);
    st = CHECK_COLOR(MK, RULE, "# note", "CCCCCC");               CHECK(st == RULE);
    /* an assignment does */
    st = CHECK_COLOR(MK, RULE, "X = 1", "P...N");                 CHECK(st == 0);
}

static void
test_tab_line_without_a_rule(void)
{
    int st;

    /*
     * Make reads a tab line as a recipe only once a rule is in effect.
     * Before one - inside a conditional at the head of the file, say - the
     * line is ordinary, so a tab-indented assignment is coloured as an
     * assignment rather than as a command.
     */
    st = CHECK_COLOR(MK, 0, "\tCFLAGS += -D_X", ".PPPPPP........");
    CHECK(st == 0);
    st = CHECK_COLOR(MK, RULE, "\tCFLAGS += -D_X", "...............");
    CHECK(st == RULE);
}

/* ---- variable references ------------------------------------------------ */

static void
test_references(void)
{
    int st;

    /* a nested reference is one token, so the inner ')' does not close it */
    st = CHECK_COLOR(MK, 0, "x = $(patsubst %.c,%.o,$(SRC))",
                            "P...PPPPPPPPPPPPPPPPPPPPPPPPPP");
    CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "x = ${VAR}", "P...PPPPPP");  CHECK(st == 0);
    /* "$$" is make's escape for a literal '$'; the shell name after it is not
     * a make variable */
    st = CHECK_COLOR(MK, 0, "x = $$HOME", "P...PP....");  CHECK(st == 0);
}

/* ---- comments ----------------------------------------------------------- */

static void
test_comments(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "# a comment",  "CCCCCCCCCCC");   CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "  # indented", "..CCCCCCCCCC");  CHECK(st == 0);
    st = CHECK_COLOR(MK, 0, "all: x # why", "KKK....CCCCC");  CHECK(st == RULE);
    st = CHECK_COLOR(MK, 0, "CFLAGS = -O2 # fast",
                            "PPPPPP.......CCCCCC");
    CHECK(st == 0);
    /* an escaped hash is not a comment */
    st = CHECK_COLOR(MK, 0, "foo\\#bar: x", "KKKKKKKK...");   CHECK(st == RULE);
    /* nor is one inside quoted text */
    st = CHECK_COLOR(MK, RULE, "\techo \"a#b\"", "......SSSSS");
    CHECK(st == RULE);
}

/* ---- continued lines ---------------------------------------------------- */

static void
test_continuations(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "SRC = a.c \\", "PPP........");
    CHECK(st == CONT);
    /* the continuation is value text: no target, no directive */
    st = CHECK_COLOR(MK, CONT, "      b.c: x", "............");
    CHECK(st == 0);
    /* a continued recipe keeps the rule in effect */
    st = CHECK_COLOR(MK, RULE, "\t@cmd \\", ".P.....");
    CHECK(st == (RULE | CONT));
    st = CHECK_COLOR(MK, RULE | CONT, "\t  more", ".......");
    CHECK(st == RULE);
    /* a continued comment stays a comment */
    st = CHECK_COLOR(MK, 0, "# note \\", "CCCCCCCC");
    CHECK(st == CMT);
    st = CHECK_COLOR(MK, CMT, "still a comment", "CCCCCCCCCCCCCCC");
    CHECK(st == 0);
    /* an even number of trailing backslashes does not continue the line */
    st = CHECK_COLOR(MK, 0, "X = a\\\\", "P......");
    CHECK(st == 0);
}

/* ---- numbers ------------------------------------------------------------ */

static void
test_numbers(void)
{
    int st;

    st = CHECK_COLOR(MK, 0, "VERSION = 3", "PPPPPPP...N");  CHECK(st == 0);
    st = CHECK_COLOR(MK, RULE, "\tchmod 755 f", ".......NNN..");
    CHECK(st == RULE);
    /* a digit inside a word is part of the word */
    st = CHECK_COLOR(MK, RULE, "\tgcc -O2 x", "..........");
    CHECK(st == RULE);
}

/* ---- file name matching ------------------------------------------------- */

static void
test_colorizer_find(void)
{
    CHECK(colorizer_find("Makefile") == &colorizer_make);
    CHECK(colorizer_find("makefile") == &colorizer_make);
    CHECK(colorizer_find("GNUmakefile") == &colorizer_make);
    CHECK(colorizer_find("BSDmakefile") == &colorizer_make);
    CHECK(colorizer_find("/home/u/src/Makefile") == &colorizer_make);
    /* a dotted suffix on the base name, as in the Dockerfile convention */
    CHECK(colorizer_find("Makefile.am") == &colorizer_make);
    /* an included fragment named for what it holds */
    CHECK(colorizer_find("rules.mk") == &colorizer_make);
    CHECK(colorizer_find("config.mak") == &colorizer_make);
    CHECK(colorizer_find("build.make") == &colorizer_make);
    CHECK(colorizer_find("rules.inc") == &colorizer_make);
    /* case is ignored */
    CHECK(colorizer_find("MAKEFILE") == &colorizer_make);
    /* a name that only starts like the base name does not match */
    CHECK(colorizer_find("Makefilexyz") == NULL);
    /* other languages still resolve */
    CHECK(colorizer_find("notes.mkd") != &colorizer_make);
    CHECK(colorizer_find("main.c") != &colorizer_make);
    CHECK(colorizer_find("Dockerfile") != &colorizer_make);
}

/* ---- main --------------------------------------------------------------- */

int
main(void)
{
    test_assignments();
    test_rules();
    test_directives();
    test_directive_word_used_as_a_name();
    test_define();
    test_recipes();
    test_tab_line_without_a_rule();
    test_references();
    test_comments();
    test_continuations();
    test_numbers();
    test_colorizer_find();
    SUMMARY();
}
