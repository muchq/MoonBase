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
is_png() { [ "$(od -An -tx1 -N 8 "$1" | tr -d ' \n')" = 89504e470d0a1a0a ]; }

echo "wordchains_ios app store"

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
else
  fail "icon is a PNG"
fi

if [ "$FAILURES" -ne 0 ]; then
  echo "$FAILURES failure(s)"
  exit 1
fi
