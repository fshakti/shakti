#!/usr/bin/env bash
# Build Shakti for Android (arm64) using the NDK toolchain.
#
#   export ANDROID_NDK=$HOME/Android/Sdk/ndk/26.x
#   ./scripts/ci-android.sh [build-dir]
#
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
: "${ANDROID_NDK:?Set ANDROID_NDK to the NDK root (contains build/cmake/android.toolchain.cmake)}"
tc="$ANDROID_NDK/build/cmake/android.toolchain.cmake"
if [[ ! -f "$tc" ]]; then
  echo "Not found: $tc" >&2
  exit 1
fi
build=${1:-"$root/build/ci-android-arm64"}
cmake -S "$root" -B "$build" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-android.cmake" \
  -DANDROID_NDK="$ANDROID_NDK" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26
cmake --build "$build" -j"$(nproc 2>/dev/null || echo 4)"
# NDK cross-build: run smoke only on device or with qemu; skip ctest here.
echo "OK: $build/libshakti.so (use ../android/ sample app + System.loadLibrary(\"shakti\"))"
