#!/usr/bin/env bash
# Two runs on the same bundle must write the same bytes. Each java image
# carries the truststore as a layer, so a store that varies run to run changes
# every java image digest on every cache miss. scripts/impacted-services
# compares those digests, so it would then call every java service impacted.
set -euo pipefail

tool="$1"
bundle="$2"

for run in a b; do
  "$tool" "$bundle" "$TEST_TMPDIR/$run.pem" "$TEST_TMPDIR/$run.jks"
  # A store that stamps the wall clock would differ even between fast runs,
  # but only by the milliseconds between them. Wait long enough to see it.
  sleep 1
done

for out in pem jks; do
  if ! cmp "$TEST_TMPDIR/a.$out" "$TEST_TMPDIR/b.$out"; then
    echo "FAIL: two runs wrote different $out bytes" >&2
    exit 1
  fi
  echo "ok: $out reproducible"
done
