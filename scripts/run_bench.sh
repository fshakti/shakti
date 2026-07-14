#!/usr/bin/env bash
# Parser throughput benchmark (parse only). Fails if below MIN_PARSE_PER_SEC.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ISO:-$ROOT/shakti}"
ITERS="${ITERS:-100000}"
MIN_PARSE_PER_SEC="${MIN_PARSE_PER_SEC:-50000}"
SRC="${BENCH_SRC:-$ROOT/benchmarks/parse_snippet.ie}"

if [[ ! -x "$ISO" ]]; then
  echo "error: $ISO not found; run make shakti" >&2
  exit 1
fi

out="$("$ISO" --parse-bench --parse-bench-iters "$ITERS" "$SRC")"
echo "$out"
rate="$(echo "$out" | sed -n 's/.*(\([0-9][0-9]*\) parses\/sec).*/\1/p')"
if [[ -z "$rate" ]]; then
  echo "error: could not parse benchmark output" >&2
  exit 1
fi
if (( rate < MIN_PARSE_PER_SEC )); then
  echo "FAIL: $rate parses/sec < minimum $MIN_PARSE_PER_SEC" >&2
  exit 1
fi
echo "bench ok: $rate parses/sec (min $MIN_PARSE_PER_SEC)"
