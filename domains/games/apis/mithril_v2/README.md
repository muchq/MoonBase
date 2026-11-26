# mithril_v2

mithril's word-ladder solver in [Gleam](https://gleam.run), built with
[rules_gleam](https://github.com/muchq/rules_gleam). It runs in the
consolidated deploy beside the Rust [mithril](../mithril); Caddy routes
`api.muchq.com/mithril/v2/wordchain` to it.

```
POST /mithril/v2/wordchain
{"start": "cold", "end": "warm"}
→ 200 {"path": ["cold", "cord", "card", "ward", "warm"]}
```

Both Mithril APIs, the CLI, and iOS read the same generated `word_graph.json`.
The graph uses the filtered ENABLE dictionary that lobby races use. The
generator orders neighbours by word index so equally short ladders resolve
consistently. `testdata/v1_ladders.tsv` holds 500 answers from the Rust solver
on that graph, and the tests replay them.
[Word Chains dictionary](../../libs/wordchain_race/DICTIONARY.md)

Its status codes match v1's too (406, 413, 415, 400, 422),
with two exceptions: v1 also reads a body sent as an array of the two words,
and v1 refuses a repeated key, where v2 takes the first one.

## Metrics and rate limiting

It serves through [mist_pal](../../../platform/libs/mist_pal), which reports
the `http_server_*` family every service reports and limits each client to
server_pal's default of 100 requests a second, with bursts of 200. The client
is derived behind Caddy from `TRUSTED_PROXY_CIDRS`, the way mithril v1 derives
it.

## Build

```bash
bazel test //domains/games/apis/mithril_v2/...
bazel run //domains/games/apis/mithril_v2:mithril_v2_bin   # PORT, default 8080
```

The release bundles the Erlang/OTP it was built with, built for the build host.
An image built anywhere except linux/amd64 (CI's publish job) will not run on
the deploy host.

Hex dependencies come from `manifest.toml`, which `gleam deps download`
writes. Commit it as written; rules_gleam imports its Hex entries and skips
the `mist_pal` path entry, which Bazel builds from its own target.
