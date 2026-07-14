#!/usr/bin/env bash
# Build and test Shakti for WebAssembly with Emscripten.
# Prerequisite: EMSDK activated (`source emsdk_env.sh`) OR pass EMSDK=/path/to/emsdk
#
#   export EMSDK=~/emsdk && source "$EMSDK/emsdk_env.sh"
#   ./scripts/ci-wasm.sh
#
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
if [[ -n "${EMSDK:-}" && -f "$EMSDK/emsdk_env.sh" ]]; then
  # shellcheck source=/dev/null
  source "$EMSDK/emsdk_env.sh"
fi
if ! command -v emcc >/dev/null 2>&1; then
  echo "Need Emscripten on PATH (run: export EMSDK=… && source \"\$EMSDK/emsdk_env.sh\")" >&2
  exit 1
fi
build=${1:-"$root/build/ci-wasm"}
cmake -S "$root" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-emscripten.cmake"
cmake --build "$build" -j"$(nproc 2>/dev/null || echo 4)"
ctest --test-dir "$build" --output-on-failure
echo "OK: $build/shakti.js"
