#!/usr/bin/env bash
# Lifts the trust roots out of the pinned distroless image's layer blobs.
#
# The member is spelled two ways in the wild — distroless tars carried
# ./etc/ssl/certs/ca-certificates.crt through 2026-09 and etc/ssl/... after
# — and GNU tar matches the name exactly as written, so neither spelling
# finds the other. Hardcoding one turns the next digest bump into "no roots
# in the layers" with the file sitting right there in the image.
#
# Failing loudly matters: an image with no roots builds and ships fine and
# then fails every outbound handshake with CERTIFICATE_VERIFY_FAILED.
set -euo pipefail

image_dir="$1"
out="$2"

for blob in "$image_dir"/blobs/sha256/*; do
  for member in ./etc/ssl/certs/ca-certificates.crt etc/ssl/certs/ca-certificates.crt; do
    if tar xOf "$blob" "$member" >"$out.part" 2>/dev/null && [ -s "$out.part" ]; then
      mv "$out.part" "$out"
      exit 0
    fi
  done
done

rm -f "$out.part"
echo "no etc/ssl/certs/ca-certificates.crt in the pinned distroless layers" >&2
exit 1
