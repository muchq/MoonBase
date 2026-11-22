//// The HTTP contract, pinned against mithril v1's answers to the same
//// requests: status, content type and, where a client reads it, the body.

import gleam/http
import gleam/http/request
import gleam/http/response.{type Response}
import gleam/list
import gleeunit/should
import mithril_v2/http_metrics
import mithril_v2/service.{Body, TooLarge}
import mithril_v2/wordchain

const wordchain_path = "/mithril/v2/wordchain"

fn graph() -> wordchain.Graph {
  // 0 card, 1 cold, 2 cord, 3 ward, 4 warm: cold-cord-card-ward-warm.
  let assert Ok(graph) =
    wordchain.from_edges(["card", "cold", "cord", "ward", "warm"], <<
      1:size(64), 2:size(64), 2:size(64), 0:size(64), 0:size(64), 3:size(64),
      3:size(64), 4:size(64),
    >>)
  graph
}

fn post(body: String) -> request.Request(service.Body) {
  request.new()
  |> request.set_method(http.Post)
  |> request.set_path(wordchain_path)
  |> request.set_header("content-type", "application/json")
  |> request.set_body(Body(<<body:utf8>>))
}

fn get(path: String) -> request.Request(service.Body) {
  request.new()
  |> request.set_path(path)
  |> request.set_body(Body(<<>>))
}

fn send(req: request.Request(service.Body)) -> Response(String) {
  service.handle(req, graph())
}

fn content_type(resp: Response(String)) -> Result(String, Nil) {
  response.get_header(resp, "content-type")
}

// The bytes games_hub's client pins in mithril_cpp's wordchain_wire_test.
pub fn a_ladder_is_answered_byte_for_byte_test() {
  let resp = send(post("{\"end\":\"warm\",\"start\":\"cold\"}"))
  resp.status |> should.equal(200)
  resp.body
  |> should.equal("{\"path\":[\"cold\",\"cord\",\"card\",\"ward\",\"warm\"]}")
  content_type(resp) |> should.equal(Ok("application/json"))
}

pub fn no_ladder_is_a_null_path_test() {
  let resp = send(post("{\"start\":\"cold\",\"end\":\"zzzz\"}"))
  resp.status |> should.equal(200)
  resp.body |> should.equal("{\"path\":null}")
}

pub fn unknown_fields_are_ignored_test() {
  send(post("{\"start\":\"cold\",\"end\":\"cord\",\"x\":1}")).body
  |> should.equal("{\"path\":[\"cold\",\"cord\"]}")
}

pub fn a_word_is_its_own_ladder_even_off_the_dictionary_test() {
  send(post("{\"start\":\"zzzq\",\"end\":\"zzzq\"}")).body
  |> should.equal("{\"path\":[\"zzzq\"]}")
}

// Validation accepts nine letters, but the dictionary holds only words
// shorter than that, so a nine-letter pair has no ladder unless it is one
// word twice.
pub fn nine_letters_pass_validation_test() {
  send(post("{\"start\":\"abcdefghi\",\"end\":\"abcdefghi\"}")).body
  |> should.equal("{\"path\":[\"abcdefghi\"]}")
  send(post("{\"start\":\"abcdefghi\",\"end\":\"abcdefghj\"}")).body
  |> should.equal("{\"path\":null}")
}

pub fn words_are_not_lowercased_test() {
  send(post("{\"start\":\"Cold\",\"end\":\"warm\"}")).body
  |> should.equal("{\"path\":null}")
}

// Length is counted in bytes, so two two-byte letters are four.
pub fn non_ascii_letters_are_letters_test() {
  let resp = send(post("{\"start\":\"éé\",\"end\":\"éé\"}"))
  resp.status |> should.equal(200)
  resp.body |> should.equal("{\"path\":[\"éé\"]}")
  send(post("{\"start\":\"café\",\"end\":\"cafe\"}")).body
  |> should.equal("{\"path\":null}")
}

fn status_of(body: String) -> Int {
  send(post(body)).status
}

pub fn a_word_that_fails_validation_is_unprocessable_test() {
  [
    "{\"start\":\"\",\"end\":\"warm\"}",
    "{\"start\":\"co\",\"end\":\"warm\"}",
    "{\"start\":\"cold\",\"end\":\"wa\"}",
    "{\"start\":\"abcdefghij\",\"end\":\"warm\"}",
    "{\"start\":\"c0ld\",\"end\":\"warm\"}",
    "{\"start\":\"co ld\",\"end\":\"warm\"}",
    // A combining accent is a mark, not a letter.
    "{\"start\":\"cafe\\u0301\",\"end\":\"cafe\"}",
  ]
  |> list.map(status_of)
  |> should.equal([422, 422, 422, 422, 422, 422, 422])
}

pub fn a_body_of_the_wrong_shape_is_unprocessable_test() {
  [
    "{\"start\":\"cold\"}",
    "{\"start\":1,\"end\":\"warm\"}",
    "{\"start\":null,\"end\":\"warm\"}",
    "[]",
    "\"cold\"",
  ]
  |> list.map(status_of)
  |> should.equal([422, 422, 422, 422, 422])
}

pub fn a_validation_failure_says_which_word_and_why_test() {
  let resp = send(post("{\"start\":\"co\",\"end\":\"warm\"}"))
  resp.body |> should.equal("start: word must be at least 3 chars")
  content_type(resp) |> should.equal(Ok("text/plain; charset=utf-8"))
}

pub fn a_body_that_is_not_json_is_a_bad_request_test() {
  ["{\"start\":\"cold\",", "{\"start\":\"cold\",\"end\":\"warm\"} x", ""]
  |> list.map(status_of)
  |> should.equal([400, 400, 400])
  send(
    post("")
    |> request.set_body(Body(<<"{\"start\":\"":utf8, 255, "\"}":utf8>>)),
  ).status
  |> should.equal(400)
}

pub fn a_body_past_four_mebibytes_is_too_large_test() {
  send(post("") |> request.set_body(TooLarge)).status
  |> should.equal(413)
}

pub fn the_body_must_be_declared_json_test() {
  let body = "{\"start\":\"cold\",\"end\":\"warm\"}"
  let with = fn(content_type) {
    send(post(body) |> request.set_header("content-type", content_type)).status
  }
  with("text/plain") |> should.equal(415)
  with("application/jsonx") |> should.equal(415)
  with("application/json; charset=utf-8") |> should.equal(200)
  with("Application/JSON") |> should.equal(200)
  with("application/vnd.muchq+json") |> should.equal(200)

  let undeclared =
    request.new()
    |> request.set_method(http.Post)
    |> request.set_path(wordchain_path)
    |> request.set_body(Body(<<body:utf8>>))
  send(undeclared).status |> should.equal(415)
}

pub fn a_client_must_accept_json_test() {
  let with = fn(req, accept) {
    send(req |> request.set_header("accept", accept)).status
  }
  let body = "{\"start\":\"cold\",\"end\":\"warm\"}"
  with(post(body), "text/html") |> should.equal(406)
  with(post(body), "*/*") |> should.equal(200)
  with(post(body), "application/*") |> should.equal(200)
  with(post(body), "text/html, application/json;q=0.9") |> should.equal(200)
  // Every route, the probe and the fallback included.
  with(get("/health"), "text/html") |> should.equal(406)
  with(get("/nope"), "text/html") |> should.equal(406)
}

pub fn health_answers_ok_test() {
  let resp = send(get("/health"))
  resp.status |> should.equal(200)
  resp.body |> should.equal("Ok")
  content_type(resp) |> should.equal(Ok("text/plain; charset=utf-8"))
  send(get("/health") |> request.set_method(http.Head)).status
  |> should.equal(200)
}

pub fn a_known_path_with_the_wrong_method_is_not_allowed_test() {
  send(get(wordchain_path)).status |> should.equal(405)
  send(get("/health") |> request.set_method(http.Post)).status
  |> should.equal(405)
}

pub fn any_other_path_is_not_found_test() {
  let resp = send(get("/nope"))
  resp.status |> should.equal(404)
  resp.body |> should.equal("Not Found")
  send(post("") |> request.set_path(wordchain_path <> "/")).status
  |> should.equal(404)
  send(post("") |> request.set_path("/mithril/v1/wordchain")).status
  |> should.equal(404)
}

pub fn routes_are_labelled_by_template_and_the_rest_as_unmatched_test() {
  [
    service.route_label(post("")),
    service.route_label(get(wordchain_path)),
    service.route_label(get("/health")),
    service.route_label(get("/nope")),
    service.route_label(get("/health/")),
  ]
  |> should.equal([
    wordchain_path,
    wordchain_path,
    "/health",
    "unmatched",
    "unmatched",
  ])
}

// Where mithril v2 parts from v1, whose JSON decoder reads a struct from an
// array of its fields and refuses a repeated key. Here an array is the wrong
// shape, and the first of a repeated key is the one read.
pub fn v1_quirks_that_are_not_kept_test() {
  status_of("[\"cold\",\"warm\"]") |> should.equal(422)
  send(post("{\"start\":\"cold\",\"start\":\"warm\",\"end\":\"cord\"}")).body
  |> should.equal("{\"path\":[\"cold\",\"cord\"]}")
}

fn metrics() -> http_metrics.Metrics {
  let assert Ok(metrics) = http_metrics.start("mithril_v2")
  metrics
}

pub fn a_served_request_is_counted_by_caller_route_and_outcome_test() {
  let metrics = metrics()
  let req =
    post("{\"start\":\"cold\",\"end\":\"warm\"}")
    |> request.set_header("x-forwarded-for", "203.0.113.7")
  service.instrumented(metrics, req, send).status |> should.equal(200)
  service.instrumented(metrics, get("/nope"), send).status |> should.equal(404)

  let snapshot = http_metrics.snapshot(metrics)
  list.key_find(snapshot.callers, "edge") |> should.equal(Ok(1))
  list.key_find(snapshot.callers, "other") |> should.equal(Ok(1))
  snapshot.active |> should.equal([#("GET", 0), #("POST", 0)])
  snapshot.routes
  |> list.map(fn(route) { #(route.0, { route.1 }.success, { route.1 }.failure) })
  |> should.equal([
    #(http_metrics.RouteKey("GET", "unmatched"), 0, 1),
    #(http_metrics.RouteKey("POST", "/mithril/v2/wordchain"), 1, 0),
  ])
}

// A handler that crashes answers 500 and is still counted, so the gauge
// cannot drift and the failure shows on the dashboard.
pub fn a_crashing_handler_is_a_counted_500_test() {
  let metrics = metrics()
  service.instrumented(metrics, get("/health"), fn(_) { panic as "boom" }).status
  |> should.equal(500)

  let snapshot = http_metrics.snapshot(metrics)
  snapshot.active |> should.equal([#("GET", 0)])
  let assert [#(_, stats)] = snapshot.routes
  #(stats.requests, stats.failure) |> should.equal(#(1, 1))
}
