#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
export SHAKTI_LIB="$ROOT/src/lib"
if [[ -f "$ROOT/integrations/linkedin/.env" ]]; then
  set -a
  # shellcheck disable=SC1091
  source "$ROOT/integrations/linkedin/.env"
  set +a
fi
exec "$ROOT/shakti" "$ROOT/integrations/linkedin/dashboard_server.ie" "$@"
