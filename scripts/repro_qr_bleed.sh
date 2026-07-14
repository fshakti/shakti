#!/usr/bin/env bash
# Reproduce QR banner bleed: parent REPL + child shakti (stderr QR) + stdout prints.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export SHAKTI_LIB="$ROOT/lib"

if [[ ! -x "$ROOT/shakti" ]]; then
  echo "Build first: make prod" >&2
  exit 1
fi

# Parent interactive REPL spawns background child shakti (prints banner QR to stderr),
# then prints short lines — similar visual corruption to proc iteration.
printf '%s\n' \
  'sh("(sleep 0.2; printf \"exit\\n\" | ./shakti) &")' \
  'for x in ["alpha","beta","gamma"]: print(x)' \
  '2+2' \
  'exit' | script -q -c "$ROOT/shakti" /dev/null
