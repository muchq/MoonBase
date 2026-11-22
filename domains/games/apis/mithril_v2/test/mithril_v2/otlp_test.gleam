import gleam/bit_array
import gleam/bytes_tree
import gleam/erlang/process
import gleam/http/request
import gleam/http/response
import gleam/int
import gleam/string
import gleeunit/should
import mist
import mithril_v2/http_metrics.{RouteKey, RouteStats, Snapshot}
import mithril_v2/otlp

fn snapshot() -> http_metrics.Snapshot {
  Snapshot(
    service_name: "mithril_v2",
    routes: [
      #(
        RouteKey("POST", "/mithril/v2/wordchain"),
        RouteStats(
          requests: 3,
          success: 2,
          failure: 1,
          duration_count: 3,
          duration_sum: 1250.5,
          buckets: [0, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
        ),
      ),
    ],
    active: [#("POST", 1)],
    callers: [#("edge", 2), #("games_hub", 1)],
  )
}

const point_times = "\"startTimeUnixNano\":\"1000\",\"timeUnixNano\":\"2000\""

const route_attributes = "\"attributes\":[{\"key\":\"http_method\",\"value\":{\"stringValue\":\"POST\"}},{\"key\":\"route\",\"value\":{\"stringValue\":\"/mithril/v2/wordchain\"}},{\"key\":\"service_name\",\"value\":{\"stringValue\":\"mithril_v2\"}}]"

fn counter(name: String, description: String, value: Int) -> String {
  "{\"name\":\""
  <> name
  <> "\",\"description\":\""
  <> description
  <> "\",\"sum\":{\"aggregationTemporality\":2,\"isMonotonic\":true,\"dataPoints\":[{"
  <> point_times
  <> ",\"asInt\":\""
  <> int.to_string(value)
  <> "\","
  <> route_attributes
  <> "}]}}"
}

fn caller_point(caller: String, value: Int) -> String {
  "{"
  <> point_times
  <> ",\"asInt\":\""
  <> int.to_string(value)
  <> "\",\"attributes\":[{\"key\":\"caller\",\"value\":{\"stringValue\":\""
  <> caller
  <> "\"}},{\"key\":\"service_name\",\"value\":{\"stringValue\":\"mithril_v2\"}}]}"
}

// The ExportMetricsServiceRequest in OTLP/HTTP's JSON mapping: 64-bit
// integers as strings, cumulative temporality (2), names and descriptions
// shared with every other rail. The collector's Prometheus exporter turns
// these into http_server_requests_total and friends.
pub fn a_snapshot_encodes_as_an_otlp_export_request_test() {
  let want =
    "{\"resourceMetrics\":[{\"resource\":{\"attributes\":["
    <> "{\"key\":\"service.name\",\"value\":{\"stringValue\":\"mithril_v2\"}},"
    <> "{\"key\":\"service.version\",\"value\":{\"stringValue\":\"abc123\"}}"
    <> "]},\"scopeMetrics\":[{\"scope\":{\"name\":\"http_server\"},\"metrics\":["
    <> counter("http_server_requests", "HTTP requests received", 3)
    <> ","
    <> counter(
      "http_server_requests_success",
      "HTTP requests completed successfully (2xx-3xx)",
      2,
    )
    <> ","
    <> counter(
      "http_server_requests_failure",
      "HTTP requests that returned 4xx or 5xx",
      1,
    )
    <> ",{\"name\":\"http_server_requests_active_gauge\",\"description\":\"HTTP requests currently in flight\",\"sum\":{\"aggregationTemporality\":2,\"isMonotonic\":false,\"dataPoints\":[{"
    <> point_times
    <> ",\"asInt\":\"1\",\"attributes\":[{\"key\":\"http_method\",\"value\":{\"stringValue\":\"POST\"}},{\"key\":\"service_name\",\"value\":{\"stringValue\":\"mithril_v2\"}}]}]}}"
    <> ",{\"name\":\"http_server_request_duration_microseconds\",\"description\":\"HTTP request duration in microseconds\",\"histogram\":{\"aggregationTemporality\":2,\"dataPoints\":[{"
    <> point_times
    <> ",\"count\":\"3\",\"sum\":1250.5,\"bucketCounts\":[\"0\",\"1\",\"2\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\",\"0\"]"
    // Erlang's shortest float form: exponents from 1000 up, valid JSON.
    <> ",\"explicitBounds\":[100.0,250.0,500.0,1.0e3,2.5e3,5.0e3,1.0e4,2.5e4,5.0e4,1.0e5,2.5e5,5.0e5,1.0e6,2.5e6,1.0e7],"
    <> route_attributes
    <> "}]}}"
    <> ",{\"name\":\"http_server_requests_by_caller\",\"description\":\"HTTP requests received, by who sent them\",\"sum\":{\"aggregationTemporality\":2,\"isMonotonic\":true,\"dataPoints\":["
    <> caller_point("edge", 2)
    <> ","
    <> caller_point("games_hub", 1)
    <> "]}}"
    <> "]}]}]}"

  otlp.encode(
    snapshot(),
    [#("service.name", "mithril_v2"), #("service.version", "abc123")],
    1000,
    2000,
  )
  |> should.equal(want)
}

// An instrument with nothing recorded is left out rather than sent empty.
pub fn instruments_without_points_are_left_out_test() {
  let encoded =
    otlp.encode(Snapshot(..snapshot(), routes: [], active: []), [], 1, 2)
  string.contains(encoded, "http_server_requests_by_caller") |> should.be_true
  string.contains(encoded, "\"http_server_requests\"") |> should.be_false
  string.contains(encoded, "active_gauge") |> should.be_false
  string.contains(encoded, "duration") |> should.be_false
}

pub fn resource_attributes_come_from_the_standard_variables_test() {
  otlp.resource_attributes(
    Ok("mithril_v2"),
    Ok("service.name=ignored,service.version=abc123, deployment=prod"),
  )
  |> should.equal([
    #("service.name", "mithril_v2"),
    #("service.version", "abc123"),
    #("deployment", "prod"),
  ])

  otlp.resource_attributes(Error(Nil), Ok("service.name=from_attrs"))
  |> should.equal([#("service.name", "from_attrs")])

  otlp.resource_attributes(Error(Nil), Error(Nil)) |> should.equal([])
}

// Through a real socket: the exporter POSTs JSON to <endpoint>/v1/metrics.
pub fn an_export_reaches_the_collector_test() {
  let received = process.new_subject()
  let ports = process.new_subject()
  let assert Ok(_) =
    fn(req: request.Request(mist.Connection)) {
      let assert Ok(req) = mist.read_body(req, 1_000_000)
      process.send(received, #(
        req.path,
        request.get_header(req, "content-type"),
        bit_array.to_string(req.body),
      ))
      response.new(200)
      |> response.set_body(mist.Bytes(bytes_tree.new()))
    }
    |> mist.new
    |> mist.bind("127.0.0.1")
    |> mist.port(0)
    |> mist.after_start(fn(port, _, _) { process.send(ports, port) })
    |> mist.start
  let assert Ok(port) = process.receive(ports, 5000)

  otlp.post(
    "http://127.0.0.1:" <> int.to_string(port),
    "{\"resourceMetrics\":[]}",
  )
  |> should.equal(Ok(Nil))
  process.receive(received, 5000)
  |> should.equal(
    Ok(#("/v1/metrics", Ok("application/json"), Ok("{\"resourceMetrics\":[]}"))),
  )
}

pub fn a_collector_that_refuses_the_export_is_an_error_test() {
  let ports = process.new_subject()
  let assert Ok(_) =
    fn(_req) {
      response.new(503) |> response.set_body(mist.Bytes(bytes_tree.new()))
    }
    |> mist.new
    |> mist.bind("127.0.0.1")
    |> mist.port(0)
    |> mist.after_start(fn(port, _, _) { process.send(ports, port) })
    |> mist.start
  let assert Ok(port) = process.receive(ports, 5000)

  otlp.post("http://127.0.0.1:" <> int.to_string(port), "{}")
  |> should.be_error
  // Nothing is listening on port 1.
  otlp.post("http://127.0.0.1:1", "{}") |> should.be_error
}
