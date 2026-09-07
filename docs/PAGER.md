# Using vic as a pager

vic can read a document from standard input and display it read-only, which
makes it usable as the pager for `man(1)` and for any command whose output
you would rather browse in a vi buffer than in `less`.

Last updated: 2026-09-07.

## The two options

`-` as the file name reads the document from standard input instead of from
disk. The buffer that results has no name, so `:write` must be given a path;
that is deliberate, so a stray `:w` cannot create a file called `-`.

`-p` is pager mode. It implies `-R`, and it strips terminal markup from the
document while loading it.

## Why a plain redirection is not enough

vic reads every keystroke from `STDIN_FILENO`. A pager receives its document
on that same descriptor, so the document and the user's keys would collide:
without special handling vic consumes the piped text as if it were typed.

`-` resolves that in two steps. vic drains standard input to memory before
the editor starts, then reopens `/dev/tty` and `dup2`s it onto
`STDIN_FILENO`. Swapping the descriptor rather than carrying a second file
descriptor through the editor means every later terminal read, `ioctl` and
`tcsetattr` is correct without further change, and it also gives the children
of `:sh` and `:!cmd` a usable standard input.

## Why `-p` is needed for man pages

When `man` writes to a terminal it emits formatting for the pager to render.
vic renders no text attributes, so that markup would appear as literal text —
a manual page would open looking like `^[[4mLS^[[24m(1)`.

`man` splits `MANPAGER` into words and executes it directly rather than
through a shell, so a pipeline such as `col -bx | vic -` cannot be used
there. The stripping therefore has to happen inside vic, which is what `-p`
does. It removes both encodings that nroff and groff produce, and these are
the only two:

1. CSI escape sequences — `ESC [`, parameter bytes, then a final byte.
2. Backspace overstrike — `X\bX` for bold and `_\bX` for underline, which
   groff emits when `GROFF_NO_SGR` is set.

## Configuring man

Install vic first, because the option is only present in builds that include
it:

```sh
sudo make install
```

Then set the pager in your shell startup file:

```sh
export MANPAGER="vic -p -"
```

Both groff configurations work, so no `GROFF_NO_SGR` or `col` setting is
needed alongside it.

## Other uses

```sh
git show | vic -          # browse a commit, escapes left intact
ls -la | vic -p -         # read-only, markup stripped
```

Without `-p` the document is inserted byte for byte, so a pipe can also be
used to start an editing session from a command's output. Save it with
`:w path`.

## Regression coverage

`tools/check-pty.c` drives these cases over a real pty, with the document on
a pipe and the terminal as the controlling terminal, which is exactly how
`man` invokes a pager:

- `stdin-pipe` — piped text reaches the buffer.
- `stdin-keeps-escapes` — without `-p`, escapes are left in the document.
- `stdin-pager-sgr` — `-p` removes CSI sequences.
- `stdin-pager-overstrike` — `-p` removes backspace overstrike.
