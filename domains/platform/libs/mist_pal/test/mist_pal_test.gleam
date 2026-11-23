//// The server pipeline over real HTTP: the limiter keyed on the derived
//// client, /health exempt from it, and every answer counted, a 429 included.

import gleam/erlang/process.{type Subject}
import gleam/http/request.{type Request}
import gleam/http/response.{type Response}
import gleam/int
import gleam/list
import gleam/option.{None, Some}
import gleeunit
import gleeunit/should
import mist_pal.{type Body, Body, TooLarge}
import mist_pal/forwarded
import mist_pal/http_metrics.{RouteKey}
import mist_pal/rate_limit.{RateLimit}

pub fn main() {
  gleeunit.main()
}

type Method {
  Get
  Post
}

@external(erlang, "mist_pal_test_ffi", "request")
fn ffi_request(
  method: Method,
  url: String,
  headers: List(#(String, String)),
  body: String,
) -> Result(#(Int, List(#(String, String)), String), String)

fn get(
  server: mist_pal.Server,
  path: String,
  headers: List(#(String, String)),
) -> #(Int, List(#(String, String)), String) {
  let assert Ok(resp) = ffi_request(Get, url(server, path), headers, "")
  resp
}

fn post(
  server: mist_pal.Server,
  path: String,
  body: String,
) -> #(Int, List(#(String, String)), String) {
  let assert Ok(resp) = ffi_request(Post, url(server, path), [], body)
  resp
}

fn url(server: mist_pal.Server, path: String) -> String {
  "http://127.0.0.1:" <> int.to_string(server.port) <> path
}

fn status(resp: #(Int, List(#(String, String)), String)) -> Int {
  resp.0
}

// Answers 200 "ok" and reports each request it was handed.
fn recording(calls: Subject(String)) -> fn(Request(Body)) -> Response(String) {
  fn(req: Request(Body)) {
    process.send(calls, req.path)
    response.new(200) |> response.set_body("ok")
  }
}

fn labelled(path: String) -> String {
  case path {
    "/health" -> mist_pal.health_route
    "/thing" -> "/thing"
    _ -> mist_pal.unmatched_route
  }
}

fn start(
  handler: fn(Request(Body)) -> Response(String),
  limit: option.Option(rate_limit.RateLimit),
  trusted: forwarded.TrustedProxies,
) -> mist_pal.Server {
  let assert Ok(server) =
    mist_pal.new(handler)
    |> mist_pal.port(0)
    |> mist_pal.route_label(labelled)
    |> mist_pal.rate_limit(limit)
    |> mist_pal.trusted_proxies(trusted)
    |> mist_pal.start
  server
}

fn handled(calls: Subject(String)) -> List(String) {
  case process.receive(calls, 0) {
    Ok(path) -> [path, ..handled(calls)]
    Error(Nil) -> []
  }
}

const one_per_second = Some(RateLimit(per_second: 1.0, burst: 1))

pub fn past_its_limit_a_client_is_refused_before_the_handler_test() {
  let calls = process.new_subject()
  let server = start(recording(calls), one_per_second, forwarded.none())
  status(get(server, "/thing", [])) |> should.equal(200)
  let refused = get(server, "/thing", [])
  refused.0 |> should.equal(429)
  list.key_find(refused.1, "retry-after") |> should.equal(Ok("1"))
  handled(calls) |> should.equal(["/thing"])
}

// A 429 is a failure the dashboard has to show, so it is counted on the
// route it was refused from.
pub fn a_refused_request_is_counted_as_a_failure_test() {
  let server =
    start(recording(process.new_subject()), one_per_second, forwarded.none())
  get(server, "/thing", [])
  get(server, "/thing", [])
  let assert Ok(stats) =
    list.key_find(
      http_metrics.snapshot(server.metrics).routes,
      RouteKey("GET", "/thing"),
    )
  #(stats.requests, stats.success, stats.failure) |> should.equal(#(2, 1, 1))
}

// The probe's liveness signal stays off the quota path, and spends none of
// it.
pub fn health_is_never_limited_test() {
  let server =
    start(recording(process.new_subject()), one_per_second, forwarded.none())
  list.repeat("/health", 5)
  |> list.map(fn(path) { status(get(server, path, [])) })
  |> should.equal([200, 200, 200, 200, 200])
  status(get(server, "/thing", [])) |> should.equal(200)
}

// Behind a trusted proxy the peer is always the proxy; keying on it would put
// every caller in one bucket.
pub fn behind_a_trusted_proxy_each_client_has_its_own_bucket_test() {
  let assert Ok(loopback) = forwarded.parse(["127.0.0.1"])
  let server = start(recording(process.new_subject()), one_per_second, loopback)
  let from = fn(client) { [#("x-forwarded-for", client)] }
  [
    get(server, "/thing", from("203.0.113.7")),
    get(server, "/thing", from("203.0.113.7")),
    get(server, "/thing", from("198.51.100.9")),
  ]
  |> list.map(status)
  |> should.equal([200, 429, 200])
}

// From an untrusted peer the header is the client's own claim, and naming a
// new address each time buys no new bucket.
pub fn an_untrusted_peer_cannot_mint_buckets_test() {
  let server =
    start(recording(process.new_subject()), one_per_second, forwarded.none())
  [
    get(server, "/thing", [#("x-forwarded-for", "203.0.113.7")]),
    get(server, "/thing", [#("x-forwarded-for", "198.51.100.9")]),
  ]
  |> list.map(status)
  |> should.equal([200, 429])
}

pub fn no_limit_admits_everything_test() {
  let server = start(recording(process.new_subject()), None, forwarded.none())
  list.repeat("/thing", 5)
  |> list.map(fn(path) { status(get(server, path, [])) })
  |> should.equal([200, 200, 200, 200, 200])
}

pub fn a_body_past_the_limit_reaches_the_handler_as_too_large_test() {
  let handler = fn(req: Request(Body)) {
    case req.body {
      Body(bytes) -> response.new(200) |> response.set_body(bytes_seen(bytes))
      TooLarge -> response.new(413) |> response.set_body("")
      mist_pal.Unreadable -> response.new(400) |> response.set_body("")
    }
  }
  let assert Ok(server) =
    mist_pal.new(handler)
    |> mist_pal.port(0)
    |> mist_pal.max_body_bytes(16)
    |> mist_pal.rate_limit(None)
    |> mist_pal.trusted_proxies(forwarded.none())
    |> mist_pal.start
  let fits = post(server, "/thing", "0123456789abcdef")
  #(fits.0, fits.2) |> should.equal(#(200, "16"))
  status(post(server, "/thing", "0123456789abcdefg")) |> should.equal(413)
}

fn bytes_seen(bytes: BitArray) -> String {
  int.to_string(bit_array_size(bytes))
}

@external(erlang, "erlang", "byte_size")
fn bit_array_size(bytes: BitArray) -> Int

// A handler that crashes answers 500 and is still counted, so the gauge
// cannot drift and the failure shows on the dashboard.
pub fn a_crashing_handler_is_a_counted_500_test() {
  let server = start(fn(_) { panic as "boom" }, None, forwarded.none())
  status(get(server, "/thing", [])) |> should.equal(500)
  let snapshot = http_metrics.snapshot(server.metrics)
  snapshot.active |> should.equal([#("GET", 0)])
  let assert [#(RouteKey("GET", "/thing"), stats)] = snapshot.routes
  #(stats.requests, stats.failure) |> should.equal(#(1, 1))
}

pub fn requests_are_counted_by_caller_route_and_outcome_test() {
  let server = start(recording(process.new_subject()), None, forwarded.none())
  get(server, "/thing", [#("x-forwarded-for", "203.0.113.7")])
  get(server, "/nope", [#("user-agent", "games_hub/1.0")])
  let snapshot = http_metrics.snapshot(server.metrics)
  list.key_find(snapshot.callers, "edge") |> should.equal(Ok(1))
  list.key_find(snapshot.callers, "games_hub") |> should.equal(Ok(1))
  snapshot.routes
  |> list.map(fn(route) { #(route.0, { route.1 }.requests) })
  |> should.equal([
    #(RouteKey("GET", "/thing"), 1),
    #(RouteKey("GET", "unmatched"), 1),
  ])
}

pub fn without_a_route_label_only_health_is_named_test() {
  let assert Ok(server) =
    mist_pal.new(recording(process.new_subject()))
    |> mist_pal.port(0)
    |> mist_pal.rate_limit(None)
    |> mist_pal.trusted_proxies(forwarded.none())
    |> mist_pal.start
  get(server, "/health", [])
  get(server, "/thing", [])
  http_metrics.snapshot(server.metrics).routes
  |> list.map(fn(route) { route.0 })
  |> should.equal([RouteKey("GET", "/health"), RouteKey("GET", "unmatched")])
}

// A route label that raises costs the series its name, not the response or
// the gauge.
pub fn a_crashing_route_label_is_counted_as_unmatched_test() {
  let assert Ok(server) =
    mist_pal.new(recording(process.new_subject()))
    |> mist_pal.port(0)
    |> mist_pal.route_label(fn(_) { panic as "boom" })
    |> mist_pal.rate_limit(None)
    |> mist_pal.trusted_proxies(forwarded.none())
    |> mist_pal.start
  status(get(server, "/thing", [])) |> should.equal(200)
  let snapshot = http_metrics.snapshot(server.metrics)
  snapshot.active |> should.equal([#("GET", 0)])
  snapshot.routes
  |> list.map(fn(route) { #(route.0, { route.1 }.requests) })
  |> should.equal([#(RouteKey("GET", "unmatched"), 1)])
}

@external(erlang, "mist_pal_test_ffi", "mist_clock_running")
fn mist_clock_running() -> Bool

// A Bazel release starts no OTP applications, so mist's own start callback,
// which runs the clock its Date header reads, never runs unless start does.
pub fn starting_runs_mists_clock_test() {
  start(recording(process.new_subject()), None, forwarded.none())
  mist_clock_running() |> should.be_true
}
