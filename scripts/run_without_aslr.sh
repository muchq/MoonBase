#!/usr/bin/env bash
# Prefix for TSan tests: clears ASLR for the child so ThreadSanitizer can
# remap its shadow memory. Used via test:tsan --run_under in .bazelrc.
# No-ops when setarch is absent (macOS) — TSan ASLR abort is a Linux CI issue.
set -euo pipefail
if command -v setarch >/dev/null 2>&1; then
  exec setarch "$(uname -m)" -R -- "$@"
fi
exec "$@"
