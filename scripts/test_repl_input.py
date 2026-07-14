#!/usr/bin/env python3
"""Regression: REPL must not repeat characters (input hub KEY_DOWN/UP queue bug)."""

from __future__ import annotations

import os
import pty
import re
import select
import signal
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHAKTI = os.path.join(ROOT, "shakti")


def strip_ansi(text: str) -> str:
    return re.sub(r"\x1b\[[0-9;?]*[ -/]*[@-~]", "", text)


def drain(master: int, timeout: float = 0.1) -> bytes:
    out = b""
    end = time.time() + timeout
    while time.time() < end:
        ready, _, _ = select.select([master], [], [], 0.05)
        if not ready:
            continue
        chunk = os.read(master, 65536)
        if not chunk:
            break
        out += chunk
    return out


def main() -> int:
    if not os.path.isfile(SHAKTI):
        print(f"FAIL: {SHAKTI} not found", file=sys.stderr)
        return 1

    master, slave = pty.openpty()
    pid = os.fork()
    if pid == 0:
        os.close(master)
        os.dup2(slave, 0)
        os.dup2(slave, 1)
        os.dup2(slave, 2)
        os.close(slave)
        os.execl(SHAKTI, "shakti", "-q", "-i")
        os._exit(127)

    os.close(slave)
    buf = b""
    try:
        deadline = time.time() + 5.0
        while time.time() < deadline and b">" not in buf:
            if select.select([master], [], [], 0.1)[0]:
                buf += os.read(master, 65536)

        os.write(master, b"a")
        time.sleep(0.2)
        buf += drain(master, 0.3)
        plain = strip_ansi(buf.decode("utf-8", "replace"))
        if re.search(r">\s*a{2,}", plain):
            print("FAIL: single keystroke produced repeated characters", file=sys.stderr)
            print(plain[-300:], file=sys.stderr)
            return 1

        os.write(master, b"\nquit\n")
        drain(master, 0.3)
    finally:
        os.close(master)
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
        os.waitpid(pid, 0)

    print("repl input ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
