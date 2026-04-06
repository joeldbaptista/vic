#!/usr/bin/env python3
import argparse
import os
import pty
import select
import signal
import sys
import time
from pathlib import Path

DEFAULT_SAMPLE_TEXT = "foo one\nfoo two\nfoo three\n"
DEFAULT_EXPECTED_TEXT = "foo one\noo two\nfoo three\n"


def run_vi_with_keys(vi_path: str, file_path: str, keys: bytes, timeout: float) -> tuple[int, bool]:
    pid, fd = pty.fork()
    if pid == 0:
        os.execvp(vi_path, [vi_path, file_path])

    timed_out = False
    status = None

    try:
        time.sleep(0.25)
        os.write(fd, keys)

        deadline = time.time() + timeout
        while time.time() < deadline:
            ready, _, _ = select.select([fd], [], [], 0.05)
            if fd in ready:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    chunk = b""
                if not chunk:
                    break

            wpid, st = os.waitpid(pid, os.WNOHANG)
            if wpid == pid:
                status = st
                break

        if status is None:
            timed_out = True
            os.kill(pid, signal.SIGTERM)
            try:
                _, status = os.waitpid(pid, 0)
            except ChildProcessError:
                status = 0
    finally:
        try:
            os.close(fd)
        except OSError:
            pass

    if isinstance(status, int) and os.WIFEXITED(status):
        return os.WEXITSTATUS(status), timed_out
    if isinstance(status, int) and os.WIFSIGNALED(status):
        return 128 + os.WTERMSIG(status), timed_out
    return 1, timed_out


def run_case(
    name: str,
    vi_path: str,
    tmp_dir: Path,
    keys: bytes,
    sample_text: str,
    expected_text: str,
) -> tuple[bool, Path]:
    test_file = tmp_dir / f"vi_{name}_test.txt"
    test_file.write_text(sample_text, encoding="utf-8")

    rc, timed_out = run_vi_with_keys(vi_path, str(test_file), keys, timeout=5.0)
    got = test_file.read_text(encoding="utf-8")
    ok = got == expected_text

    print(f"[{name}] rc={rc} timed_out={timed_out}")
    if ok:
        print(f"[{name}] PASS")
    else:
        print(f"[{name}] FAIL")
        print(f"[{name}] expected:\n{expected_text}", end="")
        print(f"[{name}] got:\n{got}", end="")

    return ok, test_file


def main() -> int:
    parser = argparse.ArgumentParser(description="PTY-driven check for vi NORMAL mode *, #, g*, and g# behavior")
    parser.add_argument("--vi", default="./vi", help="Path to vi binary (default: ./vi)")
    parser.add_argument("--tmp-dir", default="/tmp", help="Directory for temporary test files")
    args = parser.parse_args()

    vi_path = args.vi
    if not Path(vi_path).exists():
        print(f"error: vi binary not found: {vi_path}", file=sys.stderr)
        return 2

    tmp_dir = Path(args.tmp_dir)
    tmp_dir.mkdir(parents=True, exist_ok=True)

    cases = [
        {
            "name": "star",
            "keys": b"*x:write\r",
            "sample": DEFAULT_SAMPLE_TEXT,
            "expected": DEFAULT_EXPECTED_TEXT,
        },
        {
            "name": "hash",
            "keys": b"jj#x:write\r",
            "sample": DEFAULT_SAMPLE_TEXT,
            "expected": DEFAULT_EXPECTED_TEXT,
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
    ]

    all_ok = True
    case_files: list[tuple[str, Path]] = []

    for case in cases:
        ok, test_file = run_case(
            case["name"],
            vi_path,
            tmp_dir,
            case["keys"],
            case["sample"],
            case["expected"],
        )
        all_ok = all_ok and ok
        case_files.append((case["name"], test_file))

    for name, test_file in case_files:
        print(f"{name} file: {test_file}")

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
