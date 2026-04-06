# vic

`vic` is a vi clone. It is derived from the vi implementation shipped with
BusyBox and rewritten toward the suckless ideal: small source, no
dependencies, predictable behaviour, and a codebase a single programmer
can hold in their head.

The text buffer is a plain dynamic array — one contiguous heap slab. There
is no gap buffer, no rope, no piece table. Rob Pike's rules apply: keep the
data structure simple, and the algorithms follow naturally. `data/erv.txt`,
a 31 000-line file included in the repository, edits responsively with no
perceptible lag. For the actual use cases of `vic` — source files,
configuration files, prose — the array is fast enough, and the complexity
that a fancier structure would introduce is not justified.

## Build

```sh
make          # produces ./vic
make install  # copies to /usr/bin/vic
```

Requirements: a C99 compiler, POSIX.1-2008, a Linux terminal. No external
libraries.

# Usage

```sh
vic file           # open file for editing
vic -R file        # open read-only
vic -c 'cmd' file  # run an ex command on startup
```

The interface is standard vi. Editing is modal: press `i` to enter insert
mode, `Esc` to return to normal mode.

## Features

### Editing model

- Normal, insert, replace, visual charwise, visual linewise modes.
- Count prefixes on motions and operators: `3dw`, `5j`, `"a2yy`.
- Dot-repeat (`.`) replays the last modifying command.
- Undo (`u`) and redo (`Ctrl-R`), including persistent undo across sessions
  (`:set undofile`).

### Motions

| Keys | Motion |
|------|--------|
| `h j k l` | character and line |
| `w b e W B E` | word and WORD |
| `0 ^ $` | line start / first non-blank / end |
| `gg G {n}G` | file start / end / line n |
| `f F t T ; ,` | character search within line |
| `% ( ) { }` | matching pair, sentence, paragraph |
| `H M L` | screen top / middle / bottom |
| `Ctrl-F Ctrl-B Ctrl-D Ctrl-U Ctrl-E Ctrl-Y` | scroll |

### Operators and text objects

`d` `c` `y` `x` `r` `~` `J` `>` `<` work with counts and motions.

Text objects: `iw` `aw` `iW` `aW`, `i"` `a"` `i'` `a'` `` i` `` `` a` ``,
`i(` `a(` `i[` `a[` `i{` `a{` `i<` `a<`, `it` `at`.

### Search

`/` `?` `n` `N` with POSIX basic regular expressions.
`*` `#` `g*` `g#` search for the word under the cursor.

### Registers and marks

Named registers `"a`–`"z` and the default register. Marks `ma`–`mz`
with `'a`–`'z` jump motions.

### Ex commands

| Command | Action |
|---------|--------|
| `:w` `:q` `:wq` `:x` | write, quit, and combinations |
| `:e file` | edit another file |
| `:r file` | read file into buffer |
| `:s/pat/repl/flags` | substitute |
| `:%s/pat/repl/g` | global substitute |
| `:g/pat/cmd` `:v/pat/cmd` | global and inverse-global |
| `:set option` | toggle or assign an option |

### Options

| Option | Short | Default | Description |
|--------|-------|---------|-------------|
| `autoindent` | `ai` | off | copy indent from previous line |
| `expandtab` | `et` | off | insert spaces instead of tab |
| `ignorecase` | `ic` | off | case-insensitive search |
| `showmatch` | `sm` | off | flash matching bracket |
| `flash` | `fl` | off | flash instead of bell |
| `number` | `nu` | off | show line numbers |
| `relativenumber` | `rnu` | off | show relative line numbers |
| `tabstop` | `ts` | 8 | tab display width |
| `cursorshape` | `cshp` | 1 | normal-mode cursor (DECSCUSR 0–6) |
| `cursorshapeinsert` | `cshpi` | 5 | insert-mode cursor (DECSCUSR 0–6) |
| `undofile` | `uf` | off | persist undo history to disk |

Cursor shape values: `0` terminal default, `1` blinking block, `2` block,
`3` blinking underline, `4` underline, `5` blinking pipe, `6` pipe.

### Syntax highlighting

Highlighting is activated automatically by file extension. Colour
assignments (ANSI 16-colour):

| Token | Colour |
|-------|--------|
| keyword | bold |
| string / character literal | yellow |
| comment | cyan |
| preprocessor | magenta |
| number | green |

Languages and extensions:

| Language | Extensions |
|----------|------------|
| C | `.c` `.h` |
| C++ | `.cc` `.cpp` `.cxx` `.hh` `.hpp` `.hxx` `.inl` |
| Shell | `.sh` `.bash` `.zsh` `.ksh` |
| Markdown | `.md` `.markdown` `.mkd` `.mdwn` `.mdown` |
| SQL | `.sql` |

## Design

### Philosophy

The project follows [Rob Pike's five programming rules](PRINCIPLES.md). Rule 3
and Rule 4 govern the data structure choice: a plain dynamic array is the
simplest correct representation of a text buffer. A gap buffer or rope would
reduce the asymptotic cost of edits in large files, but Pike's rules say
not to pay that complexity cost until measurement demands it. For the files
people actually edit, the array is fine.

Behaviour is encoded as data where possible — dispatch tables, option
bitmasks, colorizer function pointers — rather than hardcoded control flow.
A small engine interprets the table; adding a new language colorizer is one
file and two lines in a registry.

### Code style

[Suckless coding style](STYLE.md): tabs for indentation, `/* */` comments,
return type on its own line, short expressive names, no typedefs for
structs, no external dependencies.

### Buffer model

```
g->text                               g->end
|                                         |
[  h  e  l  l  o  \n  w  o  r  l  d  \n ]
                ^
               g->dot  (cursor)
```

`text`, `end`, and all interior pointers (`dot`, `screenbegin`, `mark[]`)
are pointers into one heap slab. Insert and delete are O(n) memmove from
the edit point to `end`. Capacity doubles up to 1 MB then grows by 1 MB
increments. The sentinel newline at `end - 1` is always maintained.

### Module structure

```
vic.c           command dispatch, main loop
parser.c        MONRAS normal-mode command parser
screen.c        virtual screen, diff/refresh
buffer.c        insert, delete, file I/O
undo.c          undo/redo stack
operator.c      d/c/y/x/r/~ operators
motion.c        cursor motions
range.c         operator-pending motion resolution
textobj.c       text object ranges
wordmotion.c    w/b/e/W/B/E
search.c        regex search, char-search
visual.c        visual mode
editcmd.c       insert/replace mode entry
ex.c            colon command parsing
excore.c        :set and argument expansion
line.c          line arithmetic
codepoint.c     UTF-8 navigation
scan.c          token scanning
term.c          terminal raw mode, cursor shape
status.c        status line
color.c         colorizer registry
color_c.c       C/C++ colorizer
color_sh.c      shell colorizer
color_md.c      Markdown colorizer
color_sql.c     SQL colorizer
```

## Multiple files and tmux

`vic` does not implement splits or tabs. The composable alternative is
tmux: open one `vic` session per pane, let tmux handle layout.

The one thing tmux cannot provide on its own is sharing a yank buffer
between two independent `vic` processes. `vic` solves this with the `"+`
register, backed by a file at `$XDG_CACHE_HOME/vic/yank` (defaulting to
`~/.cache/vic/yank`).

```
pane 1                   pane 2
──────────────────────   ──────────────────────
+yy    yank to file      +p     put from file
+y3j   yank 4 lines
+yw    yank word
+dd    delete to file
```

`+` is the shared-register prefix. It works like a named register prefix
(`"a`, `"b`, …) but always refers to the shared file. `"+` also works for
vim-muscle-memory compatibility. The file is written on every yank and read
on every put, so any number of `vic` sessions on the same machine see the
same content. Register type (linewise, charwise, multi-line) is inferred
from the file content, so `+yy` / `+p` transfers whole lines correctly.

A suggested tmux workflow for editing two related files side by side:

```sh
tmux new-session -s work
tmux split-window -h
# pane 1: vic file_a.c
# pane 2: vic file_b.c
# Ctrl-W to switch panes; "+y / "+p to move text between them
```

## What vic does not do

- Vimscript.
- Plugins.
- Multiple windows or tabs.
- LSP or IDE integration.
- GUI.

See [NOT_SUPPORTED.md](NOT_SUPPORTED.md) for the full list.

## Testing

```sh
make check-regression-pty    # PTY-driven regression suite
make check-sanitizers-pty    # same under ASan and UBSan
```

Tests drive a real `vic` process through a pseudo-terminal, write the
result to disk with `:w`, and assert file content. The process exit code is
not the test signal — file content is.

## License

Derived from BusyBox vi. GPLv2.
