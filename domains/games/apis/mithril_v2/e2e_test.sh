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
  PORT="$(sed -n 's/^listening on 0\.0\.0\.0:\([0-9]*\)$/\1/p' "$LOG")"
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
if ! bash -c "${PROBE//\/8096 /\/$PORT }"; then
  echo "FAIL: the compose healthcheck fails against the release: $PROBE" >&2
  exit 1
fi

check health "Ok" "$(curl -sf "http://127.0.0.1:$PORT/health")"
check ladder '{"path":["cold","cord","card","ward","warm"]}' \
  "$(curl -sf -X POST "http://127.0.0.1:$PORT/mithril/v2/wordchain" \
    -H 'content-type: application/json' -d '{"start":"cold","end":"warm"}')"

# SIGTERM reaches the VM, which stops.
kill -TERM "$SERVER_PID"
for _ in $(seq 1 40); do
  kill -0 "$SERVER_PID" 2>/dev/null || exit 0
  sleep 0.25
done
echo "FAIL: the release outlived SIGTERM" >&2
exit 1
