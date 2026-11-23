import gleam/http
import gleam/list
import gleeunit/should
import mist_pal/http_metrics.{RouteKey, RouteStats}

pub fn a_forwarded_request_came_from_the_edge_whoever_it_names_test() {
  http_metrics.caller_of([
    #("user-agent", "games_hub/1.0"),
    #("x-forwarded-for", "203.0.113.7"),
  ])
  |> should.equal("edge")
}

pub fn a_direct_request_is_named_by_its_first_product_token_test() {
  [
    [#("user-agent", "games_hub/1.0")],
    [#("user-agent", "games_hub")],
    [#("user-agent", "mcpserver (java)")],
    [#("user-agent", "one_d4_worker/2 extra")],
    [#("user-agent", "games_hubx/1.0")],
    [#("user-agent", "curl/8.7.1")],
    [#("user-agent", "")],
    [],
  ]
  |> list.map(http_metrics.caller_of)
  |> should.equal([
    "games_hub", "games_hub", "mcpserver", "one_d4_worker", "other", "other",
    "other", "other",
  ])
}

pub fn methods_outside_rfc_9110_share_one_label_test() {
  [http.Get, http.Head, http.Post, http.Patch, http.Other("PURGE")]
  |> list.map(http_metrics.method_label)
  |> should.equal(["GET", "HEAD", "POST", "PATCH", "CUSTOM"])
}

// Explicit buckets hold (previous bound, bound]; past the last bound is the
// overflow bucket.
pub fn a_duration_lands_in_the_first_bucket_bounding_it_test() {
  [0.0, 100.0, 100.5, 250.0, 9_999_999.0, 10_000_000.0, 10_000_001.0]
  |> list.map(http_metrics.bucket_of)
  |> should.equal([0, 0, 1, 1, 14, 14, 15])
}

fn started() -> http_metrics.Metrics {
  let assert Ok(metrics) = http_metrics.start("widgets")
  metrics
}

pub fn every_caller_is_reported_at_zero_before_any_request_test() {
  let snapshot = http_metrics.snapshot(started())
  snapshot.service_name |> should.equal("widgets")
  snapshot.routes |> should.equal([])
  snapshot.active |> should.equal([])
  snapshot.callers
  |> should.equal([
    #("edge", 0),
    #("games_hub", 0),
    #("mcpserver", 0),
    #("one_d4_worker", 0),
    #("other", 0),
  ])
}

pub fn a_request_in_flight_is_counted_only_by_caller_and_gauge_test() {
  let metrics = started()
  http_metrics.request_started(metrics, "POST", "games_hub")
  let snapshot = http_metrics.snapshot(metrics)
  snapshot.active |> should.equal([#("POST", 1)])
  snapshot.routes |> should.equal([])
  list.key_find(snapshot.callers, "games_hub") |> should.equal(Ok(1))
}

pub fn a_finished_request_is_counted_against_its_route_test() {
  let metrics = started()
  http_metrics.request_started(metrics, "POST", "edge")
  http_metrics.request_finished(
    metrics,
    "POST",
    "/widgets/v1/widget",
    200,
    150.0,
  )
  let snapshot = http_metrics.snapshot(metrics)
  snapshot.active |> should.equal([#("POST", 0)])
  snapshot.routes
  |> should.equal([
    #(
      RouteKey("POST", "/widgets/v1/widget"),
      RouteStats(
        requests: 1,
        success: 1,
        failure: 0,
        duration_count: 1,
        duration_sum: 150.0,
        buckets: [0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
      ),
    ),
  ])
}

pub fn three_hundreds_succeed_and_four_hundreds_fail_test() {
  let metrics = started()
  list.each([200, 304, 399, 400, 404, 500], fn(status) {
    http_metrics.request_started(metrics, "GET", "other")
    http_metrics.request_finished(metrics, "GET", "unmatched", status, 50.0)
  })
  let assert [#(_, stats)] = http_metrics.snapshot(metrics).routes
  #(stats.requests, stats.success, stats.failure)
  |> should.equal(#(6, 3, 3))
  stats.duration_sum |> should.equal(300.0)
}

pub fn routes_and_methods_are_separate_series_test() {
  let metrics = started()
  http_metrics.request_finished(metrics, "GET", "/health", 200, 1.0)
  http_metrics.request_finished(metrics, "HEAD", "/health", 200, 1.0)
  http_metrics.request_finished(metrics, "GET", "unmatched", 404, 1.0)
  http_metrics.request_finished(metrics, "GET", "/health", 200, 1.0)
  http_metrics.snapshot(metrics).routes
  |> list.map(fn(route) { #(route.0, { route.1 }.requests) })
  |> should.equal([
    #(RouteKey("GET", "/health"), 2),
    #(RouteKey("GET", "unmatched"), 1),
    #(RouteKey("HEAD", "/health"), 1),
  ])
}

@external(erlang, "mist_pal_test_ffi", "with_byte_255")
fn with_byte_255(text: String) -> String

// A header value is bytes a client chose, not necessarily text.
pub fn a_user_agent_that_is_not_utf8_is_another_caller_test() {
  [
    [#("user-agent", with_byte_255("games_hub"))],
    [#("user-agent", with_byte_255("games_hub") <> "/1.0")],
  ]
  |> list.map(http_metrics.caller_of)
  |> should.equal(["other", "other"])
}
