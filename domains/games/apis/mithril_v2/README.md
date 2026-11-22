# mithril_v2

mithril's word-ladder solver in [Gleam](https://gleam.run), built with
[rules_gleam](https://github.com/muchq/rules_gleam). It runs in the
consolidated deploy beside the Rust [mithril](../mithril), with no gateway
route and no caller yet.

```
POST /mithril/v2/wordchain
{"start": "cold", "end": "warm"}
→ 200 {"path": ["cold", "cord", "card", "ward", "warm"]}
```

It serves mithril's own `words.txt` and `.graph` and searches them the way the
Rust solver does, so it answers every query with the same ladder.
`testdata/v1_ladders.tsv` holds 500 answers captured from mithril v1, and the
tests replay them. Its status codes match v1's too (406, 413, 415, 400, 422),
with two exceptions: v1 also reads a body sent as an array of the two words,
and v1 refuses a repeated key, where v2 takes the first one.

## Metrics

It reports the `http_server_*` family that every service reports, with the
same names, descriptions, labels, latency buckets and caller vocabulary.
`//domains/platform/libs/otel_contract` pins these against the other languages.
There is no Gleam OpenTelemetry SDK, so `otlp.gleam` encodes OTLP/HTTP JSON
itself, the way yodel does for Java, and POSTs it to
`$OTEL_EXPORTER_OTLP_ENDPOINT/v1/metrics` every
`$OTEL_METRIC_EXPORT_INTERVAL` ms (default 60000). With no endpoint set,
nothing is exported.

## Build

```bash
bazel test //domains/games/apis/mithril_v2/...
bazel run //domains/games/apis/mithril_v2:mithril_v2_bin   # PORT, default 8080
```

The release bundles the Erlang/OTP it was built with, built for the build host.
An image built anywhere except linux/amd64 (CI's publish job) will not run on
the deploy host.

Hex dependencies come from `manifest.toml`, which `gleam deps download`
writes. rules_gleam v0.3.1 refuses a package whose OTP application name differs
from its Hex name, and mist depends on one, `hpack_erl`, whose application is
`hpack`. The manifest therefore records its `otp_app` as `hpack_erl`, as
rules_gleam's own web_service example does. Re-apply that edit whenever the
manifest is regenerated.
