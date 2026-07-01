#!/usr/bin/env python3
"""Send one OSC message over UDP. Used by import sonicpi (python3 fallback)."""

from __future__ import annotations

import socket
import struct
import sys


def pad(b: bytes) -> bytes:
    return b + (b"\x00" * ((4 - len(b) % 4) % 4))


def main() -> None:
    host, port_s, path, types = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    args = sys.argv[5:]
    port = int(port_s)
    addr = pad(path.encode("utf-8") + b"\x00")
    tags = pad(("," + types).encode("utf-8") + b"\x00")
    body = b""
    for ty, val in zip(types, args):
        if ty == "i":
            body += struct.pack(">i", int(val))
        elif ty == "f":
            body += struct.pack(">f", float(val))
        elif ty == "s":
            body += pad(str(val).encode("utf-8") + b"\x00")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(addr + tags + body, (host, port))


if __name__ == "__main__":
    main()
