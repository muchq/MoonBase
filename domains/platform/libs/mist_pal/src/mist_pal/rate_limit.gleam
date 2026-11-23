//// A per-client rate limit: GCRA, the algorithm under server_pal's
//// tower_governor, so a client gets the same quota from either rail.
////
//// Each client is one number, the theoretical arrival time of its next
//// request. A request is admitted while that time is no more than
//// burst - 1 periods away, and moves it one period on; a refusal moves
//// nothing, so it costs no quota.

import gleam/dict.{type Dict}
import gleam/erlang/process.{type Subject}
import gleam/float
import gleam/int
import gleam/option.{type Option, None, Some}
import gleam/otp/actor
import gleam/result
import mist_pal/env

pub type RateLimit {
  RateLimit(
    /// Sustained requests per second. Fractional rates are allowed: 0.1 is
    /// one request every ten seconds.
    per_second: Float,
    /// How many may arrive at once before the sustained rate binds.
    burst: Int,
  )
}

/// server_pal's default.
pub const default = RateLimit(per_second: 100.0, burst: 200)

/// How often the limiter forgets clients whose quota has fully refilled.
const sweep_every_micros = 60_000_000

pub opaque type Limiter(key) {
  Limiter(subject: Subject(Message(key)))
}

type Message(key) {
  Check(key: key, now: Int, reply: Subject(Result(Nil, Int)))
  Tracked(reply: Subject(Int))
}

type State(key) {
  State(
    period: Int,
    tolerance: Int,
    sweep_every: Int,
    last_sweep: Option(Int),
    arrivals: Dict(key, Int),
  )
}

pub fn start(limit: RateLimit) -> Result(Limiter(key), String) {
  start_sweeping(limit, sweep_every_micros)
}

/// `start`, forgetting refilled clients every `every` microseconds.
pub fn start_sweeping(
  limit: RateLimit,
  every: Int,
) -> Result(Limiter(key), String) {
  use period <- result.try(period_micros(limit.per_second))
  use <- bool_guard(
    limit.burst < 1,
    "rate_limit burst must be at least 1, got " <> int.to_string(limit.burst),
  )
  State(
    period:,
    tolerance: period * { limit.burst - 1 },
    sweep_every: every,
    last_sweep: None,
    arrivals: dict.new(),
  )
  |> actor.new
  |> actor.on_message(update)
  |> actor.start
  |> result.map(fn(started) { Limiter(started.data) })
  |> result.map_error(fn(_) { "the rate limiter did not start" })
}

fn period_micros(per_second: Float) -> Result(Int, String) {
  let refuse = fn(why) {
    Error(
      "rate_limit per_second " <> why <> ", got " <> float.to_string(per_second),
    )
  }
  case per_second >. 0.0 {
    False -> refuse("must be a positive rate")
    True ->
      case float.round(1_000_000.0 /. per_second) {
        0 -> refuse("rounds to a zero-length period")
        period -> Ok(period)
      }
  }
}

fn bool_guard(
  refuse: Bool,
  why: String,
  next: fn() -> Result(a, String),
) -> Result(a, String) {
  case refuse {
    True -> Error(why)
    False -> next()
  }
}

/// Admits a request from `key` now, or says how many microseconds it must
/// wait.
pub fn check(limiter: Limiter(key), key: key) -> Result(Nil, Int) {
  check_at(limiter, key, env.monotonic_micros())
}

/// `check` at `now`, in monotonic microseconds.
pub fn check_at(limiter: Limiter(key), key: key, now: Int) -> Result(Nil, Int) {
  process.call(limiter.subject, 5000, Check(key, now, _))
}

/// How many clients the limiter is holding state for.
pub fn tracked(limiter: Limiter(key)) -> Int {
  process.call(limiter.subject, 5000, Tracked)
}

/// A wait as a Retry-After value: whole seconds, rounded up, since 0 tells
/// a client to retry at once.
pub fn retry_after_seconds(wait_micros: Int) -> Int {
  int.max(1, { wait_micros + 999_999 } / 1_000_000)
}

fn update(
  state: State(key),
  message: Message(key),
) -> actor.Next(State(key), Message(key)) {
  case message {
    Tracked(reply) -> {
      process.send(reply, dict.size(state.arrivals))
      actor.continue(state)
    }
    Check(key, now, reply) -> {
      let state = sweep(state, now)
      let arrival = case dict.get(state.arrivals, key) {
        Ok(arrival) -> int.max(arrival, now)
        Error(Nil) -> now
      }
      let ahead = arrival - now
      case ahead > state.tolerance {
        True -> {
          process.send(reply, Error(ahead - state.tolerance))
          actor.continue(state)
        }
        False -> {
          process.send(reply, Ok(Nil))
          let arrivals =
            dict.insert(state.arrivals, key, arrival + state.period)
          actor.continue(State(..state, arrivals:))
        }
      }
    }
  }
}

// A client whose next arrival is already due has a full bucket, the same as
// a client never seen, so forgetting it changes no decision.
fn sweep(state: State(key), now: Int) -> State(key) {
  case state.last_sweep {
    None -> State(..state, last_sweep: Some(now))
    Some(last) if now - last < state.sweep_every -> state
    Some(_) ->
      State(
        ..state,
        last_sweep: Some(now),
        arrivals: dict.filter(state.arrivals, fn(_, arrival) { arrival > now }),
      )
  }
}
