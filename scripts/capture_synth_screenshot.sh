#!/usr/bin/env bash
# Capture Shakti Synth window screenshots for docs/images/.
# Requires: DISPLAY, ImageMagick (import), xdotool, shakti built with SHAKTI_SYNTH=1.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${ROOT}/docs/images"
BIN="${ROOT}/shakti"
DEMO="${ROOT}/examples/synth_demo.ie"

if [[ ! -x "$BIN" ]]; then
  echo "missing $BIN — run: SHAKTI_SYNTH=1 make shakti" >&2
  exit 1
fi
if [[ -z "${DISPLAY:-}" ]]; then
  echo "DISPLAY is not set" >&2
  exit 1
fi

mkdir -p "$OUT"
export SHAKTI_LIB="${ROOT}/lib"

"$BIN" "$DEMO" &
PID=$!
trap 'kill "$PID" 2>/dev/null || true' EXIT

WID=""
for _ in $(seq 1 50); do
  WID="$(xdotool search --name "Shakti Synth" 2>/dev/null | head -1 || true)"
  [[ -n "$WID" ]] && break
  sleep 0.2
done
if [[ -z "$WID" ]]; then
  echo "Shakti Synth window not found" >&2
  exit 1
fi

sleep 0.8
import -window "$WID" "${OUT}/synth-default.png"
xdotool windowsize "$WID" 1280 720 2>/dev/null || true
sleep 0.5
import -window "$WID" "${OUT}/synth-wide.png"

echo "wrote ${OUT}/synth-default.png and ${OUT}/synth-wide.png"
