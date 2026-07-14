#!/usr/bin/env bash
# Golden parser tests: input -> AST substring match (parse only, no eval).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${ISO:-$ROOT/shakti}"
CASES="${1:-$ROOT/tests/parse_cases.tsv}"
fail=0
pass=0

if [[ ! -x "$ISO" ]]; then
  echo "error: $ISO not found; run make shakti" >&2
  exit 1
fi

while IFS= read -r line || [[ -n "$line" ]]; do
  [[ -z "$line" || "$line" == \#* ]] && continue
  input="${line%%$'\t'*}"
  want="${line#*$'\t'}"
  got="$("$ISO" --parse-dump -c "$input" 2>/dev/null || true)"
  if [[ "$got" == *"$want"* ]]; then
    pass=$((pass + 1))
  else
    echo "FAIL: $input" >&2
    echo "  want substring: $want" >&2
    echo "  got:            $got" >&2
    fail=$((fail + 1))
  fi
done < "$CASES"

echo "parse_golden: $pass passed, $fail failed"
[[ "$fail" -eq 0 ]]
