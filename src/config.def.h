/*
 * config.def.h - compile-time defaults for vic.
 *
 * Copy to config.h (gitignored) and edit to taste, then `make`:
 *   cp src/config.def.h src/config.h
 *
 * Every value here is also reachable at runtime via :set (see OPTS_STR in
 * vic.h) — this file only picks what a freshly started vic begins with.
 */
#ifndef SRC_CONFIG_H
#define SRC_CONFIG_H

#include "term.h"

/* Boolean :set options, at startup: 1 = on, 0 = off. */
#define CFG_AUTOINDENT 0     /* ai   — indent a new line like the one above */
#define CFG_EXPANDTAB 0      /* et   — insert spaces instead of tabs */
#define CFG_FLASH 0          /* fl   — flash the screen instead of beeping */
#define CFG_IGNORECASE 0     /* ic   — case-insensitive search */
#define CFG_SHOWMATCH 0      /* sm   — briefly jump to a matching bracket */
#define CFG_NUMBER 0         /* nu   — show line numbers */
#define CFG_RELATIVENUMBER 0 /* rnu  — show relative line numbers */
#define CFG_UNDOFILE 0       /* uf   — persist undo history to disk */
#define CFG_SYNTAX 0         /* syn  — syntax highlighting */

/* ts — width of a tab stop, in columns (1-32). */
#define CFG_TABSTOP 8

/*
 * cshp/cshpi/cshpe — cursor shape in Normal/Insert/Ex-command-line mode,
 * one of the CURSOR_STYLE_* constants (term.h): DEFAULT, BLINK_BLOCK,
 * BLOCK, BLINK_UNDERLINE, UNDERLINE, BLINK_PIPE, PIPE.
 */
#define CFG_CURSORSHAPE CURSOR_STYLE_BLINK_BLOCK
#define CFG_CURSORSHAPE_INSERT CURSOR_STYLE_BLINK_PIPE
#define CFG_CURSORSHAPE_EX CURSOR_STYLE_PIPE

#endif
