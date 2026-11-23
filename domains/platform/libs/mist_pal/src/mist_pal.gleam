//// An HTTP service on mist, wired the way server_pal wires a Rust one: the
//// http_server_* instruments around every request, exported over OTLP when
//// the environment names a collector, and a per-client rate limit keyed on
//// the client ADR-0012 derives from TRUSTED_PROXY_CIDRS.
////
//// ```gleam
//// mist_pal.new(handle)
//// |> mist_pal.route_label(route_of)
//// |> mist_pal.serve
//// ```

import exception
import gleam/bytes_tree
import gleam/erlang/process
import gleam/http/request.{type Request}
import gleam/http/response.{type Response}
import gleam/int
import gleam/json
import gleam/list
import gleam/option.{type Option, None, Some}
import gleam/result
import gleam/string
import mist.{type Connection, type ResponseData}
import mist_pal/env
import mist_pal/forwarded.{type TrustedProxies}
import mist_pal/http_metrics.{type Metrics}
import mist_pal/log
import mist_pal/otlp
import mist_pal/rate_limit.{type Limiter, type RateLimit}

/// The probe route. It is never rate limited, so no tuning of the limit can
/// take a container's liveness signal down with it.
pub const health_route = "/health"

/// The route label for a path no route matches, so scanners cannot mint a
/// series per path. The spelling is shared with every other rail.
pub const unmatched_route = "unmatched"

/// A request body as the server read it.
pub type Body {
  Body(BitArray)
  /// Longer than the builder's `max_body_bytes`.
  TooLarge
  /// The connection failed or the body was malformed.
  Unreadable
}

pub opaque type Builder {
  Builder(
    handler: fn(Request(Body)) -> Response(String),
    port: Option(Int),
    max_body_bytes: Int,
    route_label: fn(String) -> String,
    rate_limit: Option(RateLimit),
    trusted_proxies: Option(TrustedProxies),
  )
}

/// A running server: the port it bound and the metrics it records.
pub type Server {
  Server(port: Int, metrics: Metrics)
}

/// A server for `handler`, which answers every request but those the rate
/// limit refuses. Unless the builder says otherwise it listens on $PORT
/// (8080 when unset), reads bodies up to 4 MiB, labels only /health, limits
/// each client to `rate_limit.default`, and trusts $TRUSTED_PROXY_CIDRS.
pub fn new(handler: fn(Request(Body)) -> Response(String)) -> Builder {
  Builder(
    handler:,
    port: None,
    max_body_bytes: 4_194_304,
    route_label: fn(path) {
      case path {
        "/health" -> health_route
        _ -> unmatched_route
      }
    },
    rate_limit: Some(rate_limit.default),
    trusted_proxies: None,
  )
}

pub fn port(builder: Builder, port: Int) -> Builder {
  Builder(..builder, port: Some(port))
}

pub fn max_body_bytes(builder: Builder, bytes: Int) -> Builder {
  Builder(..builder, max_body_bytes: bytes)
}

/// The route label for a request path: its route template, or
/// `unmatched_route`. Labels are metric series, so a label must never carry
/// a value a client chose.
pub fn route_label(builder: Builder, label: fn(String) -> String) -> Builder {
  Builder(..builder, route_label: label)
}

/// None serves without a limit.
pub fn rate_limit(builder: Builder, limit: Option(RateLimit)) -> Builder {
  Builder(..builder, rate_limit: limit)
}

pub fn trusted_proxies(builder: Builder, trusted: TrustedProxies) -> Builder {
  Builder(..builder, trusted_proxies: Some(trusted))
}

/// Starts the server, or says why it cannot: a malformed
/// TRUSTED_PROXY_CIDRS or rate limit, or a port it cannot bind.
pub fn start(builder: Builder) -> Result(Server, String) {
  use trusted <- result.try(case builder.trusted_proxies {
    Some(trusted) -> Ok(trusted)
    None -> forwarded.from_env()
  })
  use limiter <- result.try(case builder.rate_limit {
    Some(limit) -> rate_limit.start(limit) |> result.map(Some)
    None -> Ok(None)
  })
  let service_name = env.getenv("OTEL_SERVICE_NAME") |> result.unwrap("")
  use metrics <- result.try(
    http_metrics.start(service_name)
    |> result.replace_error("the metrics actor did not start"),
  )
  otlp.start_from_env(metrics)

  let port = case builder.port {
    Some(port) -> port
    None -> env.getenv("PORT") |> result.try(int.parse) |> result.unwrap(8080)
  }
  start_mist_clock()
  let bound = process.new_subject()
  use _ <- result.try(
    fn(req) { respond(builder, req, service_name, metrics, limiter, trusted) }
    |> mist.new
    |> mist.bind("0.0.0.0")
    |> mist.port(port)
    |> mist.after_start(fn(port, _, _) {
      log.info("listening on 0.0.0.0:" <> int.to_string(port), [])
      process.send(bound, port)
    })
    |> mist.start
    |> result.replace_error("cannot listen on port " <> int.to_string(port)),
  )
  use port <- result.map(
    process.receive(bound, 5000)
    |> result.replace_error("the server never reported its port"),
  )
  Server(port:, metrics:)
}

/// Starts the server and serves until the VM stops; a server that cannot
/// start stops the VM.
pub fn serve(builder: Builder) -> Nil {
  case start(builder) {
    Ok(_) -> process.sleep_forever()
    Error(reason) -> {
      log.error("cannot serve: " <> reason, [])
      panic as reason
    }
  }
}

// The instruments sit outside the limiter, so a 429 is counted as the
// failure it is. A handler that crashes answers 500, counted the same way,
// so the in-flight gauge cannot drift, and the reason goes to the log.
fn respond(
  builder: Builder,
  req: Request(Connection),
  service_name: String,
  metrics: Metrics,
  limiter: Option(Limiter(forwarded.Address)),
  trusted: TrustedProxies,
) -> Response(ResponseData) {
  let started = env.monotonic_micros()
  let method = http_metrics.method_label(req.method)
  http_metrics.request_started(
    metrics,
    method,
    http_metrics.caller_of(req.headers),
  )
  let peer =
    mist.get_connection_info(req.body)
    |> result.map(fn(info) { forwarded.from_mist(info.ip_address) })
    |> option.from_result
  let client = forwarded.derive_client(peer, req.headers, trusted)
  let outcome =
    exception.rescue(fn() {
      case limiter, req.path == health_route {
        Some(limiter), False ->
          case admit(client, limiter) {
            Ok(Nil) -> handle(builder, req)
            Error(refusal) -> refusal
          }
        _, _ -> handle(builder, req)
      }
    })
  let route =
    exception.rescue(fn() { builder.route_label(req.path) })
    |> result.unwrap(unmatched_route)
  let resp = case outcome {
    Ok(resp) -> resp
    Error(crash) -> {
      log.error("request crashed", [
        #("route", json.string(route)),
        #("target", json.string(target_of(req))),
        #("reason", json.string(string.inspect(crash))),
      ])
      text(500, "Internal Server Error")
    }
  }
  let micros = int.max(env.monotonic_micros() - started, 0)
  http_metrics.request_finished(
    metrics,
    method,
    route,
    resp.status,
    int.to_float(micros),
  )
  // Probes are metered but not logged.
  case route == health_route {
    True -> Nil
    False ->
      log.info(
        "request",
        access_fields(
          service_name,
          method,
          route,
          req,
          resp.status,
          micros,
          client,
        ),
      )
  }
  response.map(resp, fn(body) { mist.Bytes(bytes_tree.from_string(body)) })
}

// server_pal's access line (#1459): http_method and route are the bounded
// labels the metrics carry, so a pivot from a dashboard to the logs is a
// copy-paste, and the raw path and query ride in target. client is the one
// the limiter keys on, never the raw x-forwarded-for.
fn access_fields(
  service_name: String,
  method: String,
  route: String,
  req: Request(a),
  status: Int,
  micros: Int,
  client: forwarded.DerivedClient,
) -> List(#(String, json.Json)) {
  [
    // "event", not "log": docker's json-file driver wraps each line in an
    // object whose payload key is "log".
    #("event", json.string("access")),
    #("service_name", json.string(service_name)),
    #("http_method", json.string(method)),
    #("route", json.string(route)),
    #("target", json.string(target_of(req))),
    #("status", json.int(status)),
    #("duration_us", json.int(micros)),
    #("trace_id", json.string(trace_id_of(req))),
    #(
      "client",
      json.string(case client.address {
        Some(address) -> forwarded.address_to_string(address)
        None -> ""
      }),
    ),
    #("client_source", json.string(forwarded.source_label(client.source))),
  ]
}

fn target_of(req: Request(a)) -> String {
  case req.query {
    Some(query) -> req.path <> "?" <> query
    None -> req.path
  }
}

// The W3C trace id off a traceparent header, or "" when it is absent or
// malformed.
fn trace_id_of(req: Request(a)) -> String {
  case request.get_header(req, "traceparent") {
    Error(Nil) -> ""
    Ok(traceparent) ->
      case string.split(traceparent, "-") {
        [_, trace_id, ..] ->
          case string.length(trace_id) == 32 && is_hex(trace_id) {
            True -> trace_id
            False -> ""
          }
        _ -> ""
      }
  }
}

fn is_hex(text: String) -> Bool {
  string.to_utf_codepoints(text)
  |> list.all(fn(c) {
    let code = string.utf_codepoint_to_int(c)
    { code >= 48 && code <= 57 }
    || { code >= 65 && code <= 70 }
    || { code >= 97 && code <= 102 }
  })
}

// Ahead of the body read and the handler, so every request costs quota,
// one the handler would refuse included.
fn admit(
  client: forwarded.DerivedClient,
  limiter: Limiter(forwarded.Address),
) -> Result(Nil, Response(String)) {
  case client.address {
    // A request with no peer cannot be keyed, and is refused, as
    // tower_governor refuses it.
    None -> Error(text(500, "Unable To Extract Key!"))
    Some(client) ->
      rate_limit.check(limiter, client)
      |> result.map_error(fn(wait) {
        let seconds = int.to_string(rate_limit.retry_after_seconds(wait))
        text(429, "Too Many Requests! Wait for " <> seconds <> "s")
        |> response.set_header("retry-after", seconds)
      })
  }
}

fn handle(builder: Builder, req: Request(Connection)) -> Response(String) {
  let body = case mist.read_body(req, builder.max_body_bytes) {
    Ok(req) -> Body(req.body)
    Error(mist.ExcessBody) -> TooLarge
    Error(mist.MalformedBody) -> Unreadable
  }
  builder.handler(request.set_body(req, body))
}

@external(erlang, "mist_pal_ffi", "start_mist_clock")
fn start_mist_clock() -> Nil

fn text(status: Int, body: String) -> Response(String) {
  response.new(status)
  |> response.set_header("content-type", "text/plain; charset=utf-8")
  |> response.set_body(body)
}
