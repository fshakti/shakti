#!/usr/bin/env bash
# build_guards.sh — compile/analyze guards for gfx stub headers and JNI paths.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p .build
mkdir -p .build/analyze
if [[ ! -f .build/shakti_version.h ]]; then
  sed 's/.*/#define SHAKTI_PKG_VERSION "&"/' src/VERSION > .build/shakti_version.h
fi

echo "build_guards: gfx_x11 stub (stddef/stdio)"
clang --analyze -Isrc -I.build -DSHAKTI_STANDALONE=1 -DSHAKTI_HAVE_GFX=1 \
  -o .build/analyze/gfx_x11.plist src/gfx_x11.c >/dev/null

echo "build_guards: shakti_jni.o (jni.h include paths)"
make -s shakti_jni.o

# LGPL static-link guard (M5): Makefile must refuse static+LGPL by default.
grep -q "SHAKTI_ALLOW_LGPL_STATIC" Makefile || {
  echo "build_guards: LGPL static guard missing from Makefile" >&2
  exit 1
}
make -n >/dev/null || {
  echo "build_guards: default make -n failed" >&2
  exit 1
}

echo "build_guards ok"
