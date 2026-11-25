#!/bin/bash
# What App Store Connect validates in the bundle's inputs, checked on Linux so a
# regression shows up on every PR instead of at the first signed upload.
set -uo pipefail

PKG=domains/games/apps/wordchains_ios
PLIST=$PKG/Info.plist
ICONSET=$PKG/Sources/Resources/Assets.xcassets/AppIcon.appiconset

FAILURES=0
pass() { echo "  ok    $1"; }
fail() {
  echo "  FAIL  $1"
  FAILURES=$((FAILURES + 1))
}
check() {
  local desc=$1
  shift
  if "$@"; then pass "$desc"; else fail "$desc"; fi
}

# The lines following <key>$1</key> up to the next <key>, whitespace stripped.
value_of() {
  awk -v key="<key>$1</key>" '
    { line = $0; gsub(/^[ \t]+|[ \t]+$/, "", line) }
    found && line ~ /^<key>/ { exit }
    found && line != "" { print line }
    line == key { found = 1 }
  ' "$PLIST" | tr -d '\n'
}

# Big-endian unsigned int of $2 bytes at offset $1 in file $3.
be_uint() { od -An -tu1 -j "$1" -N "$2" "$3" | awk '{ for (i = 1; i <= NF; i++) n = n * 256 + $i } END { print n }'; }
# Chunk types before the image data. tRNS there makes an RGB image transparent.
chunks_before_idat() {
  local offset=8 size len type
  size=$(wc -c <"$1")
  while [ "$offset" -lt "$size" ]; do
    len=$(be_uint "$offset" 4 "$1")
    type=$(od -An -c -j $((offset + 4)) -N 4 "$1" | tr -d ' \n')
    [ "$type" = IDAT ] && return
    echo "$type"
    offset=$((offset + 12 + len))
  done
}
is_png() { [ "$(od -An -tx1 -N 8 "$1" | tr -d ' \n')" = 89504e470d0a1a0a ]; }

echo "wordchains_ios app store"

command -v jq >/dev/null || fail "jq is installed (reads Contents.json)"

# The release builds arm64 only; a capability the binary lacks fails validation.
check "requires arm64, not armv7" \
  test "$(value_of UIRequiredDeviceCapabilities)" = "<array><string>arm64</string></array>"
# Without it every TestFlight build stops at an export-compliance question.
check "declares no non-exempt encryption" \
  test "$(value_of ITSAppUsesNonExemptEncryption)" = "<false/>"

icon_name=$(jq -r '.images[] | select(.idiom == "universal" and .size == "1024x1024") | .filename // empty' \
  "$ICONSET/Contents.json")
icon=$ICONSET/$icon_name
check "names a 1024x1024 universal icon" test -n "$icon_name"
check "icon file exists" test -f "$icon"
if [ -n "$icon_name" ] && [ -f "$icon" ] && is_png "$icon"; then
  pass "icon is a PNG"
  check "icon is 1024 wide" test "$(be_uint 16 4 "$icon")" = 1024
  check "icon is 1024 tall" test "$(be_uint 20 4 "$icon")" = 1024
  # Colour type 2 is RGB. App Store Connect rejects an icon with alpha.
  check "icon has no alpha channel" test "$(be_uint 25 1 "$icon")" = 2
  check "icon has no tRNS transparency" bash -c '! grep -qx tRNS <<<"$1"' _ "$(chunks_before_idat "$icon")"
else
  fail "icon is a PNG"
fi

# v1 ships on iPhone only: an iPad build needs iPad screenshots, iPad review
# and a layout nobody has checked. The bundle's device families come from
# BUILD's `families`; iPad orientations in the plist would mean iPad again.
check "the app targets iPhone only" \
  bash -c 'tr -d " \n" <"$1" | grep -q "families=\[\"iphone\",\]"' _ "$PKG/BUILD.bazel"
check "the plist names no iPad orientations" bash -c '! grep -q "~ipad" "$1"' _ "$PLIST"

# Apple rejects an upload (ITMS-91053) whose code calls a required-reason
# API its privacy manifest doesn't declare. UserDefaults holds a resume
# token from 0.1.2 or earlier until it moves to the Keychain; CA92.1 is
# "read and written by this app only".
MANIFEST=$PKG/Sources/Resources/PrivacyInfo.xcprivacy
# -R: runfiles are symlinks, which -r skips.
if grep -Rqw UserDefaults "$PKG/Sources"; then
  check "a privacy manifest ships in the bundle's resources" test -f "$MANIFEST"
  check "the manifest declares UserDefaults, for this app's own use (CA92.1)" \
    bash -c 'tr -d " \t\n" <"$1" | grep -q "<string>NSPrivacyAccessedAPICategoryUserDefaults</string><key>NSPrivacyAccessedAPITypeReasons</key><array><string>CA92.1</string></array>"' _ "$MANIFEST"
fi

if [ "$FAILURES" -ne 0 ]; then
  echo "$FAILURES failure(s)"
  exit 1
fi
