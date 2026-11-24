#!/bin/bash
# The games_hub binary embeds SCOWL-derived word lists; SCOWL's license asks
# that its copyright notice appear in every copy. The linker drops what
# nothing references, so check the binary itself.
set -euo pipefail
binary=$1
grep -aq "Kevin Atkinson" "$binary" || {
  echo "SCOWL's notice is missing from $binary" >&2
  exit 1
}
grep -aq "zymes" "$binary" || {
  echo "the race word list is missing from $binary" >&2
  exit 1
}
