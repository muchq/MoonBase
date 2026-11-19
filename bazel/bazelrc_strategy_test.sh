#!/usr/bin/env bash
# No config may run every action unsandboxed. --spawn_strategy=local reaches
# compiles too, and the llvm module's hermetic glibc only builds sandboxed:
# start.S then sees glibc's generic bits/wordsize.h, which is an #error stub.
# A warm disk cache hides that; the first cold build fails. Scope local
# execution to the action that needs it (--strategy=TestRunner=local).
set -euo pipefail

BAZELRC=${1:-.bazelrc}

if offenders=$(grep -nE '^[^#]*--spawn_strategy[= ]+(local|standalone)\b' "$BAZELRC"); then
  echo "FAIL: $BAZELRC runs all actions unsandboxed:" >&2
  echo "$offenders" >&2
  exit 1
fi
echo "PASS: no config sets a global local spawn strategy"
