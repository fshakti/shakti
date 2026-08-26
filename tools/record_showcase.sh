#!/usr/bin/env bash
# Capture only the Shakti gfx window from examples/showcase.ie → MP4 → .iefs.
# Runs the demo on a private 1920x1080 Xvfb so no other windows exist, then
# moves the gfx window to fill that screen.
#   make record-showcase
# Needs ffmpeg, ffprobe, xdotool, Xvfb.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export SHAKTI_LIB="$ROOT/lib"

OUT="${1:-$ROOT/.build/showcase.mp4}"
RAW="$ROOT/.build/showcase_raw.mp4"
BLOB="$ROOT/.build/showcase_blob.iefs"
INDEX="$ROOT/.build/showcase_index.iefs"
DONE="$ROOT/.build/showcase_record_done"
PIDF="$ROOT/.build/showcase_demo_pid"
PACK="$ROOT/.build/iefs_pack_cvec"

CAPTURE_W=1920
CAPTURE_H=1080

mkdir -p "$ROOT/.build"

if [ ! -x "$ROOT/shakti" ] && [ ! -x "$ROOT/.build/shakti" ]; then
  echo "ERROR: build shakti first (make prod)" >&2
  exit 1
fi
if [ ! -x "$PACK" ]; then
  echo "ERROR: missing $PACK — run: make iefs-pack-cvec" >&2
  exit 1
fi
command -v ffmpeg >/dev/null 2>&1 || { echo "ERROR: ffmpeg not found" >&2; exit 1; }
command -v ffprobe >/dev/null 2>&1 || { echo "ERROR: ffprobe not found" >&2; exit 1; }
command -v xdotool >/dev/null 2>&1 || { echo "ERROR: xdotool not found" >&2; exit 1; }
command -v Xvfb >/dev/null 2>&1 || { echo "ERROR: Xvfb not found" >&2; exit 1; }
command -v xdpyinfo >/dev/null 2>&1 || { echo "ERROR: xdpyinfo not found" >&2; exit 1; }

VDISPLAY=""
for n in 99 98 97 96 95; do
  if ! xdpyinfo -display ":$n" >/dev/null 2>&1; then
    VDISPLAY=":$n"
    break
  fi
done
if [ -z "$VDISPLAY" ]; then
  echo "ERROR: no free X display in :95–:99" >&2
  exit 1
fi

gfx_window_ids() {
  local id name
  for id in $(DISPLAY="$VDISPLAY" xdotool search --name 'Shakti' 2>/dev/null || true); do
    name=$(DISPLAY="$VDISPLAY" xdotool getwindowname "$id" 2>/dev/null || true)
    printf '%s\n' "$id"
  done
}

fill_screen() {
  local id="$1"
  DISPLAY="$VDISPLAY" xdotool windowmap "$id" 2>/dev/null || true
  DISPLAY="$VDISPLAY" xdotool windowmove "$id" 0 0 2>/dev/null || true
  DISPLAY="$VDISPLAY" xdotool windowsize "$id" "$CAPTURE_W" "$CAPTURE_H" 2>/dev/null || true
}

XVFBPID=""
SHPID=""
FFPID=""
cleanup() {
  kill -INT "${FFPID:-}" 2>/dev/null || true
  kill "${SHPID:-}" 2>/dev/null || true
  kill "${XVFBPID:-}" 2>/dev/null || true
  wait "${FFPID:-}" 2>/dev/null || true
  wait "${SHPID:-}" 2>/dev/null || true
  wait "${XVFBPID:-}" 2>/dev/null || true
}
trap cleanup EXIT

if [ -f "$PIDF" ]; then
  old=$(cat "$PIDF" || true)
  if [ -n "${old:-}" ] && kill -0 "$old" 2>/dev/null; then kill "$old" 2>/dev/null || true; fi
fi
rm -f "$DONE" "$RAW" "$OUT" "$PIDF" "$BLOB" "$INDEX"

Xvfb "$VDISPLAY" -screen 0 "${CAPTURE_W}x${CAPTURE_H}x24" -nolisten tcp +extension XTEST \
  >/dev/null 2>&1 &
XVFBPID=$!
for i in $(seq 1 50); do
  if xdpyinfo -display "$VDISPLAY" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done
if ! xdpyinfo -display "$VDISPLAY" >/dev/null 2>&1; then
  echo "ERROR: Xvfb $VDISPLAY did not start" >&2
  exit 1
fi

(
  cd "$ROOT"
  echo $$ > "$PIDF"
  export DISPLAY="$VDISPLAY"
  export SHAKTI_LIB="$ROOT/lib"
  ./shakti examples/showcase.ie
  date +%s.%N > "$DONE"
) &
SHPID=$!

for i in $(seq 1 80); do
  if [ -n "$(gfx_window_ids)" ]; then
    break
  fi
  if [ -f "$DONE" ]; then
    echo "ERROR: showcase exited before a gfx window opened" >&2
    exit 1
  fi
  sleep 0.1
done

WID=""
for id in $(gfx_window_ids); do
  WID="$id"
  break
done
if [ -z "$WID" ]; then
  echo "ERROR: no Shakti gfx window on $VDISPLAY" >&2
  exit 1
fi
fill_screen "$WID"
sleep 0.15

# Whole private display — only the demo window lives there.
ffmpeg -y -nostdin -loglevel error \
  -f x11grab -draw_mouse 0 -video_size "${CAPTURE_W}x${CAPTURE_H}" -framerate 30 \
  -i "${VDISPLAY}.0" \
  -c:v libx264 -pix_fmt yuv420p -preset veryfast -crf 20 \
  "$RAW" &
FFPID=$!

for i in $(seq 1 360); do
  if [ -f "$DONE" ]; then
    sleep 0.35
    break
  fi
  if (( i % 4 == 0 )) && [ -n "$WID" ]; then
    fill_screen "$WID" >/dev/null
  fi
  sleep 0.25
done

kill -INT "$FFPID" 2>/dev/null || true
wait "$FFPID" 2>/dev/null || true
FFPID=""
wait "$SHPID" 2>/dev/null || true
SHPID=""

if [ ! -s "$RAW" ]; then
  echo "ERROR: capture missing or empty: $RAW" >&2
  exit 1
fi

ffmpeg -y -nostdin -loglevel error -i "$RAW" -c copy "$OUT"
rm -f "$RAW"
echo "wrote $OUT"
ffprobe -v error -show_entries format=duration,size -show_entries stream=width,height -of default=nw=1 "$OUT"

"$PACK" "$OUT" "$BLOB"
echo "wrote $BLOB"

VER=$(tr -d '[:space:]' < "$ROOT/src/VERSION")
DUR=$(ffprobe -v error -select_streams v:0 -show_entries format=duration -of default=nw=1:nk=1 "$OUT")
WIDTH=$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of default=nw=1:nk=1 "$OUT")
HEIGHT=$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of default=nw=1:nk=1 "$OUT")
BYTES=$(stat -c '%s' "$OUT")
SHA=$(sha256sum "$OUT" | awk '{print $1}')
RECORDED=$(date -Iseconds)

export SHAKTI_IEFS_NAME="shakti-showcase"
export SHAKTI_IEFS_VERSION="$VER"
export SHAKTI_IEFS_RECORDED_AT="$RECORDED"
export SHAKTI_IEFS_DURATION="$DUR"
export SHAKTI_IEFS_WIDTH="$WIDTH"
export SHAKTI_IEFS_HEIGHT="$HEIGHT"
export SHAKTI_IEFS_BYTES="$BYTES"
export SHAKTI_IEFS_SHA256="$SHA"
export SHAKTI_IEFS_MP4="$OUT"
export SHAKTI_IEFS_BLOB="$BLOB"
export SHAKTI_IEFS_INDEX="$INDEX"

./shakti tools/iefs_index_demo.ie

if [ -d "$HOME/Downloads" ]; then
  cp -f "$OUT" "$HOME/Downloads/shakti_showcase.mp4"
  echo "copied $HOME/Downloads/shakti_showcase.mp4"
fi
