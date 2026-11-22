//// mithril v2: the word-ladder solver behind /mithril/v2/wordchain.

import gleam/bytes_tree
import gleam/erlang/process
import gleam/http/request.{type Request}
import gleam/http/response.{type Response}
import gleam/int
import gleam/io
import gleam/result
import mist.{type Connection, type ResponseData}
import mithril_v2/http_metrics
import mithril_v2/otlp
import mithril_v2/runtime
import mithril_v2/service
import mithril_v2/wordchain.{type Graph}

pub fn main() -> Nil {
  let port =
    runtime.getenv("PORT") |> result.try(int.parse) |> result.unwrap(8080)
  let graph = case wordchain.load(wordchain.data_dir()) {
    Ok(graph) -> graph
    Error(reason) -> panic as { "cannot load the word graph: " <> reason }
  }
  // Read by every connection's process without a copy.
  store_graph(graph)

  let service_name = runtime.getenv("OTEL_SERVICE_NAME") |> result.unwrap("")
  let assert Ok(metrics) = http_metrics.start(service_name)
  case runtime.getenv("OTEL_EXPORTER_OTLP_ENDPOINT") {
    Ok(endpoint) -> {
      let interval =
        runtime.getenv("OTEL_METRIC_EXPORT_INTERVAL")
        |> result.try(int.parse)
        |> result.unwrap(otlp.default_interval_ms)
      let resource =
        otlp.resource_attributes(
          runtime.getenv("OTEL_SERVICE_NAME"),
          runtime.getenv("OTEL_RESOURCE_ATTRIBUTES"),
        )
      otlp.start_exporter(metrics, endpoint, resource, interval)
      io.println("exporting metrics to " <> endpoint)
    }
    Error(Nil) ->
      io.println("OTEL_EXPORTER_OTLP_ENDPOINT unset; metrics are not exported")
  }

  let assert Ok(_) =
    fn(req) { respond(req, metrics) }
    |> mist.new
    |> mist.bind("0.0.0.0")
    |> mist.port(port)
    |> mist.after_start(fn(port, _, _) {
      io.println("listening on 0.0.0.0:" <> int.to_string(port))
    })
    |> mist.start
  process.sleep_forever()
}

fn respond(
  req: Request(Connection),
  metrics: http_metrics.Metrics,
) -> Response(ResponseData) {
  let body = case mist.read_body(req, service.max_body_bytes) {
    Ok(req) -> service.Body(req.body)
    Error(mist.ExcessBody) -> service.TooLarge
    Error(mist.MalformedBody) -> service.Unreadable
  }
  let resp =
    service.instrumented(metrics, request.set_body(req, body), fn(req) {
      service.handle(req, stored_graph())
    })
  response.map(resp, fn(body) { mist.Bytes(bytes_tree.from_string(body)) })
}

@external(erlang, "mithril_v2_ffi", "store_graph")
fn store_graph(graph: Graph) -> Nil

@external(erlang, "mithril_v2_ffi", "stored_graph")
fn stored_graph() -> Graph
