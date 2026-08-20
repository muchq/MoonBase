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

# Both names carry the same bytes because two runtimes look in two places.
#
# BoringSSL's set_default_verify_paths() registers exactly two lookups
# (crypto/x509/x509_d2.cc): a file at X509_get_default_cert_file(), which
# is OPENSSLDIR "/cert.pem" and OPENSSLDIR is /etc/ssl
# (crypto/x509/x509_def.cc), and a hash dir at /etc/ssl/certs that is only
# ever read as <8-hex-hash>.<n> (crypto/x509/by_dir.cc). A bundle at
# /etc/ssl/certs/ca-certificates.crt matches neither lookup, so shipping
# that path alone left every C++ handshake failing exactly as it had
# before, with the file plainly visible in the image.
#
# Go's crypto/x509 does read the Debian bundle path, and the same shared
# rule builds the Go images, so dropping it would break them instead.
for path in etc/ssl/cert.pem etc/ssl/certs/ca-certificates.crt; do
  if ! tar tf "$tar_path" | grep -qx "$path"; then
    echo "FAIL: layer does not carry $path" >&2
    tar tf "$tar_path" >&2
    exit 1
  fi

  # An empty or truncated bundle fails every handshake exactly like no
  # bundle at all, and looks fine on a file listing.
  certs="$(tar xOf "$tar_path" "$path" | grep -c 'BEGIN CERTIFICATE' || true)"
  if [ "$certs" -lt 100 ]; then
    echo "FAIL: only $certs certificates at $path" >&2
    exit 1
  fi

  echo "ok: $certs trust roots at $path"
done
