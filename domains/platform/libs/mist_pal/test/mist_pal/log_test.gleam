import gleam/json
import gleam/list
import gleam/string
import gleeunit/should
import mist_pal/log

@external(erlang, "mist_pal_test_ffi", "capture_logs")
pub fn capture(run: fn() -> a) -> List(String)

@external(erlang, "mist_pal_test_ffi", "log_foreign")
fn log_foreign(message: String) -> Nil

// The line minus its timestamp, which is checked for shape on its own.
fn without_timestamp(line: String) -> String {
  let assert "{\"timestamp\":\"" <> rest = line
  let assert Ok(#(timestamp, rest)) = string.split_once(rest, "\",")
  // RFC 3339 in UTC, to the microsecond: 2026-11-23T02:08:11.417572Z
  string.length(timestamp) |> should.equal(27)
  string.ends_with(timestamp, "Z") |> should.be_true
  "{" <> rest
}

fn only(lines: List(String), containing: String) -> String {
  let assert [line] = list.filter(lines, string.contains(_, containing))
  line
}

// The shape server_pal's tracing subscriber writes: one flat JSON object per
// line, timestamp, level and message first, then the event's own fields.
pub fn a_line_is_one_flat_json_object_test() {
  capture(fn() { log.info("hello", [#("count", json.int(1))]) })
  |> only("hello")
  |> without_timestamp
  |> should.equal("{\"level\":\"INFO\",\"message\":\"hello\",\"count\":1}\n")
}

pub fn levels_are_spelled_as_server_pal_spells_them_test() {
  capture(fn() {
    log.warning("careful", [])
    log.error("broken", [])
  })
  |> fn(lines) { [only(lines, "careful"), only(lines, "broken")] }
  |> list.map(without_timestamp)
  |> should.equal([
    "{\"level\":\"WARN\",\"message\":\"careful\"}\n",
    "{\"level\":\"ERROR\",\"message\":\"broken\"}\n",
  ])
}

pub fn a_message_over_several_lines_is_still_one_line_test() {
  capture(fn() { log.info("one\ntwo", []) })
  |> only("one")
  |> without_timestamp
  |> should.equal("{\"level\":\"INFO\",\"message\":\"one\\ntwo\"}\n")
}

// mist and OTP log through Erlang's logger directly, and their lines take
// the same shape rather than the multi-line =WARNING REPORT= one.
pub fn a_line_from_elsewhere_takes_the_same_shape_test() {
  capture(fn() { log_foreign("Failed to lookup date, re-calculating") })
  |> only("lookup date")
  |> without_timestamp
  |> should.equal(
    "{\"level\":\"WARN\",\"message\":\"Failed to lookup date, re-calculating\"}\n",
  )
}
