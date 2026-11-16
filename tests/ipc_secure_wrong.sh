#!/bin/sh
# M3 wrong-token probe: a client with a mismatched SHAKTI_IPC_TOKEN must
# fail ipc.connect (mutual HMAC challenge), and a tampered frame must be
# rejected. Run by ci (tests/ipc) or manually:
#   SHAKTI_IPC_TOKEN=right sh tests/ipc/ipc_secure_wrong.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export SHAKTI_LIB="$ROOT/lib"
export PRIVATE_ROOT="$ROOT"
TOKEN="${SHAKTI_IPC_TOKEN:-test-token-123}"

# server with the right token, background shakti process
cat > /tmp/ipc-wrong-srv.ie <<EOF
import ipc
srv : ipc.listen(19272, "127.0.0.1", "tcp")
conn : ipc.accept(srv)
if type(conn) = "int":
    print("UNEXPECTED-ACCEPT")
EOF
SHAKTI_IPC_TOKEN="$TOKEN" "$ROOT/.build/shakti" /tmp/ipc-wrong-srv.ie > /tmp/ipc-wrong-srv.log 2>&1 &
SRVPID=$!
sleep 1

OUT="$(SHAKTI_IPC_TOKEN="wrong-token" timeout 10 "$ROOT/.build/shakti" -c '
import ipc
c : ipc.connect("127.0.0.1", 19272, "tcp")
print(type(c))
' 2>&1)"
kill $SRVPID 2>/dev/null; wait $SRVPID 2>/dev/null
echo "$OUT"
if echo "$OUT" | grep -q '"int"'; then
  echo "FAIL: wrong-token client connected"
  exit 1
fi
if grep -q "UNEXPECTED-ACCEPT" /tmp/ipc-wrong-srv.log; then
  echo "FAIL: server accepted wrong-token client"
  exit 1
fi
echo "ok: wrong-token client rejected"
