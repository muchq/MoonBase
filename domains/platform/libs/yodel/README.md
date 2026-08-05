# yodel

The Java rails for MoonBase observability (#1212): how a Java service emits
the shared `http_server_*` serving metrics so the collector, prom_proxy, and
the dashboard read it exactly like the C++ (`aura`/`futility`) and Rust
(`server_pal`) services.

## The contract

Instrument names, labels, and units are pinned to the family the other
languages emit — parity is the whole point:

| OTLP instrument | Kind | Prometheus name |
|---|---|---|
| `http_server_requests` | monotonic sum | `http_server_requests_total` |
| `http_server_requests_success` | monotonic sum | `http_server_requests_success_total` |
| `http_server_requests_failure` | monotonic sum | `http_server_requests_failure_total` |
| `http_server_requests_active_gauge` | non-monotonic sum | `http_server_requests_active_gauge` |
| `http_server_request_duration_microseconds` | histogram (values in **microseconds**) | `..._{sum,count,bucket}` |

Every data point carries `service_name` (from `OTEL_SERVICE_NAME`) and
`http_method`; the counters and the histogram also carry `route` (#1303) —
a bounded matched-handler value, never the raw path, with a fixed
`unmatched` sentinel — so probe traffic is separable from serving traffic.
The gauge is the one instrument without a route: it moves at request start,
before routing has matched anything, which is also why the counters move at
completion rather than start. Success means status < 400.

All three rails speak this dialect now (#1304, #1305), pinned by the
otel_contract suite alongside descriptions, units, and bucket layouts. The
`route` *value* vocabulary differs per rail because each router exposes a
different bounded identity: yodel and server_pal emit the matched route
template ("/games/{id}"), while futility emits the matched Smithy operation
name ("Trace") — smithy-cpp annotates responses with the operation, not the
URI pattern. All three agree on the literal `/health` for the probe
endpoint (which prom_proxy's probeFilter subtracts) and on the `unmatched`
sentinel. futility additionally labels its histogram with
`status_code`/`result` and its failure counter with those plus
`error_type`; the shared core is identical everywhere.

Rather than pulling the OpenTelemetry SDK + Micrometer dependency trees into
`maven_install.json` for five fixed instruments (and then remapping
Micrometer's `http.server.requests` names/units back to this family), the
library speaks the small OTLP/HTTP JSON surface directly with deps the repo
already has (Jackson, JDK HttpClient).

## Joining a Micronaut service

1. Add `"//domains/platform/libs/yodel:micronaut"` to the binary's
   `runtime_deps`. The `HttpServerMetricsFilter` registers itself for all
   routes in the METRICS filter phase — outside auth and routing, so rejected
   and unmatched requests are measured too.
2. Set the standard env block in `deploy/consolidated/compose.yaml`
   (same as every other service):

   ```yaml
   OTEL_EXPORTER_OTLP_ENDPOINT: http://otelcol:4318
   OTEL_SERVICE_NAME: my_service
   OTEL_RESOURCE_ATTRIBUTES: service.name=my_service,service.version=${MY_SERVICE_SHA:-latest}
   ```

3. Add the service to `serviceOrder` / `serviceRegistry` in
   `domains/platform/apis/prom_proxy/registry.go` so it gets a catalog entry
   and service page.

Without `OTEL_EXPORTER_OTLP_ENDPOINT` the instruments record in memory and
nothing exports — the same no-op contract as `server_pal::init_otel`, so
tests and local runs need no configuration.

## Non-Micronaut use

`HttpMetricsPipeline.fromEnv()` + `recordRequestStart` /
`recordRequestComplete(method, route, status, micros)` are
framework-agnostic; the Micronaut filter is just the first transport
binding, and a non-Micronaut caller supplies its own matched-template
route (or the filter's `unmatched` sentinel). Future cross-cutting
families (`rate_limit_*`, `cache_*` — #1209) should ride the same
exporter.
