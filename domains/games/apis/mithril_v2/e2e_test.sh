#!/bin/bash
# Starts the real release from its runfiles and talks to it over HTTP: the
# compose healthcheck exactly as compose runs it, and a ladder.
set -euo pipefail

export RUNFILES_DIR="$TEST_SRCDIR"
LOG="$TEST_TMPDIR/mithril_v2.log"

PORT=0 ./domains/games/apis/mithril_v2/mithril_v2_bin >"$LOG" 2>&1 &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true' EXIT

PORT=""
for _ in $(seq 1 120); do
  PORT="$(sed -n 's/.*"message":"listening on 0\.0\.0\.0:\([0-9]*\)".*/\1/p' "$LOG")"
  [[ -n "$PORT" ]] && break
  kill -0 "$SERVER_PID" 2>/dev/null || break
  sleep 0.25
done
if [[ -z "$PORT" ]]; then
  echo "FAIL: the release never reported a port" >&2
  cat "$LOG" >&2
  exit 1
fi

check() {
  local name="$1" want="$2" got="$3"
  if [[ "$got" != "$want" ]]; then
    echo "FAIL: $name: want '$want', got '$got'" >&2
    cat "$LOG" >&2
    exit 1
  fi
}

# The probe command from compose.yaml's mithril_v2 block, on this port.
PROBE="$(sed -n '/^  mithril_v2:$/,/^  [a-z_0-9-]*:$/p' deploy/consolidated/compose.yaml |
  sed -n "s/^ *test: \[\"CMD\", \"timeout\", \"4\", \"bash\", \"-c\", '\(.*\)'\]$/\1/p")"
if [[ -z "$PROBE" ]]; then
  echo "FAIL: no healthcheck found for mithril_v2 in compose.yaml" >&2
  exit 1
fi
# Compose reads `$$` as a literal `$`; bash would read it as its own PID.
PROBE="${PROBE//\$\$/\$}"
if ! bash -c "${PROBE//\/8096 /\/$PORT }"; then
  echo "FAIL: the compose healthcheck fails against the release: $PROBE" >&2
  exit 1
fi

check health "Ok" "$(curl -sf "http://127.0.0.1:$PORT/health")"
check ladder '{"path":["cold","cord","card","ward","warm"]}' \
  "$(curl -sf -X POST "http://127.0.0.1:$PORT/mithril/v2/wordchain" \
    -H 'content-type: application/json' -d '{"start":"cold","end":"warm"}')"

# A body past 4 MiB is refused by the server's read, not by the handler.
head -c 4194305 /dev/zero | tr '\0' ' ' >"$TEST_TMPDIR/big.json"
check "oversized body" 413 "$(curl -s -o /dev/null -w '%{http_code}' \
  -X POST "http://127.0.0.1:$PORT/mithril/v2/wordchain" \
  -H 'content-type: application/json' --data-binary @"$TEST_TMPDIR/big.json")"
head -c 4194304 /dev/zero | tr '\0' ' ' >"$TEST_TMPDIR/limit.json"
check "body at the limit" 400 "$(curl -s -o /dev/null -w '%{http_code}' \
  -X POST "http://127.0.0.1:$PORT/mithril/v2/wordchain" \
  -H 'content-type: application/json' --data-binary @"$TEST_TMPDIR/limit.json")"

# Every line is one JSON object, the shape mithril v1 logs in: the startup
# lines, and an access line for each request but the probe's.
if grep -v '^{"timestamp":"[^"]*Z","level":"[A-Z]*","message":".*}$' "$LOG"; then
  echo "FAIL: the lines above are not JSON log lines" >&2
  exit 1
fi
check "access lines" 3 "$(grep -c '"event":"access"' "$LOG")"
grep -q '"message":"reading word graph from ' "$LOG" ||
  { echo "FAIL: no startup line for the word graph" >&2; cat "$LOG" >&2; exit 1; }

# Every response above carried a Date header from mist's clock, not from the
# fallback that logs a warning per response.
if grep -q "Failed to lookup date" "$LOG"; then
  echo "FAIL: mist's clock is not running" >&2
  cat "$LOG" >&2
  exit 1
fi

# SIGTERM reaches the VM, which stops.
kill -TERM "$SERVER_PID"
for _ in $(seq 1 40); do
  kill -0 "$SERVER_PID" 2>/dev/null || exit 0
  sleep 0.25
done
echo "FAIL: the release outlived SIGTERM" >&2
exit 1
