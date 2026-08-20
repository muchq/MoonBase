#!/usr/bin/env bash
# The layer every image gets, checked without a Docker daemon — the
# container_structure_test that would assert this inside a built image is
# tagged manual and needs a daemon, so it gates nothing in CI.
set -euo pipefail

tar_path="$(find "$TEST_SRCDIR" -name 'ca_certificates_layer.tar' | head -1)"
if [ -z "$tar_path" ]; then
  echo "FAIL: no ca_certificates_layer.tar in runfiles" >&2
  exit 1
fi

# The path is the whole point: BoringSSL's default verify paths look here
# and nowhere else, so a bundle landing anywhere else is a bundle that
# does not exist.
if ! tar tf "$tar_path" | grep -qx 'etc/ssl/certs/ca-certificates.crt'; then
  echo "FAIL: layer does not carry etc/ssl/certs/ca-certificates.crt" >&2
  tar tf "$tar_path" >&2
  exit 1
fi

certs="$(tar xOf "$tar_path" etc/ssl/certs/ca-certificates.crt | grep -c 'BEGIN CERTIFICATE' || true)"
# An empty or truncated bundle fails every handshake exactly like no
# bundle at all, and looks fine on a file listing.
if [ "$certs" -lt 100 ]; then
  echo "FAIL: only $certs certificates in the bundle" >&2
  exit 1
fi

echo "ok: $certs trust roots at etc/ssl/certs/ca-certificates.crt"
