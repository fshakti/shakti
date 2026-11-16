#!/bin/sh
# M2 regression: custom REST headers (e.g. a Discord bot token) must travel
# in curl's -K config file, never in curl argv (visible via ps/proc).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SHAKTI="${SHAKTI:-$ROOT/.build/shakti}"
[ -x "$SHAKTI" ] || SHAKTI="$ROOT/shakti"
WORK="$(mktemp -d /tmp/shakti-rest-argv-XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

mkdir "$WORK/bin"
cat > "$WORK/bin/curl" <<'SHIM'
#!/bin/sh
printf '%s\n' "$@" > "__WORK__/argv.log"
prev=""
for a in "$@"; do
  if [ "$prev" = "-K" ]; then cat "$a" > "__WORK__/cfg.log"; fi
  if [ "$prev" = "-D" ]; then printf 'HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n' > "$a"; fi
  if [ "$prev" = "-o" ]; then printf 'ok-body' > "$a"; fi
  prev="$a"
done
printf '200'
SHIM
chmod +x "$WORK/bin/curl"
sed -i.bak "s|__WORK__|$WORK|g" "$WORK/bin/curl"

SECRET="Bot SECRET-TOKEN-DO-NOT-LEAK-12345"
export CURL_ARGV_LOG="$WORK/argv.log" CURL_CFG_LOG="$WORK/cfg.log"
export SHAKTI_LIB="$ROOT/lib"
OUT="$(SHAKTI_CURL="$WORK/bin/curl" PATH="$WORK/bin:$PATH" "$SHAKTI" -c "
import rest
r : rest.request(\"GET\", \"https://example.com/\", \"\", \"\", {\"Authorization\": \"$SECRET\", \"X-Test\": \"1\"})
print(r)
" 2>&1)" || true
echo "$OUT"

if echo "$OUT" | grep -q "non-private host"; then
  echo "SKIP: DNS for example.com unavailable in this environment"
  exit 0
fi
if [ ! -f "$CURL_ARGV_LOG" ]; then echo "FAIL: curl shim was not invoked"; exit 1; fi
if grep -q "SECRET-TOKEN" "$CURL_ARGV_LOG"; then
  echo "FAIL: secret header leaked into curl argv"; cat "$CURL_ARGV_LOG"; exit 1
fi
echo "ok: secret not in curl argv"
if ! grep -q "SECRET-TOKEN" "$CURL_CFG_LOG"; then
  echo "FAIL: secret header not delivered via -K config"; cat "$CURL_CFG_LOG" 2>/dev/null; exit 1
fi
echo "ok: secret delivered via -K config file"
if grep -q -- "-H" "$CURL_ARGV_LOG"; then echo "FAIL: -H still used in argv"; exit 1; fi
echo "ok: no -H in curl argv"
