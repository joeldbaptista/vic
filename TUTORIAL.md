# vic Tutorial

`vic` is a small vi/vim-compatible editor. This document covers every
feature currently implemented.

---

## Invocation

```
vic [options] file [file ...]
vic -R file          # open read-only
vic -c 'cmd' file    # run an ex command after opening
vic -H               # print feature list
vic -h               # print usage
```

Multiple files are edited in sequence. `:n` advances to the next file;
`:prev`/`:p` goes back; `:rew` restarts from the first.

---

## Modes

| Mode    | How to enter                   | How to leave  |
|---------|--------------------------------|---------------|
| Normal  | startup / `ESC`                | —             |
| Insert  | `i a I A o O s S`             | `ESC`         |
| Replace | `R`                            | `ESC`         |
| Visual  | `v` (char), `V` (line)         | `ESC` or same key |
| Command | `:`                            | `Enter` / `ESC` |

---

## Normal Mode

### Counts

Any motion or operator can be prefixed with a count: `3j` moves down 3
lines; `d2w` deletes 2 words; `5>>` indents 5 lines.

### Cursor motion

| Key(s)                    | Action                            |
|---------------------------|-----------------------------------|
| `h` `j` `k` `l`           | left / down / up / right          |
| Arrow keys                | same                              |
| `w` `W`                   | forward to next word / WORD start |
| `b` `B`                   | backward to word / WORD start     |
| `e` `E`                   | forward to word / WORD end        |
| `0`                       | beginning of line                 |
| `^`                       | first non-blank of line           |
| `$`                       | end of line                       |
| `gg` / `1G`               | first line                        |
| `G`                       | last line (or line N with `NG`)   |
| `H` `M` `L`               | top / middle / bottom of screen   |
| `{` `}`                   | previous / next paragraph         |
| `Ctrl-F` `Ctrl-B`         | page forward / backward           |
| `Ctrl-D` `Ctrl-U`         | half-page down / up               |
| `Ctrl-E` `Ctrl-Y`         | scroll one line down / up         |
| `%`                       | jump to matching bracket `()[]{}`  |
| `f` `F`                   | find char forward / backward on line |
| `t` `T`                   | to just before char forward / backward |
| `;` `,`                   | repeat last `f/F/t/T` forward / backward |

### Search

| Key    | Action                               |
|--------|--------------------------------------|
| `/pat` | search forward for `pat` (POSIX ERE) |
| `?pat` | search backward                      |
| `n`    | repeat search in same direction      |
| `N`    | repeat search in opposite direction  |
| `*`    | search forward for word under cursor |
| `#`    | search backward for word under cursor |
| `g*`   | like `*` but no whole-word boundary  |
| `g#`   | like `#` but no whole-word boundary  |

### Marks

| Key   | Action                                    |
|-------|-------------------------------------------|
| `ma`  | set mark `a` at current position          |
| `'a`  | jump to line containing mark `a`          |
| `''`  | jump to previous context position         |

Marks `a`–`z` are available. Visual mode automatically sets `'<` and `'>`.

### Operators

Operators act on a motion: `d$`, `c2w`, `y'a`, etc.

| Operator | Action                |
|----------|-----------------------|
| `d`      | delete                |
| `c`      | change (delete + insert) |
| `y`      | yank (copy)           |
| `>`      | indent right          |
| `<`      | indent left           |
| `~`      | toggle case           |

Doubling an operator applies it to the current line: `dd`, `cc`, `yy`, `>>`, `<<`.

### Shortcuts that combine operator + motion

| Key  | Action                          |
|------|---------------------------------|
| `D`  | delete to end of line (`d$`)    |
| `C`  | change to end of line (`c$`)    |
| `x`  | delete character under cursor   |
| `X`  | delete character before cursor  |
| `s`  | substitute character (= `cl`)   |
| `S`  | substitute line (= `cc`)        |
| `J`  | join current line with next     |
| `r`  | replace single character        |

### Text objects (inside operators)

| Object | Selects                       |
|--------|-------------------------------|
| `iw`   | inner word                    |
| `aw`   | a word (includes whitespace)  |
| `i(`   | inner `()` block              |
| `a(`   | outer `()` block              |
| `i[`   | inner `[]` block              |
| `i{`   | inner `{}` block              |
| `it`   | inner XML/HTML tag            |
| `i"`   | inner double-quoted string    |
| `i'`   | inner single-quoted string    |

### Put / paste

| Key  | Action                                   |
|------|------------------------------------------|
| `p`  | put (paste) after cursor / below line    |
| `P`  | put before cursor / above line           |

### Registers

Prefix any yank, delete, or put with `"x` to use named register `x`
(`a`–`z`). The `"+` register reads/writes the shared (clipboard) register.

```
"ayy        yank current line into register a
"ap         put contents of register a
"+p         put from shared register
```

### Undo / Redo

| Key      | Action             |
|----------|--------------------|
| `u`      | undo               |
| `Ctrl-R` | redo               |
| `U`      | restore current line to state at entry |

### Repeat

`.` repeats the last modifying command (including its count and register).

### Screen / misc

| Key      | Action                        |
|----------|-------------------------------|
| `Ctrl-L` | redraw screen                 |
| `Ctrl-G` | show status (filename, lines) |
| `ZZ`     | write and quit                |
| `ZQ`     | quit without writing          |

---

## Insert Mode

Enter insert mode with `i` (before cursor), `a` (after), `I` (line
start), `A` (line end), `o` (new line below), `O` (new line above).

| Key         | Action                          |
|-------------|---------------------------------|
| `ESC`       | return to Normal mode           |
| `Ctrl-H`    | backspace                       |
| `Ctrl-D`    | remove one indent level         |
| `Ctrl-V`    | insert next character literally |
| `Backspace` | same as `Ctrl-H`                |

With `autoindent` set, new lines inherit the indentation of the previous
line. With `expandtab` set, Tab inserts spaces instead of a tab character.

---

## Replace Mode

`R` enters Replace mode: typed characters overwrite existing text.
`r` replaces only the character under the cursor, then returns to Normal.

---

## Visual Mode

`v` enters character-wise visual mode; `V` enters line-wise visual mode.
Extend the selection with any motion command.

Operators available in visual mode:

| Key       | Action                         |
|-----------|--------------------------------|
| `d`       | delete selection               |
| `c`       | change selection               |
| `y`       | yank selection                 |
| `>`       | indent selection               |
| `<`       | de-indent selection            |
| `~`       | toggle case of selection       |
| `:`       | enter colon command with `'<,'>` pre-filled |

After leaving visual mode, `'<` and `'>` marks point to the start and end
of the last selection and can be used as ex addresses.

---

## Colon Commands

### File and session

| Command               | Action                                     |
|-----------------------|--------------------------------------------|
| `:w`                  | write file                                 |
| `:w filename`         | write to filename                          |
| `:w!`                 | force write (overwrite existing)           |
| `:q`                  | quit (fails if unsaved changes)            |
| `:q!`                 | quit without saving                        |
| `:wq` / `:x`          | write and quit                             |
| `:wn`                 | write and advance to next file             |
| `:e file`             | edit file (fails if unsaved; `:e!` forces) |
| `:n`                  | next file in argument list                 |
| `:prev` / `:p`        | previous file                              |
| `:rew`                | rewind to first file                       |

### Editing

| Command               | Action                                 |
|-----------------------|----------------------------------------|
| `:[range]d`           | delete lines                           |
| `:[range]y`           | yank lines                             |
| `:[range]s/F/R/[g]`   | substitute; `g` flag replaces all on line |
| `:[range]g/pat/cmd`   | run `cmd` on lines matching `pat`      |
| `:[range]v/pat/cmd`   | run `cmd` on lines NOT matching `pat`  |
| `:r file`             | read file below cursor                 |
| `:r! cmd`             | read shell command output below cursor |
| `:[addr]`             | jump to line number                    |
| `:=`                  | print current line number              |
| `:list`               | list current line showing control chars |

### Marks and info

| Command    | Action                        |
|------------|-------------------------------|
| `:ma x`    | set mark `x` (same as `mx`)   |
| `:fi name` | rename current file           |
| `:f`       | reprint file info on status bar |
| `:ve`      | show version                  |
| `:fe`      | show feature list             |

### Shell

| Command   | Action                                       |
|-----------|----------------------------------------------|
| `:! cmd`  | run shell command (press Enter to return)    |
| `:!`      | launch `$SHELL` interactively               |

### Set options

```
:set                     show all current options
:set option              enable a boolean option
:set nooption            disable a boolean option
:set option=value        set a value option
```

| Option                          | Abbrev    | Default | Meaning                                 |
|---------------------------------|-----------|---------|----------------------------------------- |
| `autoindent`                    | `ai`      | off     | new lines inherit indentation           |
| `expandtab`                     | `et`      | off     | Tab key inserts spaces                  |
| `flash`                         | `fl`      | off     | flash screen instead of beeping         |
| `ignorecase`                    | `ic`      | off     | case-insensitive search                 |
| `showmatch`                     | `sm`      | off     | briefly jump to matching bracket on insert |
| `tabstop=N`                     | `ts=N`    | 8       | visual width of a tab character         |
| `number`                        | `nu`      | off     | show absolute line numbers              |
| `relativenumber`                | `rnu`     | off     | show relative line numbers              |
| `cursorshape=N`                 | `cshp=N`  | —       | terminal cursor shape in normal mode (1=block, 2=underline, 3=bar) |
| `cursorshapeinsert=N`           | `cshpi=N` | —       | terminal cursor shape in insert mode    |
| `undofile`                      | `uf`      | off     | persist undo history across sessions    |

---

## Address Syntax

Most colon commands accept an address or range before the command name.

| Syntax       | Meaning                              |
|--------------|--------------------------------------|
| `N`          | line number N                        |
| `.`          | current line                         |
| `$`          | last line                            |
| `%`          | whole file (same as `1,$`)           |
| `'a`         | line containing mark `a`             |
| `'<` `'>`    | start/end of last visual selection   |
| `/pattern/`  | next line matching pattern           |
| `?pattern?`  | previous line matching pattern       |
| `addr+N`     | N lines after addr                   |
| `addr-N`     | N lines before addr                  |
| `a1,a2`      | range from a1 to a2                  |

Examples:
```
:1,10d          delete lines 1-10
:'<,'>s/foo/bar/g   substitute in visual selection
:g/^#/d         delete all comment lines
:%s/\t/  /g     replace all tabs with two spaces
```

---

## `:run` Commands

`:run` (abbreviated `:ru`) applies a built-in transformation to the
current buffer or an optional address range.

```
:[range] run command [args]
```

Without a range, the entire buffer is used.

### Text case

```
:run upper          uppercase all text in range
:run lower          lowercase all text in range
```

### Whitespace

```
:run trim           strip trailing whitespace from each line
:run deindent       remove one indent level from each line
:run deindent N     remove N indent levels
:run wrap N         hard-wrap lines at column N (N must be >= 4)
```

Example — wrap a long paragraph at 72 columns:
```
:'<,'>run wrap 72
```

### Lines

```
:run sort           sort lines alphabetically
:run sort -r        sort in reverse order
:run uniq           remove consecutive duplicate lines
:run number         prepend "1. 2. 3. …" to each line
```

Example — sort and deduplicate:
```
:%run sort
:%run uniq
```

### Alignment

```
:run align CHAR     align all lines so the first occurrence of CHAR
                    starts at the same column (padding with spaces)
```

Example — align `=` signs in a declaration block:
```
:'<,'>run align =
```

Before:
```
int x = 1;
long result = 42;
char c = 'a';
```
After:
```
int x        = 1;
long result  = 42;
char c       = 'a';
```

### Encoding

```
:run urlencode      percent-encode the range (RFC 3986 unreserved chars kept)
:run urldecode      decode percent-encoded text (+ decoded as space)

:run base64enc      base-64 encode the range (76-char wrapped lines)
:run base64dec      base-64 decode the range

:run jsonesc        escape the range as a JSON string value
                    (" \ newline tab etc. become \", \\, \n, \t …)
:run jsonunesc      unescape a JSON-escaped string back to plain text
                    (\uXXXX decoded to UTF-8)
```

### Statistics

```
:run wc             count lines, words, bytes in range (shown on status bar)
:run col            report maximum line width in range
:run freq           show top-5 identifier frequencies in range
```

Example output:
```
:run wc             →  3 lines  12 words  67 bytes
:run col            →  max column width: 42
:run freq           →  freq (8 unique):  foo×4  bar×3  baz×2
```

### Hashing (FNV-1a)

```
:run hash               hash the entire range; show decimal + hex on status bar
:run hash mod N         per-identifier hash%N; report total identifiers
                        and collision count (N = 1..65536)
:run hash replace       replace every identifier in range with its raw
                        64-bit FNV-1a hash value (as a decimal literal)
:run hash replace mod N replace every identifier with hash%N
```

Example:
```
:run hash replace mod 1000
```
Turns `hello world foo` into `210 716 984` (each word replaced by its
hash mod 1000).

### Identifier case conversion

```
:run convert c2s    camelCase → snake_case
:run convert s2c    snake_case → camelCase (lowerCamelCase)
:run convert s2uc   snake_case → UpperCamelCase (PascalCase)
```

Only identifier tokens (`[A-Za-z_][A-Za-z0-9_]*`) are modified; all
other text is passed through unchanged.

Examples:
```
myFunctionName  →  (c2s)  my_function_name
my_function     →  (s2c)  myFunction
my_function     →  (s2uc) MyFunction
```

### Debugging / scripting

```
:run echo hello world   print arguments to status bar (no range effect)
```

---

## Syntax Highlighting

Highlighting activates automatically based on file extension.

| Language | Extensions                              |
|----------|-----------------------------------------|
| C        | `.c` `.h`                               |
| C++      | `.cc` `.cpp` `.cxx` `.hpp` `.hh` `.hxx` |
| Shell    | `.sh`                                   |
| Markdown | `.md`                                   |
| SQL      | `.sql`                                  |
| Python   | `.py`                                   |

Highlighted elements vary by language but include:
- **Keywords** — language reserved words
- **Numbers** — integer, float, hex, octal, binary, complex (Python)
- **Strings** — including triple-quoted strings (Python), escape sequences
- **Comments** — line and block comments
- **Preprocessor** — `#include`, `#define`, etc. (C/C++)
- **Decorators** — `@name` (Python)

---

## Startup Configuration

On startup vic reads, in order:

1. The `EXINIT` environment variable (treated as a sequence of ex commands).
2. `~/.exrc` — if it exists, is owned by the current user, and is not
   group- or world-writable.

Any valid colon command can appear in these files, one per line:

```
set ai et ts=4
set ic
set nu
```

---

## Tips and Examples

**Jump to a specific line:**
```
:42
```

**Delete all blank lines:**
```
:g/^$/d
```

**Comment out selected lines (prepend `// `):**
```
:'<,'>s/^/\/\/ /
```

**Sort a visual selection in reverse:**
```
:'<,'>run sort -r
```

**Count words in current file:**
```
:%run wc
```

**Align a struct field list on `=`:**
```
:'<,'>run align =
```

**Rename all snake_case identifiers to camelCase:**
```
:%run convert s2c
```

**Show hash of a suspicious string:**
```
:'<,'>run hash
```
