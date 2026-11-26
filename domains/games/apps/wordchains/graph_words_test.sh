#!/bin/bash
# Reads the generated word_graph.json the iOS app bundles and the CLI embeds.
set -uo pipefail

GRAPH=$1
RACE_WORDS=$2

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

nodes=$(mktemp)
trap 'rm -f "$nodes"' EXIT
jq -r '.nodes[]' "$GRAPH" | LC_ALL=C sort >"$nodes"
has() { grep -qx "$1" "$nodes"; }
lacks() { ! grep -qx "$1" "$nodes"; }

echo "wordchains graph words"

# Not words: what a 3-letter puzzle once ran through (KEX -> NAA by KEA and
# NEA), and abbreviations the scraped list carried.
for w in naa nea rps dpt abp afb bcd abc obv; do
  check "lacks $w" lacks "$w"
done
# Obscure and archaic headwords stay alongside words in current use.
for w in bee cat kea kex shall thee doth hath ere oft strew strews dandruff verandah; do
  check "has $w" has "$w"
done
# Specialized words with modern dictionary entries and independently defined archaisms.
for w in benzole benzoles eftsoons lentando oscule oscules pistache; do
  check "has $w" has "$w"
done
for w in sall strow strown strows dandriff odyls swinneys woalds yonkers; do
  check "lacks $w" lacks "$w"
done
# Every 3-5 letter word is a word the race accepts, and every word the race
# accepts is here: one dictionary for every mode.
check "its 3-5 letter words are exactly the race's" \
  diff -q <(awk 'length >= 3 && length <= 5' "$nodes") <(LC_ALL=C sort "$RACE_WORDS")

if [ "$FAILURES" -ne 0 ]; then
  echo "$FAILURES failure(s)"
  exit 1
fi
