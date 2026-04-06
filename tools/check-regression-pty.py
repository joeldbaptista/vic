#!/usr/bin/env python3
import argparse
import os
import pty
import re
import select
import signal
import sys
import time
from pathlib import Path
from typing import List, Optional, Tuple

ANSI_RE = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]|\x1b[@-Z\\-_]")
STARTUP_SETTLE_SEC = 0.60


def strip_ansi(data: bytes) -> str:
    text = data.decode("utf-8", errors="ignore")
    text = ANSI_RE.sub("", text)
    return text.replace("\r", "\n")


def decode_wait_status(status: Optional[int]) -> int:
    if status is None:
        return 1
    if os.WIFEXITED(status):
        return os.WEXITSTATUS(status)
    if os.WIFSIGNALED(status):
        return 128 + os.WTERMSIG(status)
    return 1


def wait_for_startup(
	fd: int, 
	pid: int,
	out: bytearray,
	needle: bytes, 
	timeout: float = 2.5
) -> Optional[int]:
    end_time = time.time() + timeout
    while time.time() < end_time:
        status = pump_io(fd, pid, out, 0.05)
        if status is not None:
            return status
        if needle and needle in out:
            return None
    return None


def spawn_vi(vi_path: str, file_path: str) -> Tuple[int, int]:
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(vi_path, [vi_path, file_path])
    return pid, fd


def pump_io(fd: int, pid: int, out: bytearray, seconds: float) -> Optional[int]:
    end_time = time.time() + seconds
    while time.time() < end_time:
        ready, _, _ = select.select([fd], [], [], 0.03)
        if fd in ready:
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                chunk = b""
            if chunk:
                out.extend(chunk)
            else:
                wpid, status = os.waitpid(pid, os.WNOHANG)
                if wpid == pid:
                    return status
                break

        wpid, status = os.waitpid(pid, os.WNOHANG)
        if wpid == pid:
            return status

    wpid, status = os.waitpid(pid, os.WNOHANG)
    if wpid == pid:
        return status
    return None


def finish_process(fd: int, pid: int, out: bytearray, timeout: float = 5.0) -> Tuple[int, bool]:
    status = pump_io(fd, pid, out, timeout)
    if status is None:
        wpid, st = os.waitpid(pid, os.WNOHANG)
        if wpid == pid:
            status = st

    timed_out = status is None

    if timed_out:
        os.kill(pid, signal.SIGTERM)
        try:
            _, status = os.waitpid(pid, 0)
        except ChildProcessError:
            status = 0

    try:
        os.close(fd)
    except OSError:
        pass

    return decode_wait_status(status), timed_out


def run_file_case(name: str, vi_path: str, tmp_dir: Path, keys: bytes, sample: str, expected: str) -> bool:
    file_path = tmp_dir / f"vi_{name}.txt"
    file_path.write_text(sample, encoding="utf-8")

    pid, fd = spawn_vi(vi_path, str(file_path))
    out = bytearray()

    wait_for_startup(fd, pid, out, file_path.name.encode("utf-8"))
    pump_io(fd, pid, out, STARTUP_SETTLE_SEC)
    os.write(fd, keys)
    rc, timed_out = finish_process(fd, pid, out)

    got = file_path.read_text(encoding="utf-8")
    ok = got == expected

    print(f"[{name}] rc={rc} timed_out={timed_out}")
    if ok:
        print(f"[{name}] PASS")
    else:
        print(f"[{name}] FAIL")
        print(f"[{name}] expected:\n{expected}", end="")
        print(f"[{name}] got:\n{got}", end="")
        rendered_tail = strip_ansi(bytes(out))[-1200:]
        if rendered_tail:
            print(f"[{name}] output tail:")
            print(rendered_tail)

    print(f"[{name}] file: {file_path}")
    return ok


def run_insert_live_redraw_case(vi_path: str, tmp_dir: Path) -> bool:
    name = "insert-live-redraw"
    file_path = tmp_dir / "vi_insert_live_redraw.txt"
    file_path.write_text("zzzz\n", encoding="utf-8")

    pid, fd = spawn_vi(vi_path, str(file_path))
    out = bytearray()

    wait_for_startup(fd, pid, out, file_path.name.encode("utf-8"))
    pump_io(fd, pid, out, STARTUP_SETTLE_SEC)

    before_len = len(out)
    os.write(fd, b"iXYZ")
    pump_io(fd, pid, out, 0.40)
    mid = strip_ansi(bytes(out[before_len:]))
    saw_xyz_before_esc = "XYZ" in mid

    os.write(fd, b"\x1b:q!\r")
    rc, timed_out = finish_process(fd, pid, out)

    ok = saw_xyz_before_esc

    print(f"[{name}] rc={rc} timed_out={timed_out}")
    if ok:
        print(f"[{name}] PASS")
    else:
        print(f"[{name}] FAIL")
        print(f"[{name}] expected to see typed text before Esc; mid-output was:")
        print(mid[-400:])

    print(f"[{name}] file: {file_path}")
    return ok


def run_number_toggle_case(vi_path: str, tmp_dir: Path) -> bool:
    name = "set-nu-rnu-screen"
    file_path = tmp_dir / "vi_set_nu_rnu.txt"
    file_path.write_text("alpha\nbeta\ngamma\n", encoding="utf-8")

    pid, fd = spawn_vi(vi_path, str(file_path))
    out = bytearray()

    wait_for_startup(fd, pid, out, file_path.name.encode("utf-8"))
    pump_io(fd, pid, out, STARTUP_SETTLE_SEC)
    os.write(fd, b":set nu\r:set nonu rnu\r:q!\r")
    rc, timed_out = finish_process(fd, pid, out)

    rendered = strip_ansi(bytes(out))
    has_nu_effect = re.search(r"(^|[^0-9])1 +alpha", rendered) is not None
    no_numbered_tilde = re.search(r"\b\d+\s+~", rendered) is None

    ok = has_nu_effect and no_numbered_tilde

    print(f"[{name}] rc={rc} timed_out={timed_out}")
    if ok:
        print(f"[{name}] PASS")
    else:
        print(f"[{name}] FAIL")
        print(f"[{name}] has_nu_effect={has_nu_effect} no_numbered_tilde={no_numbered_tilde}")
        print(f"[{name}] rendered tail:")
        print(rendered[-800:])

    print(f"[{name}] file: {file_path}")
    return ok


def run_visual_highlight_case(vi_path: str, tmp_dir: Path) -> bool:
    name = "visual-highlight-render"
    file_path = tmp_dir / "vi_visual_highlight.txt"
    file_path.write_text("abcde\n", encoding="utf-8")

    pid, fd = spawn_vi(vi_path, str(file_path))
    out = bytearray()

    wait_for_startup(fd, pid, out, file_path.name.encode("utf-8"))
    pump_io(fd, pid, out, STARTUP_SETTLE_SEC)
    os.write(fd, b"v$")
    pump_io(fd, pid, out, 0.35)

    raw = bytes(out)
    segments = re.findall(br"\x1b\[7m([^\x1b]*)\x1b\[m", raw)
    target = next((seg for seg in segments if b"abcde" in seg), b"")
    has_inverse = bool(target)
    no_synthetic_trailing = bool(target) and b" " not in target

    os.write(fd, b"\x1b:q!\r")
    rc, timed_out = finish_process(fd, pid, out)

    ok = has_inverse and no_synthetic_trailing

    print(f"[{name}] rc={rc} timed_out={timed_out}")
    if ok:
        print(f"[{name}] PASS")
    else:
        print(f"[{name}] FAIL")
        print(f"[{name}] expected inverse-video only for actual selected text, without synthetic trailing spaces")
        print(f"[{name}] has_inverse={has_inverse} no_synthetic_trailing={no_synthetic_trailing}")
        print(f"[{name}] output tail:")
        print(strip_ansi(bytes(out))[-800:])

    print(f"[{name}] file: {file_path}")
    return ok


def run_visual_escape_clear_case(vi_path: str, tmp_dir: Path) -> bool:
    name = "visual-esc-clears-highlight"
    file_path = tmp_dir / "vi_visual_esc_clear.txt"
    file_path.write_text("abcde\n", encoding="utf-8")

    pid, fd = spawn_vi(vi_path, str(file_path))
    out = bytearray()

    wait_for_startup(fd, pid, out, file_path.name.encode("utf-8"))
    pump_io(fd, pid, out, STARTUP_SETTLE_SEC)

    os.write(fd, b"v$")
    pump_io(fd, pid, out, 0.30)
    before_esc = len(out)

    os.write(fd, b"\x1b")
    pump_io(fd, pid, out, 0.40)
    delta = bytes(out[before_esc:])

    esc_cleared = b"\x1b[7m" not in delta

    os.write(fd, b":q!\r")
    rc, timed_out = finish_process(fd, pid, out)

    ok = esc_cleared

    print(f"[{name}] rc={rc} timed_out={timed_out}")
    if ok:
        print(f"[{name}] PASS")
    else:
        print(f"[{name}] FAIL")
        print(f"[{name}] expected no inverse-video sequence after Esc")
        print(f"[{name}] output tail:")
        print(strip_ansi(bytes(out))[-800:])

    print(f"[{name}] file: {file_path}")
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description="PTY-driven regression checks for vi")
    parser.add_argument("--vi", default="./vi", help="Path to vi binary (default: ./vi)")
    parser.add_argument("--tmp-dir", default="/tmp", help="Directory for temporary test files")
    args = parser.parse_args()

    vi_path = args.vi
    if not Path(vi_path).exists():
        print(f"error: vi binary not found: {vi_path}", file=sys.stderr)
        return 2

    tmp_dir = Path(args.tmp_dir)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    file_cases = [
        {
            "name": "star",
            "keys": b"*x:write\r",
            "sample": "foo one\nfoo two\nfoo three\n",
            "expected": "foo one\noo two\nfoo three\n",
        },
        {
            "name": "hash",
            "keys": b"jj#x:write\r",
            "sample": "foo one\nfoo two\nfoo three\n",
            "expected": "foo one\noo two\nfoo three\n",
        },
        {
            "name": "gstar",
            "keys": b"g*x:write\r",
            "sample": "foo\nzfooz\nfoo\n",
            "expected": "foo\nzooz\nfoo\n",
        },
        {
            "name": "ghash",
            "keys": b"G0g#x:write\r",
            "sample": "foo\nzfooz\nfoo\n",
            "expected": "foo\nzooz\nfoo\n",
        },
        {
            "name": "j-first-nonblank",
            "keys": b"jiX\x1b:write\r",
            "sample": "top\n    mid\nbottom\n",
            "expected": "top\n    Xmid\nbottom\n",
        },
        {
            "name": "k-first-nonblank",
            "keys": b"GkiX\x1b:write\r",
            "sample": "top\n    mid\nbottom\n",
            "expected": "top\n    Xmid\nbottom\n",
        },
        {
            "name": "down-first-nonblank",
            "keys": b"\x1b[BiX\x1b:write\r",
            "sample": "top\n    mid\nbottom\n",
            "expected": "top\n    Xmid\nbottom\n",
        },
        {
            "name": "up-first-nonblank",
            "keys": b"G\x1b[AiX\x1b:write\r",
            "sample": "top\n    mid\nbottom\n",
            "expected": "top\n    Xmid\nbottom\n",
        },
        {
            "name": "visual-char-delete",
            "keys": b"vld:write\r",
            "sample": "abcde\n",
            "expected": "cde\n",
        },
        {
            "name": "visual-line-delete",
            "keys": b"Vjd:write\r",
            "sample": "one\ntwo\nthree\n",
            "expected": "three\n",
        },
        {
            "name": "visual-put-replace",
            "keys": b"ywj0vllp:write\r",
            "sample": "foo\nbar\n",
            "expected": "foo\nfoo\n",
        },
        {
            "name": "visual-textobj-vi-paren",
            "keys": b"f(lvi(d:write\r",
            "sample": "x (abc) y\n",
            "expected": "x () y\n",
        },
        {
            "name": "visual-textobj-va-paren",
            "keys": b"f(lva(d:write\r",
            "sample": "x (abc) y\n",
            "expected": "x  y\n",
        },
        {
            "name": "visual-textobj-viw",
            "keys": b"wviwd:write\r",
            "sample": "one two three\n",
            "expected": "one  three\n",
        },
        {
            "name": "visual-textobj-vi-quote",
            "keys": b'f"lvi"d:write\r',
            "sample": "x \"abc\" y\n",
            "expected": "x \"\" y\n",
        },
        {
            "name": "visual-textobj-vit",
            "keys": b"f>lvitd:write\r",
            "sample": "<p>hello</p>\n",
            "expected": "<p></p>\n",
        },
        # count-prefixed motion
        {
            "name": "count-motion-3j",
            "keys": b"3jx:write\r",
            "sample": "one\ntwo\nthree\nfour\nfive\n",
            "expected": "one\ntwo\nthree\nour\nfive\n",
        },
        # operator + motion
        {
            "name": "operator-dw",
            "keys": b"dw:write\r",
            "sample": "foo bar\n",
            "expected": "bar\n",
        },
        # operator + count + motion
        {
            "name": "operator-d2w",
            "keys": b"d2w:write\r",
            "sample": "foo bar baz\n",
            "expected": "baz\n",
        },
        # doubled operator — delete line
        {
            "name": "operator-dd",
            "keys": b"dd:write\r",
            "sample": "one\ntwo\nthree\n",
            "expected": "two\nthree\n",
        },
        # doubled operator — yank line and put
        {
            "name": "operator-yyp",
            "keys": b"yyp:write\r",
            "sample": "foo\nbar\n",
            "expected": "foo\nfoo\nbar\n",
        },
        # doubled operator — change line
        {
            "name": "operator-cc",
            "keys": b"ccnew\x1b:write\r",
            "sample": "foo\nbar\n",
            "expected": "new\nbar\n",
        },
        # register-prefixed yank and put
        {
            "name": "register-yank-put",
            "keys": b'"ayyj"ap:write\r',
            "sample": "foo\nbar\n",
            "expected": "foo\nbar\nfoo\n",
        },
        # shift right
        {
            "name": "shift-right",
            "keys": b">>:write\r",
            "sample": "foo\n",
            "expected": "\tfoo\n",
        },
        # shift left
        {
            "name": "shift-left",
            "keys": b"<<:write\r",
            "sample": "\tfoo\n",
            "expected": "foo\n",
        },
        # ZZ — write and quit
        {
            "name": "zz-write-quit",
            "keys": b"xZZ",
            "sample": "hello\n",
            "expected": "ello\n",
        },
        # dot-repeat: dw then . deletes next word
        {
            "name": "dot-repeat-dw",
            "keys": b"dw.:write\r",
            "sample": "foo bar baz\n",
            "expected": "baz\n",
        },
        # dot-repeat: dd then . deletes next line
        {
            "name": "dot-repeat-dd",
            "keys": b"dd.:write\r",
            "sample": "one\ntwo\nthree\n",
            "expected": "three\n",
        },
        # dot-repeat: >> then . indents twice
        {
            "name": "dot-repeat-shift",
            "keys": b">>.:write\r",
            "sample": "foo\n",
            "expected": "\t\tfoo\n",
        },
        # dot-repeat: r, then l . replaces second char
        {
            "name": "dot-repeat-replace-char",
            "keys": b"r,l.:write\r",
            "sample": "abcde\n",
            "expected": ",,cde\n",
        },
        # dot-repeat: cc then . replaces whole line
        {
            "name": "dot-repeat-cc",
            "keys": b"ccnew\x1bj.:write\r",
            "sample": "old\ntext\n",
            "expected": "new\nnew\n",
        },
        # dot-repeat: ciw then . replaces next word
        {
            "name": "dot-repeat-ciw",
            "keys": b"wciwnew\x1b0.:write\r",
            "sample": "foo bar\n",
            "expected": "new new\n",
        },
        # undo: x then u restores deleted char
        {
            "name": "undo-x",
            "keys": b"xu:write\r",
            "sample": "hello\n",
            "expected": "hello\n",
        },
        # undo: dd then u restores deleted line
        {
            "name": "undo-dd",
            "keys": b"ddu:write\r",
            "sample": "one\ntwo\n",
            "expected": "one\ntwo\n",
        },
        # redo: x, u, ctrl-r leaves char deleted again
        {
            "name": "redo-x",
            "keys": b"xu\x12:write\r",
            "sample": "hello\n",
            "expected": "ello\n",
        },
        # search forward: /foo finds and lands on it, x deletes first char
        {
            "name": "search-forward",
            "keys": b"/bar\rx:write\r",
            "sample": "foo\nbar\nbaz\n",
            "expected": "foo\nar\nbaz\n",
        },
        # search backward: ?foo from end finds it
        {
            "name": "search-backward",
            "keys": b"G?foo\rx:write\r",
            "sample": "foo\nbar\nfoo\n",
            "expected": "oo\nbar\nfoo\n",
        },
        # n repeats last search forward (start before first match)
        {
            "name": "search-n-repeat",
            "keys": b"/bar\rnx:write\r",
            "sample": "foo\nbar\nbaz\nbar\n",
            "expected": "foo\nbar\nbaz\nar\n",
        },
        # mark set and jump: ma, move, 'a returns to mark, x deletes there
        {
            "name": "mark-set-jump",
            "keys": b"mamajj'ax:write\r",
            "sample": "one\ntwo\nthree\n",
            "expected": "ne\ntwo\nthree\n",
        },
        # replace mode: R overwrites chars
        {
            "name": "replace-mode",
            "keys": b"RXY\x1b:write\r",
            "sample": "abcde\n",
            "expected": "XYcde\n",
        },
        # visual +y then +p pastes into another location
        {
            "name": "visual-shared-yank-put",
            "keys": b"viw+yj$+p:write\r",
            "sample": "abc\ndef\n",
            "expected": "abc\ndefabc\n",
        },
        # :mark sets a mark; jump back with 'a and delete from there
        {
            "name": "colon-mark",
            "keys": b":mark a\rjj'ax:write\r",
            "sample": "one\ntwo\nthree\n",
            "expected": "ne\ntwo\nthree\n",
        },
        # :2mark b sets mark b at line 2; 'b jumps there, x deletes first char
        {
            "name": "colon-mark-addr",
            "keys": b":2mark b\r'bx:write\r",
            "sample": "one\ntwo\nthree\n",
            "expected": "one\nwo\nthree\n",
        },
        # :r!cmd inserts command output after current line
        {
            "name": "read-shell",
            "keys": b":r!echo hello\r:write\r",
            "sample": "first\nlast\n",
            "expected": "first\nhello\nlast\n",
        },
        # :r!cmd with address inserts after that line
        {
            "name": "read-shell-addr",
            "keys": b":1r!echo inserted\r:write\r",
            "sample": "aaa\nbbb\n",
            "expected": "aaa\ninserted\nbbb\n",
        },
    ]

    all_ok = True
    for case in file_cases:
        all_ok = run_file_case(
            case["name"],
            vi_path,
            tmp_dir,
            case["keys"],
            case["sample"],
            case["expected"],
        ) and all_ok

    all_ok = run_insert_live_redraw_case(vi_path, tmp_dir) and all_ok
    all_ok = run_number_toggle_case(vi_path, tmp_dir) and all_ok
    all_ok = run_visual_highlight_case(vi_path, tmp_dir) and all_ok
    all_ok = run_visual_escape_clear_case(vi_path, tmp_dir) and all_ok

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
