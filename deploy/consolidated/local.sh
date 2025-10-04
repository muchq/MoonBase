#!/usr/bin/env bash

trap 'kill $(jobs -p) 2>/dev/null; exit' SIGINT SIGTERM EXIT

PORT=8080 DEV_MODE=1 bazel run //go/games_ws_backend 2>&1 | sed 's/^/[games_ws_backend] /' &
PORT=8081 bazel run //cpp/portrait 2>&1 | sed 's/^/[portrait] /' &
PORT=8083 bazel run //rust/mithril 2>&1 | sed 's/^/[mithril] /' &
PORT=8084 bazel run //rust/posterize 2>&1 | sed 's/^/[posterize] /' &
caddy run --config deploy/consolidated/Caddyfile.local 2>&1 | sed 's/^/[caddy] /' &

echo "$(tput setaf 3)Caddy listening on port 2015...\n$(tput sgr0)"

wait
