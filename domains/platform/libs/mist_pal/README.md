# mist_pal

HTTP service plumbing for Gleam on [mist](https://hexdocs.pm/mist), the
Gleam counterpart of the Rust [server_pal](../server_pal).

- The `http_server_*` instruments every MoonBase service reports, with the
  same names, descriptions, labels, latency buckets and caller vocabulary.
  `//domains/platform/libs/otel_contract` pins them against the other rails.
- OTLP/HTTP JSON export to `$OTEL_EXPORTER_OTLP_ENDPOINT/v1/metrics` every
  `$OTEL_METRIC_EXPORT_INTERVAL` ms (default 60000). There is no Gleam
  OpenTelemetry SDK, so `otlp.gleam` encodes the export itself, the way
  yodel does for Java. With no endpoint set, nothing is exported.
- A per-client rate limit, by default server_pal's 100 requests a second
  with bursts of 200. It is GCRA, the algorithm under server_pal's
  tower_governor, and a refused request gets a 429 with `Retry-After`.
- The client it keys on is derived from the socket peer and
  `X-Forwarded-For` against `$TRUSTED_PROXY_CIDRS`, by opal-cpp ADR-0012's
  rightmost-untrusted walk; its tests are server_pal's `forwarded.rs` tests,
  case for case.
- `GET /health` is never rate limited.
- A handler that crashes answers 500, and is counted.
- A request's duration runs from its arrival to its response, so it covers
  the rate-limit check and the body read, as in server_pal.

## Usage

```gleam
import mist_pal
import mist_pal/rate_limit.{RateLimit}
import gleam/option.{Some}

pub fn main() {
  mist_pal.new(handle)
  |> mist_pal.route_label(route_of)
  |> mist_pal.rate_limit(Some(RateLimit(per_second: 10.0, burst: 20)))
  |> mist_pal.serve
}
```

The handler gets a `Request(Body)`, the body already read up to
`max_body_bytes` (4 MiB unless set), and answers a `Response(String)`.
`route_label` maps a path to its route template for the metrics; anything it
does not know must map to `mist_pal.unmatched_route`, so a scanner cannot
mint a series per path. The port is `$PORT`, or 8080.

In Bazel, depend on `//domains/platform/libs/mist_pal`. For the `gleam` CLI,
add it as a path dependency in `gleam.toml`.

## Not ported from server_pal

The JSON access log, response compression, the Accept check and the
request timeout. A service does its own Accept check; mithril_v2's is in
its handler.

## Hex packages

It keeps no manifest of its own. Its Hex dependencies come from the one manifest `bazel/gleam.MODULE.bazel`
imports, mithril_v2's. rules_gleam refuses a manifest entry that is not from
Hex, so after `gleam deps download` there, delete the `mist_pal` entry it
adds to `[packages]`.
