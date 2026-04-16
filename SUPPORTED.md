# Supported behavior

This file defines the supported behavior contract for this project.

Last updated: 2026-04-16.

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

## Visual mode and text objects

- Visual selection toggle with `v`/`V`.
- Block visual selection with `Ctrl-V`; anchor and cursor define a column rectangle.
- Mode switching: pressing `v`, `V`, or `Ctrl-V` while in any visual mode switches to that mode without leaving visual.
- Visual operators: delete, yank, change, put replacement.
- Block visual operators: delete (`d`/`x`), yank (`y`), change (`c`), case (`U`/`u`), indent (`>`/`<`).
  - Yank stores column content per row; each row is newline-separated in the register with type BLOCK.
  - Lines that do not reach the left column of the block are skipped by operators.
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

- Automatic on open; driven by file extension.
- C / C++ files (`.c`, `.h`, `.cc`, `.cpp`, `.cxx`, `.hh`, `.hpp`, `.inl`): keywords, types,
  string/character literals, single- and multi-line comments, preprocessor directives, numbers.

## Persistent undo

- `:set undofile` serialises the undo stack to a sidecar (`.filename.vundo`) on every `:w`.
- On re-open, history is restored; `u` and `Ctrl-R` work across sessions.
- A stale sidecar (file changed outside the editor since last save) is silently ignored.

## Terminal and UX support

- Bracketed paste handling.
- Status line feedback for command outcomes.
- Cursor-shape control where terminal support is available.

## Compatibility note

“Supported” means behavior is part of the project contract. If behavior changes, this file must 
be updated in the same change.
