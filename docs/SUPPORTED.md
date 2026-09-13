# Supported behavior

This file defines the supported behavior contract for this project.

Last updated: 2026-09-14.

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
  such as `Dockerfile.dev`), which normally carry no extension.
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
- Dockerfiles: instructions (`FROM`, `RUN`, `COPY`, ...) at the head of a logical line,
  variable expansions (`$VAR`, `${VAR}`), `#` comments, quoted strings, numbers. A line
  continued with a trailing backslash does not start a new instruction on the next line.
- The scheme is monochromatic: tokens are distinguished by SGR attribute (bold, dim, grey),
  not by colour.
- Opening a file also re-seeds `tabstop` from the file type, overriding the `config.h`
  default and any earlier `:set tabstop=`. Terraform and YAML use 2; Dockerfile,
  Markdown, Python, shell and SQL use 4; C and C++ use 8; every other file type uses
  the `config.h` default.

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
