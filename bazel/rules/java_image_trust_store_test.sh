#!/usr/bin/env bash
# Checks a java image built by linux_oci_java, reading the OCI layout directly
# so it needs no Docker daemon. The container_structure_test that would check
# this is tagged manual and needs a daemon, so it gates nothing in CI.
#
# All three ways this can break fail open. A missing flag, a missing layer, or
# a layer at the wrong path leaves the JVM on the JDK's own cacerts, because
# JSSE falls back to it when javax.net.ssl.trustStore names no file. That
# cacerts still trusts the distrusted roots, and handshakes keep working.
set -euo pipefail

image_dir="$1"
want="$2"

config="$(grep -l '"Entrypoint"' "$image_dir"/blobs/sha256/* | head -1)"
if [ -z "$config" ]; then
  echo "FAIL: no image config in $image_dir" >&2
  exit 1
fi

# The path comes from the entrypoint, so the flag and the layer cannot drift
# apart without this failing.
path="$(grep -o -- '-Djavax.net.ssl.trustStore=[^"]*' "$config" | head -1 | cut -d= -f2)"
if [ -z "$path" ]; then
  echo "FAIL: entrypoint does not set javax.net.ssl.trustStore" >&2
  grep -o '"Entrypoint":\[[^]]*\]' "$config" >&2
  exit 1
fi

found="$(mktemp)"
for blob in "$image_dir"/blobs/sha256/*; do
  if tar xOf "$blob" "${path#/}" >"$found" 2>/dev/null && [ -s "$found" ]; then
    if cmp -s "$found" "$want"; then
      echo "ok: entrypoint trusts $path, and the image carries our store there"
      exit 0
    fi
    echo "FAIL: $path in the image is not //bazel/rules:cacerts" >&2
    exit 1
  fi
done

echo "FAIL: no layer carries $path, so the JVM falls back to the JDK's cacerts" >&2
exit 1
