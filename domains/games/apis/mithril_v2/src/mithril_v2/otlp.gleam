//// The http_server_* family as an OTLP/HTTP JSON ExportMetricsServiceRequest,
//// pushed to the collector on an interval: 64-bit integers as strings,
//// cumulative temporality, one data point per label set.

import gleam/erlang/process
import gleam/int
import gleam/io
import gleam/json.{type Json}
import gleam/list
import gleam/result
import gleam/string
import mithril_v2/http_metrics.{type Metrics, type RouteStats, type Snapshot}

const cumulative = 2

/// The SDK's default export interval.
pub const default_interval_ms = 60_000

pub fn encode(
  snapshot: Snapshot,
  resource: List(#(String, String)),
  start_nanos: Int,
  now_nanos: Int,
) -> String {
  let times = [
    #("startTimeUnixNano", json.string(int.to_string(start_nanos))),
    #("timeUnixNano", json.string(int.to_string(now_nanos))),
  ]
  let service = #("service_name", snapshot.service_name)
  let route_point = fn(route: http_metrics.RouteKey, fields) {
    json.object(
      list.flatten([
        times,
        fields,
        [
          attributes([
            #("http_method", route.method),
            #("route", route.route),
            service,
          ]),
        ],
      ]),
    )
  }
  let route_counter = fn(name, value: fn(RouteStats) -> Int) {
    sum(
      name,
      True,
      list.map(snapshot.routes, fn(entry) {
        route_point(entry.0, [#("asInt", int_string(value(entry.1)))])
      }),
    )
  }

  let metrics = [
    route_counter("http_server_requests", fn(stats) { stats.requests }),
    route_counter("http_server_requests_success", fn(stats) { stats.success }),
    route_counter("http_server_requests_failure", fn(stats) { stats.failure }),
    sum(
      "http_server_requests_active_gauge",
      False,
      list.map(snapshot.active, fn(entry) {
        json.object(
          list.append(times, [
            #("asInt", int_string(entry.1)),
            attributes([#("http_method", entry.0), service]),
          ]),
        )
      }),
    ),
    metric(
      "http_server_request_duration_microseconds",
      "histogram",
      [],
      list.map(snapshot.routes, fn(entry) {
        let stats = entry.1
        route_point(entry.0, [
          #("count", int_string(stats.duration_count)),
          #("sum", json.float(stats.duration_sum)),
          #("bucketCounts", json.array(stats.buckets, int_string)),
          #(
            "explicitBounds",
            json.array(http_metrics.latency_bucket_bounds_micros, json.float),
          ),
        ])
      }),
    ),
    sum(
      "http_server_requests_by_caller",
      True,
      list.map(snapshot.callers, fn(entry) {
        json.object(
          list.append(times, [
            #("asInt", int_string(entry.1)),
            attributes([#("caller", entry.0), service]),
          ]),
        )
      }),
    ),
  ]

  json.object([
    #(
      "resourceMetrics",
      json.preprocessed_array([
        json.object([
          #("resource", json.object([attributes(resource)])),
          #(
            "scopeMetrics",
            json.preprocessed_array([
              json.object([
                #("scope", json.object([#("name", json.string("http_server"))])),
                #("metrics", json.preprocessed_array(result.values(metrics))),
              ]),
            ]),
          ),
        ]),
      ]),
    ),
  ])
  |> json.to_string
}

fn sum(name: String, monotonic: Bool, points: List(Json)) -> Result(Json, Nil) {
  metric(name, "sum", [#("isMonotonic", json.bool(monotonic))], points)
}

// An instrument with no points is left out of the export.
fn metric(
  name: String,
  kind: String,
  fields: List(#(String, Json)),
  points: List(Json),
) -> Result(Json, Nil) {
  case points {
    [] -> Error(Nil)
    _ -> {
      let assert Ok(description) =
        list.key_find(http_metrics.instrument_descriptions, name)
      Ok(
        json.object([
          #("name", json.string(name)),
          #("description", json.string(description)),
          #(
            kind,
            json.object(
              list.flatten([
                [#("aggregationTemporality", json.int(cumulative))],
                fields,
                [#("dataPoints", json.preprocessed_array(points))],
              ]),
            ),
          ),
        ]),
      )
    }
  }
}

fn attributes(pairs: List(#(String, String))) -> #(String, Json) {
  #(
    "attributes",
    json.array(pairs, fn(pair) {
      json.object([
        #("key", json.string(pair.0)),
        #("value", json.object([#("stringValue", json.string(pair.1))])),
      ])
    }),
  )
}

fn int_string(value: Int) -> Json {
  json.string(int.to_string(value))
}

/// The resource as OTEL_SERVICE_NAME and OTEL_RESOURCE_ATTRIBUTES describe
/// it; the service name variable wins over a service.name attribute.
pub fn resource_attributes(
  service_name: Result(String, Nil),
  attributes: Result(String, Nil),
) -> List(#(String, String)) {
  let pairs =
    attributes
    |> result.unwrap("")
    |> string.split(",")
    |> list.filter_map(fn(pair) {
      use #(key, value) <- result.try(string.split_once(pair, "="))
      case string.trim(key) {
        "" -> Error(Nil)
        key -> Ok(#(key, string.trim(value)))
      }
    })
  case service_name {
    Ok(name) -> [
      #("service.name", name),
      ..list.filter(pairs, fn(pair) { pair.0 != "service.name" })
    ]
    Error(Nil) -> pairs
  }
}

/// POSTs an export to `<endpoint>/v1/metrics`; anything but a 2xx is an error.
pub fn post(endpoint: String, body: String) -> Result(Nil, String) {
  case post_json(endpoint <> "/v1/metrics", body) {
    Ok(status) if status >= 200 && status < 300 -> Ok(Nil)
    Ok(status) -> Error("collector answered " <> int.to_string(status))
    Error(reason) -> Error(reason)
  }
}

@external(erlang, "mithril_v2_ffi", "post_json")
fn post_json(url: String, body: String) -> Result(Int, String)

@external(erlang, "mithril_v2_ffi", "system_time_nanos")
pub fn system_time_nanos() -> Int

/// Exports a snapshot every `interval_ms`, from a process of its own, for as
/// long as the service runs. A failed export is logged and the next one
/// carries the same cumulative totals.
pub fn start_exporter(
  metrics: Metrics,
  endpoint: String,
  resource: List(#(String, String)),
  interval_ms: Int,
) -> Nil {
  let started = system_time_nanos()
  process.spawn(fn() {
    export_loop(metrics, endpoint, resource, interval_ms, started)
  })
  Nil
}

fn export_loop(metrics, endpoint, resource, interval_ms, started) -> Nil {
  process.sleep(interval_ms)
  let body =
    encode(
      http_metrics.snapshot(metrics),
      resource,
      started,
      system_time_nanos(),
    )
  case post(endpoint, body) {
    Ok(Nil) -> Nil
    Error(reason) -> io.println_error("OTLP export failed: " <> reason)
  }
  export_loop(metrics, endpoint, resource, interval_ms, started)
}
