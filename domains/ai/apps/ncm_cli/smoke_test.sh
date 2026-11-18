#!/usr/bin/env bash
set -euo pipefail
app="$TEST_SRCDIR/_main/domains/ai/apps/ncm_cli"
bin="$app/ncm"
"$bin" train-tokenizer --input "$app/testdata/tiny.jsonl" --config "$app/testdata/tiny-config.json" --output "$TEST_TMPDIR/vq" --steps 2
"$bin" train-generator --input "$app/testdata/tiny.jsonl" --model "$TEST_TMPDIR/vq" --output "$TEST_TMPDIR/model" --steps 2
"$bin" evaluate --input "$app/testdata/tiny.jsonl" --model "$TEST_TMPDIR/model"
"$bin" generate --model "$TEST_TMPDIR/model" --language java --prompt 'Define an empty class.'
if "$bin" generate --model "$TEST_TMPDIR/vq" --language java --prompt test; then
    echo 'incomplete checkpoint accepted' >&2
    exit 1
fi
mkdir "$TEST_TMPDIR/source"
cp "$app/testdata/tiny-config.json" "$TEST_TMPDIR/source/config.json"
"$bin" prepare --root "$TEST_TMPDIR/source" --language json --extensions json --output "$TEST_TMPDIR/prepared.jsonl"
test -s "$TEST_TMPDIR/prepared.jsonl"
