#!/usr/bin/env bash
# exe_realpath.sh — cover readlink→realpath exe discovery (Level-5 race fix).
#
# On Linux, g_lib_path is derived via realpath("/proc/self/exe"). Running the
# binary through a symlink (with SHAKTI_LIB unset) must still resolve to the
# install tree's lib/ directory.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${SHAKTI_BIN:-$ROOT/shakti}"

if [[ ! -x "$BIN" ]]; then
  echo "error: $BIN not found; run make shakti" >&2
  exit 1
fi

case "$(uname -s)" in
  Linux) ;;
  *)
    echo "exe_realpath: skip (Linux-only /proc/self/exe path)"
    exit 0
    ;;
esac

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/shakti-exe-realpath.XXXXXX")"
cleanup() { rm -rf "$tmpdir"; }
trap cleanup EXIT

ln -s "$BIN" "$tmpdir/shakti-link"
cat >"$tmpdir/probe.ie" <<'EOF'
import sql
t: table(x:[1, 2])
r: select x from t
assert(len(r) = 2)
assert(r["x"][1] = 2)
print("exe_realpath ok")
EOF

# Absolute symlink path, foreign cwd, no SHAKTI_LIB — relies on realpath(exe)/lib.
env -u SHAKTI_LIB "$tmpdir/shakti-link" "$tmpdir/probe.ie"
