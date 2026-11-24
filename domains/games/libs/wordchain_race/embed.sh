#!/bin/bash
# Writes a .cc defining each NAME=FILE pair as a std::string_view holding
# the file's bytes, for lexicon_data.h. A raw string literal holds any text
# but its delimiter, which the files are checked not to contain.
set -euo pipefail
out=$1
shift
{
  echo '#include "domains/games/libs/wordchain_race/lexicon_data.h"'
  echo 'namespace wordchain_race {'
  for pair in "$@"; do
    name=${pair%%=*}
    file=${pair#*=}
    if grep -q ')WORDS"' "$file"; then
      echo "embed.sh: $file holds the raw string delimiter" >&2
      exit 1
    fi
    printf 'extern const std::string_view %s;\n' "$name"
    printf 'const std::string_view %s = R"WORDS(' "$name"
    cat "$file"
    printf ')WORDS";\n'
  done
  echo '}  // namespace wordchain_race'
} > "$out"
