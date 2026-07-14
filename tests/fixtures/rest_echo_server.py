#!/usr/bin/env python3
"""Minimal HTTP echo server for rest tests (backup fixture).

Usage:
  python3 rest_echo_server.py PORT
"""

from __future__ import annotations

import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class EchoHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        body = self.path.encode()
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length) if length else b""
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.end_headers()
        try:
            data = json.loads(raw.decode("utf-8"))
            self.wfile.write(json.dumps(data).encode())
        except json.JSONDecodeError:
            self.wfile.write(raw)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: rest_echo_server.py PORT", file=sys.stderr)
        return 1
    port = int(sys.argv[1])
    HTTPServer(("127.0.0.1", port), EchoHandler).serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
