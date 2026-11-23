//// The http_server_* family every MoonBase HTTP service reports, recorded
//// the way server_pal records it: the in-flight gauge and the caller count
//// move when a request arrives, the counters and the latency histogram when
//// it finishes, where the route and the status are known.
////
//// Names, descriptions, bucket bounds and the caller vocabulary are shared
//// with the other rails, and //domains/platform/libs/otel_contract pins the
//// constants below equal to theirs.

import gleam/dict.{type Dict}
import gleam/erlang/process.{type Subject}
import gleam/http
import gleam/list
import gleam/option
import gleam/order
import gleam/otp/actor
import gleam/result
import gleam/string

/// HTTP latency buckets in microseconds, 100µs to 10s.
pub const latency_bucket_bounds_micros = [
  100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10_000.0, 25_000.0, 50_000.0,
  100_000.0, 250_000.0, 500_000.0, 1_000_000.0, 2_500_000.0, 10_000_000.0,
]

pub const instrument_descriptions = [
  #("http_server_requests", "HTTP requests received"),
  #(
    "http_server_requests_success",
    "HTTP requests completed successfully (2xx-3xx)",
  ),
  #("http_server_requests_failure", "HTTP requests that returned 4xx or 5xx"),
  #("http_server_requests_active_gauge", "HTTP requests currently in flight"),
  #(
    "http_server_request_duration_microseconds",
    "HTTP request duration in microseconds",
  ),
  #(
    "http_server_requests_by_caller",
    "HTTP requests received, by who sent them",
  ),
]

/// Callers a direct request may name in its User-Agent's first product token.
pub const internal_callers = ["games_hub", "mcpserver", "one_d4_worker"]

/// Anything the gateway forwarded, which always adds X-Forwarded-For.
pub const edge_caller = "edge"

pub const other_caller = "other"

pub type RouteKey {
  RouteKey(method: String, route: String)
}

pub type RouteStats {
  RouteStats(
    requests: Int,
    success: Int,
    failure: Int,
    duration_count: Int,
    duration_sum: Float,
    buckets: List(Int),
  )
}

/// Cumulative totals since the service started, each list sorted by key.
pub type Snapshot {
  Snapshot(
    service_name: String,
    routes: List(#(RouteKey, RouteStats)),
    active: List(#(String, Int)),
    callers: List(#(String, Int)),
  )
}

pub opaque type Metrics {
  Metrics(subject: Subject(Message))
}

type Message {
  Started(method: String, caller: String)
  Finished(method: String, route: String, status: Int, micros: Float)
  TakeSnapshot(reply: Subject(Snapshot))
}

type State {
  State(
    service_name: String,
    routes: Dict(RouteKey, RouteStats),
    active: Dict(String, Int),
    callers: Dict(String, Int),
  )
}

/// Headers as gleam_http holds them, names lowercased.
pub fn caller_of(headers: List(#(String, String))) -> String {
  case list.key_find(headers, "x-forwarded-for") {
    Ok(_) -> edge_caller
    Error(Nil) -> {
      let product =
        list.key_find(headers, "user-agent")
        |> result.unwrap("")
        |> first_token
      case list.contains(internal_callers, product) {
        True -> product
        False -> other_caller
      }
    }
  }
}

fn first_token(user_agent: String) -> String {
  let #(before_slash, _) =
    string.split_once(user_agent, "/") |> result.unwrap(#(user_agent, ""))
  let #(token, _) =
    string.split_once(before_slash, " ") |> result.unwrap(#(before_slash, ""))
  token
}

/// The nine RFC 9110 methods by name, and any extension method as CUSTOM, so
/// a client inventing verbs cannot mint a series per verb.
pub fn method_label(method: http.Method) -> String {
  case method {
    http.Get -> "GET"
    http.Head -> "HEAD"
    http.Post -> "POST"
    http.Put -> "PUT"
    http.Delete -> "DELETE"
    http.Connect -> "CONNECT"
    http.Options -> "OPTIONS"
    http.Trace -> "TRACE"
    http.Patch -> "PATCH"
    http.Other(_) -> "CUSTOM"
  }
}

/// The explicit bucket holding a duration: the first whose bound is at least
/// it, or the overflow bucket past the last bound.
pub fn bucket_of(micros: Float) -> Int {
  latency_bucket_bounds_micros
  |> list.take_while(fn(bound) { bound <. micros })
  |> list.length
}

pub fn start(service_name: String) -> Result(Metrics, actor.StartError) {
  let callers =
    [edge_caller, other_caller, ..internal_callers]
    |> list.map(fn(caller) { #(caller, 0) })
    |> dict.from_list
  actor.new(State(service_name, dict.new(), dict.new(), callers))
  |> actor.on_message(update)
  |> actor.start
  |> result.map(fn(started) { Metrics(started.data) })
}

pub fn request_started(
  metrics: Metrics,
  method: String,
  caller: String,
) -> Nil {
  process.send(metrics.subject, Started(method, caller))
}

pub fn request_finished(
  metrics: Metrics,
  method: String,
  route: String,
  status: Int,
  micros: Float,
) -> Nil {
  process.send(metrics.subject, Finished(method, route, status, micros))
}

pub fn snapshot(metrics: Metrics) -> Snapshot {
  process.call(metrics.subject, 5000, TakeSnapshot)
}

fn update(state: State, message: Message) -> actor.Next(State, Message) {
  case message {
    Started(method, caller) ->
      State(
        ..state,
        active: add(state.active, method, 1),
        callers: add(state.callers, caller, 1),
      )
      |> actor.continue
    Finished(method, route, status, micros) -> {
      let bucket = bucket_of(micros)
      let routes =
        dict.upsert(state.routes, RouteKey(method, route), fn(existing) {
          let stats = option.lazy_unwrap(existing, empty_stats)
          let succeeded = status < 400
          RouteStats(
            requests: stats.requests + 1,
            success: stats.success + bool_to_int(succeeded),
            failure: stats.failure + bool_to_int(!succeeded),
            duration_count: stats.duration_count + 1,
            duration_sum: stats.duration_sum +. micros,
            buckets: list.index_map(stats.buckets, fn(count, i) {
              count + bool_to_int(i == bucket)
            }),
          )
        })
      let active = add(state.active, method, -1)
      actor.continue(State(..state, routes:, active:))
    }
    TakeSnapshot(reply) -> {
      process.send(
        reply,
        Snapshot(
          service_name: state.service_name,
          routes: dict.to_list(state.routes)
            |> list.sort(fn(a, b) { compare_keys(a.0, b.0) }),
          active: sorted(state.active),
          callers: sorted(state.callers),
        ),
      )
      actor.continue(state)
    }
  }
}

fn empty_stats() -> RouteStats {
  RouteStats(
    requests: 0,
    success: 0,
    failure: 0,
    duration_count: 0,
    duration_sum: 0.0,
    buckets: list.repeat(0, list.length(latency_bucket_bounds_micros) + 1),
  )
}

fn add(counts: Dict(String, Int), key: String, by: Int) -> Dict(String, Int) {
  dict.upsert(counts, key, fn(existing) { option.unwrap(existing, 0) + by })
}

fn sorted(counts: Dict(String, Int)) -> List(#(String, Int)) {
  dict.to_list(counts) |> list.sort(fn(a, b) { string.compare(a.0, b.0) })
}

fn compare_keys(a: RouteKey, b: RouteKey) -> order.Order {
  string.compare(a.method, b.method)
  |> order.lazy_break_tie(fn() { string.compare(a.route, b.route) })
}

fn bool_to_int(value: Bool) -> Int {
  case value {
    True -> 1
    False -> 0
  }
}
