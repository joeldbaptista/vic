# Supported behavior

This file defines the supported behavior contract for this project.

Last updated: 2026-10-22.

## Scope of support

- Linux terminal workflow is the primary supported environment.
- UTF-8 text handling is supported for normal editing workflows.
- Features listed here are expected to work and are regression-tested or actively maintained.

## Editing model

- Modal editing: normal, insert, replace, visual charwise, visual linewise, visual block.
- Count prefixes for motions/operators.
- Dot-repeat (`.`) for last modifying command.
- Undo (`u`) and redo (`Ctrl-R`).

## Core motions

- Character motions: `h`, `j`, `k`, `l`, arrows.
  - `j`/`k` and the arrow keys are column-preserving: the horizontal
    position is remembered across lines shorter than the target column.
  - `Enter` and `-` move to the next/previous line landing on the first
    non-blank character.
- Line motions: `0`, `$`, `gg`, `G`, `{count}G`.
- Word motions: `w`, `b`, `e`, `W`, `B`, `E`.
- Screen motions and scroll: `Ctrl-F`, `Ctrl-B`, `Ctrl-D`, `Ctrl-U`, `Ctrl-E`, `Ctrl-Y`, `H`, `M`, `L`, `z.`.
- Matching pair motion: `%`.
- Char-find motions: `f`, `F`, `t`, `T`, with repeat via `;` and `,`.

## Operators and edit commands

- Delete/change/yank with motions and counts.
- Common line commands: `dd`, `cc`, `yy`, `D`, `C`, `Y`.
- Put: `p`, `P`.
- Character edits: `x`, `X`, `s`, `r`, `~`, `J`.
- Filter: `!{motion}`, `!!` — pipe the addressed lines through an external
  shell command and replace them with its output (e.g. `!Gsort`, `!!tr a-z
  A-Z`). Always linewise, like `dd`/`yy`. A non-zero exit leaves the buffer
  untouched.

## Visual mode and text objects

- Visual selection toggle with `v`/`V`.
- Block visual selection with `Ctrl-V`; anchor and cursor define a column rectangle.
- Mode switching: pressing `v`, `V`, or `Ctrl-V` while in any visual mode switches to that mode without leaving visual.
- Visual operators: delete, yank, change, put replacement, filter (`!`,
  prompts with `:'<,'>!` for a shell command).
- Block visual operators: delete (`d`/`x`), yank (`y`), change (`c`), case (`U`/`u`), indent (`>`/`<`), insert (`I`).
  - Yank stores column content per row; each row is newline-separated in the register with type BLOCK.
  - Lines that do not reach the left column of the block are skipped by operators.
  - `I` enters insert mode at the left column of the block on the first row; on ESC the typed text is replayed at the same column on every remaining row in the block that reaches that column.
  - Put of a BLOCK-type register is not yet supported.
- Text objects under operator/visual workflows:
  - Word: `iw`, `aw`, `iW`, `aW`
  - Quotes/backtick: `i"`, `a"`, `i'`, `a'`, ``i` ``, ``a` ``
  - Delimiters: `i(`/`a(`, `i[`/`a[`, `i{`/`a{`, `i<`/`a<`
  - Tag objects: `it`, `at`

## Search

- Pattern search: `/`, `?`, `n`, `N`.
- Word-under-cursor search: `*`, `#`, `g*`, `g#`.
- Substitution command (`:s`) is supported in command mode.

## Registers and marks

- Named registers and default register workflows.
- Marks with letter names and jump behavior (`'a`, `''`).

## Command mode / ex subset

- File/session commands: `:w`, `:q`, `:wq`, `:x`, `:e`.
- Option handling via `:set` for documented options.
- Global command: `:g/pattern/cmd` — execute a colon command on every line matching a regex.
- Inverse global: `:v/pattern/cmd` — execute a colon command on every line that does **not** match.
- Address ranges are supported on both: `:%g/pattern/cmd`, `:'a,'bg/pattern/cmd`.
- Filter: `:{range}!cmd` — pipe the addressed lines through an external
  shell command and replace them with its output (e.g. `:%!sort`,
  `:5,10!fmt -w72`). With no range, `:!cmd` is unchanged (interactive shell
  escape). A non-zero exit leaves the buffer untouched and shows the
  command's output on the status line.

## Options currently supported

- `autoindent` (`ai`)
- `expandtab` (`et`)
- `flash` (`fl`)
- `ignorecase` (`ic`)
- `showmatch` (`sm`)
- `tabstop` (`ts`)
- `cursorshape` (`cshp`)
- `number` (`nu`)
- `relativenumber` (`rnu`)
- `undofile` (`uf`) — persist undo history across sessions (see `docs/undofile.md`)

## Syntax highlighting

- Automatic on open; driven by file name — the extension for most languages, and the
  base name for Dockerfiles (`Dockerfile`, `Containerfile`, either with a dotted suffix
  such as `Dockerfile.dev`) and makefiles (`Makefile`, `GNUmakefile`, `BSDmakefile`,
  likewise with a dotted suffix such as `Makefile.am`), which normally carry no
  extension.
- C / C++ files (`.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.hh`, `.hpp`, `.inl`): keywords, types,
  string/character literals, single- and multi-line comments, preprocessor directives, numbers.
- YAML files (`.yaml`, `.yml`): mapping keys (plain or quoted), `#` comments, quoted
  scalars, block scalars (`|`, `>`, including their bodies across lines), anchors
  (`&a`), aliases (`*a`), tags (`!!str`), document markers (`---`, `...`), directives
  (`%YAML 1.2`), numbers, and the constants `true`, `false`, `null`, `yes`, `no`, `on`,
  `off` and `~`. Multi-line plain scalars and multi-line flow collections are not
  tracked across lines, so a key is only recognised at the head of a line.
- Terraform / HCL files (`.tf`, `.tfvars`, `.hcl`): block types (`resource`, `variable`,
  `module`, ...), named values (`var`, `local`, `each`, `count`, `path`, `self`),
  meta-arguments (`for_each`, `depends_on`, `lifecycle`, ...), the expression words
  (`for`, `in`, `if`, `else`), the constants `true`, `false` and `null`, `#` and `//`
  comments, block comments spanning lines, quoted strings with `${...}` interpolation,
  and decimal numbers. A here-document (`<<EOT`) is not tracked across lines, so its
  body is coloured as though it were HCL code; a template directive (`%{ if ... }`) is
  not highlighted.
- Makefiles (also `.mk`, `.mak`, `.make`, `.inc`): directives (`include`, `ifeq`, `define`, ...)
  and the variable each one names, the target list of a rule (including a pattern rule
  and a double-colon rule), the variable name on the left of an assignment (`=`, `:=`,
  `::=`, `?=`, `+=`, `!=`) and of a target-specific assignment, variable references
  (`$(CC)`, `${CC}`, the automatic `$@` and `$<`, the escaped `$$`, and nested forms
  such as `$(patsubst %.c,%.o,$(SRC))`), the recipe prefixes `@`, `-` and `+`, quoted
  text, `#` comments, and decimal numbers. A line continued with a trailing backslash
  is scanned as a continuation, so it opens no target and no directive, and a continued
  comment stays a comment. A tab-indented line is a recipe only once a rule is in
  effect, which is how make itself reads it, so a tab-indented assignment inside a
  conditional at the head of the file is coloured as an assignment. The body of a
  recipe is scanned as makefile text rather than as shell, because the shell is
  chosen by the makefile (`SHELL = ...`) and so is not fixed.
- awk scripts (`.awk`, `.gawk`): keywords (`BEGIN`, `END`, `BEGINFILE`, `ENDFILE`,
  `function`, `if`, `print`, `getline`, ...), the built-in function names of POSIX awk
  and gawk (`length`, `sub`, `gensub`, ...), which awk reserves as it reserves keywords,
  field references (`$1`, `$NF`, `$(i+1)`), the gawk directives `@include`, `@load` and
  `@namespace`, quoted strings, regex literals (`/^#/`), `#` comments, and numbers
  (decimal, float, exponent, and gawk `0x` hex). A `/` opens a regex literal only where
  an operand is expected, which is how awk itself tells a regex from a division, and
  only when the same line holds the closing `/`; otherwise the `/` is a division. The
  built-in variables (`NR`, `NF`, `FS`, ...) are ordinary names to awk, so they are not
  highlighted.
- Rust files (`.rs`): keywords (`fn`, `let`, `impl`, `match`, `async`, `unsafe`, ...), the
  words the language reserves (`become`, `yield`, ...), the primitive type names (`i32`,
  `u8`, `f64`, `bool`, `char`, `str`, ...), which the language builds in, attributes
  (`#[derive(Debug)]`, `#![allow(...)]`), `//` line comments (so also the doc forms `///`
  and `//!`), block comments spanning lines, string literals including the `b` prefixed
  form and the raw forms (`r"..."`, `br"..."`, `r#"..."#`), character literals (`'x'`,
  `'\n'`, `b'0'`), and numbers (decimal, `0x` hex, `0o` octal, `0b` binary, float with
  exponent, `_` digit separators, and type suffixes such as `1u8` and `2.0f32`). A `'`
  opens a character literal only when one character and its closing quote follow it, so a
  lifetime or a loop label (`'a`, `'static`, `'outer`) stays uncoloured rather than
  opening a literal that runs to the end of the line. A raw string has no backslash
  escapes and closes only at a quote carrying as many `#` as its opening delimiter did, so
  neither a trailing backslash (`r"C:\dir\"`) nor a quote in the body of a hashed form
  ends it early. A raw identifier (`r#type`, `r#match`) is a name and not an attribute, and
  its keyword-like part is not treated as a keyword. A string literal may span lines, which
  Rust allows. Three constructs are approximated: a nested block comment closes at the
  first `*/`, so the text from there to the outer close is scanned and coloured as code,
  and a quote left unpaired there opens a string that carries on to the following lines; an
  attribute is scanned to the end of its line only, so an attribute whose list is split
  over lines, as rustfmt writes a long derive list, keeps its colour on the first line
  while the rest of the list is scanned as code; and a raw string closes only when its
  quote and all of its hashes sit on the same line. Library names are not keywords to Rust,
  so `Option`, `Result`, `Some`, `None`, `Ok`, `Err`, `Vec` and `String` are ordinary names
  and are not highlighted.
- Dockerfiles: instructions (`FROM`, `RUN`, `COPY`, ...) at the head of a logical line,
  variable expansions (`$VAR`, `${VAR}`), `#` comments, quoted strings, numbers. A line
  continued with a trailing backslash does not start a new instruction on the next line.
- The scheme is monochromatic: tokens are distinguished by SGR attribute (bold, dim, grey),
  not by colour.
- Opening a file also re-seeds `tabstop` from the file type, overriding the `config.h`
  default and any earlier `:set tabstop=`. Terraform and YAML use 2; awk, Dockerfile,
  Markdown, Python, Rust, shell and SQL use 4; C, C++ and makefiles use 8; every other
  file type uses the `config.h` default.

## Persistent undo

- `:set undofile` serialises the undo stack to a sidecar (`.filename.vundo`) on every `:w`.
- On re-open, history is restored; `u` and `Ctrl-R` work across sessions.
- A stale sidecar (file changed outside the editor since last save) is silently ignored.

## Standard input and pager mode

- A file name of `-` reads the document from standard input; vic drains it
  before the editor starts and then reopens `/dev/tty` for keystrokes.
- The buffer loaded from `-` is unnamed, so `:write` requires a path.
- `-p` is pager mode: it implies `-R` and strips CSI escape sequences and
  backspace overstrike from the document as it loads.
- `MANPAGER="vic -p -"` is a supported configuration. See
  [PAGER.md](PAGER.md).

## Terminal and UX support

- Bracketed paste handling.
- Status line feedback for command outcomes.
- Cursor-shape control where terminal support is available.

## Compatibility note

“Supported” means behavior is part of the project contract. If behavior changes, this file must 
be updated in the same change.
