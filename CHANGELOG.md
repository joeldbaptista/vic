# Changelog

## v1.0

Initial release.

`vic` is a small, dependency-free vi clone derived from BusyBox vi and
rewritten around suckless principles: a plain dynamic-array text buffer,
dispatch-table-driven command handling, and a codebase small enough for one
person to hold in their head.

### Editing

- Normal, insert, replace, visual (charwise) and visual-linewise modes
- Count prefixes on motions and operators (`3dw`, `5j`, `"a2yy`)
- Dot-repeat (`.`) and undo/redo (`u`, `Ctrl-R`), including persistent undo
  via `:set undofile`
- Full motion set: character/line, word/WORD, line anchors, `gg`/`G`/`{n}G`,
  `f`/`F`/`t`/`T`/`;`/`,`, `%`, sentence/paragraph motions, screen motions
  (`H`/`M`/`L`), scrolling
- Operators (`d c y x r ~ J > <`) with counts, motions, and text objects
  (`iw aw`, quotes, brackets, `it`/`at`)
- Named registers (`"a`–`"z`), marks (`ma`–`mz`, `'a`–`'z`)
- POSIX BRE search (`/ ? n N`), word-under-cursor search (`* # g* g#`)
- Ex commands: `:w :q :wq :x`, `:e`, `:r`, `:s`/`:%s`, `:g`/`:v`, `:set`
- `"+`/`+` shared register backed by `~/.cache/vic/yank`, for moving text
  between independent `vic` sessions (e.g. across tmux panes)

### Syntax highlighting

Automatic, extension-based highlighting for C/C++, Shell, Markdown, and
SQL, via a pluggable colorizer registry.

### Configuration

Compile-time configuration via `src/config.h` (line numbers, syntax
highlighting, tabstop, cursor shapes, etc.), plus runtime `:set` options:
`autoindent`, `expandtab`, `ignorecase`, `showmatch`, `flash`, `number`,
`relativenumber`, `tabstop`, `cursorshape`/`cursorshapeinsert`, `undofile`.

### Testing

PTY-driven regression suite (`make check-regression-pty`) and sanitizer
runs (`make check-sanitizers-pty`) drive a real `vic` process and assert on
written file content.

### Design

- MONRAS parsing model for the normal-mode command parser (`src/parser.h`)
- Plain dynamic-array buffer — no gap buffer, no rope — per Rob Pike's
  rules; validated against a 31k-line file with no perceptible lag
- No Vimscript, no plugins, no splits/tabs, no LSP, no GUI — composition is
  left to tmux
</content>
