#!/bin/bash
set -euo pipefail
export LC_ALL=C

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for list in "$1" "$2"; do
  sort -cu "$list"
  if grep -nEv '^[a-z]{3,8}$' "$list"; then
    echo "$list must contain lowercase words of 3 to 8 letters." >&2
    exit 1
  fi
done
sort -m "$1" "$2" > "$work/excluded"
sort -cu "$work/excluded"
tr -d '\r' < "$3" | sort -u > "$work/enable"
sort -u "$4" > "$work/common"

missing=$(comm -23 "$work/excluded" "$work/enable")
if [ -n "$missing" ]; then
  printf 'Exclusions missing from ENABLE\n%s\n' "$missing" >&2
  exit 1
fi
overlap=$(comm -12 "$work/excluded" "$work/common")
if [ -n "$overlap" ]; then
  printf 'Excluded puzzle endpoints\n%s\n' "$overlap" >&2
  exit 1
fi
