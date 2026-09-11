#!/usr/bin/env bash
# The extractor runs against a pinned digest that nobody re-pins often, so
# the cases that matter here are the ones a digest bump changes: how the
# layer spells the member, and whether it carries one at all.
set -euo pipefail

extract="$(find "$TEST_SRCDIR" -name 'extract_ca_certificates.sh' | head -1)"
if [ -z "$extract" ]; then
  echo "FAIL: no extract_ca_certificates.sh in runfiles" >&2
  exit 1
fi

work="$TEST_TMPDIR/work"
bundle="$TEST_TMPDIR/bundle.crt"
printf -- '-----BEGIN CERTIFICATE-----\nnot-a-real-root\n-----END CERTIFICATE-----\n' >"$bundle"

failures=0

fail() {
  echo "FAIL: $1" >&2
  failures=$((failures + 1))
}

# Builds an image dir whose blobs are named by content like a real one, so
# the extractor has to scan rather than guess a path.
make_image() {
  rm -rf "$work"
  mkdir -p "$work/blobs/sha256"
  # A layer that carries no roots, to prove the scan does not stop at the
  # first blob it can read.
  local decoy="$TEST_TMPDIR/decoy"
  rm -rf "$decoy" && mkdir -p "$decoy/usr/bin"
  echo binary >"$decoy/usr/bin/app"
  tar czf "$work/blobs/sha256/decoy" -C "$decoy" ./usr
}

# $1: member prefix ("./" or ""), $2: bundle contents source
add_certs_layer() {
  local prefix="$1" src="$2"
  local layer="$TEST_TMPDIR/layer"
  rm -rf "$layer" && mkdir -p "$layer/etc/ssl/certs"
  cp "$src" "$layer/etc/ssl/certs/ca-certificates.crt"
  tar czf "$work/blobs/sha256/certs" -C "$layer" "${prefix}etc"
}

# Both spellings: distroless tars carried ./etc/... through 2026-09 and
# etc/... after, and tar finds neither by the other's name.
for prefix in "./" ""; do
  make_image
  add_certs_layer "$prefix" "$bundle"
  out="$TEST_TMPDIR/out.crt"
  rm -f "$out"
  if ! "$extract" "$work" "$out" 2>"$TEST_TMPDIR/err"; then
    fail "member spelled '${prefix}etc/...' was not extracted: $(cat "$TEST_TMPDIR/err")"
  elif ! cmp -s "$out" "$bundle"; then
    fail "member spelled '${prefix}etc/...' extracted the wrong bytes"
  else
    echo "ok: extracted member spelled '${prefix}etc/ssl/certs/ca-certificates.crt'"
  fi
done

# No roots anywhere: must fail the build rather than produce an image that
# ships without them.
make_image
out="$TEST_TMPDIR/out.crt"
rm -f "$out"
if "$extract" "$work" "$out" 2>/dev/null; then
  fail "no bundle in any layer should be an error"
elif [ -e "$out" ]; then
  fail "no bundle in any layer left an output file behind"
else
  echo "ok: missing bundle fails the build"
fi

# An empty bundle fails every handshake exactly like a missing one, and
# looks fine on a file listing.
make_image
: >"$TEST_TMPDIR/empty.crt"
add_certs_layer "./" "$TEST_TMPDIR/empty.crt"
rm -f "$out"
if "$extract" "$work" "$out" 2>/dev/null; then
  fail "an empty bundle should be an error"
elif [ -e "$out" ]; then
  fail "an empty bundle left an output file behind"
else
  echo "ok: empty bundle fails the build"
fi

exit "$failures"
