import gleam/list
import gleeunit/should
import mist_pal/rate_limit.{RateLimit}

const second = 1_000_000

fn started(limit: rate_limit.RateLimit) -> rate_limit.Limiter(String) {
  let assert Ok(limiter) = rate_limit.start(limit)
  limiter
}

// A fresh client may send `burst` at once; the next waits one period.
pub fn a_burst_is_admitted_whole_and_no_more_test() {
  let limiter = started(RateLimit(per_second: 10.0, burst: 3))
  list.repeat("a", 4)
  |> list.map(rate_limit.check_at(limiter, _, 0))
  |> should.equal([Ok(Nil), Ok(Nil), Ok(Nil), Error(100_000)])
}

// per_second is a rate, not tower_governor's replenish interval: 100 per
// second is one every 10ms, not one every 100s.
pub fn per_second_means_requests_per_second_test() {
  let limiter = started(RateLimit(per_second: 100.0, burst: 1))
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  rate_limit.check_at(limiter, "a", 9999) |> should.equal(Error(1))
  rate_limit.check_at(limiter, "a", 10_000) |> should.equal(Ok(Nil))
}

pub fn fractional_rates_are_allowed_test() {
  let limiter = started(RateLimit(per_second: 0.1, burst: 1))
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  rate_limit.check_at(limiter, "a", 9 * second)
  |> should.equal(Error(second))
  rate_limit.check_at(limiter, "a", 10 * second) |> should.equal(Ok(Nil))
}

pub fn clients_have_buckets_of_their_own_test() {
  let limiter = started(RateLimit(per_second: 1.0, burst: 1))
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  rate_limit.check_at(limiter, "a", 0) |> should.be_error
  rate_limit.check_at(limiter, "b", 0) |> should.equal(Ok(Nil))
}

// A refused request costs nothing, so a client hammering past its limit is
// admitted again as soon as the rate allows.
pub fn a_refusal_does_not_spend_quota_test() {
  let limiter = started(RateLimit(per_second: 1.0, burst: 1))
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  list.repeat(Nil, 50)
  |> list.index_map(fn(_, i) { rate_limit.check_at(limiter, "a", i * 1000) })
  rate_limit.check_at(limiter, "a", second) |> should.equal(Ok(Nil))
}

// An idle client refills to a full burst, and no further.
pub fn an_idle_client_refills_to_its_burst_test() {
  let limiter = started(RateLimit(per_second: 1.0, burst: 2))
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  let later = 3600 * second
  list.repeat("a", 3)
  |> list.map(rate_limit.check_at(limiter, _, later))
  |> should.equal([Ok(Nil), Ok(Nil), Error(second)])
}

// Keyed per client, the state would otherwise hold every address that ever
// called. A client whose bucket has refilled is indistinguishable from a new
// one, so forgetting it changes no decision.
pub fn fully_refilled_clients_are_forgotten_test() {
  let assert Ok(limiter) =
    rate_limit.start_sweeping(RateLimit(per_second: 1.0, burst: 1), 60 * second)
  rate_limit.check_at(limiter, "a", 0) |> should.equal(Ok(Nil))
  rate_limit.check_at(limiter, "b", 59 * second) |> should.equal(Ok(Nil))
  rate_limit.tracked(limiter) |> should.equal(2)
  rate_limit.check_at(limiter, "c", 60 * second - 1) |> should.equal(Ok(Nil))
  rate_limit.tracked(limiter) |> should.equal(3)
  // The sweep at 60s forgets a, and b, which refilled at 60s exactly. c is
  // still waiting, and keeps its place.
  rate_limit.check_at(limiter, "c", 60 * second) |> should.be_error
  rate_limit.tracked(limiter) |> should.equal(1)
}

pub fn a_rate_that_cannot_be_met_refuses_to_start_test() {
  [
    RateLimit(per_second: 0.0, burst: 1),
    RateLimit(per_second: -1.0, burst: 1),
    RateLimit(per_second: 3_000_000.0, burst: 1),
    RateLimit(per_second: 1.0, burst: 0),
  ]
  |> list.each(fn(limit) { rate_limit.start(limit) |> should.be_error })
}

// Retry-After is whole seconds; a wait shorter than one still says 1, since
// 0 tells a client to retry at once.
pub fn retry_after_rounds_up_to_whole_seconds_test() {
  [1, 999_999, second, second + 1]
  |> list.map(rate_limit.retry_after_seconds)
  |> should.equal([1, 1, 1, 2])
}

pub fn the_default_matches_server_pal_test() {
  rate_limit.default |> should.equal(RateLimit(per_second: 100.0, burst: 200))
}
