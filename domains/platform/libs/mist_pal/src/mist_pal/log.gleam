//// One flat JSON object per line on stdout, the shape server_pal's tracing
//// subscriber writes: timestamp, level and message, then the event's own
//// fields.
////
//// ```json
//// {"timestamp":"2026-11-23T02:08:12.069809Z","level":"INFO","message":"listening on 0.0.0.0:8083"}
//// ```
////
//// Lines go through Erlang's logger, whose default handler this formats,
//// so mist's and OTP's own reports take the same shape.

import gleam/json.{type Json}
import gleam/string

type Level {
  Info
  Warning
  Error
}

pub fn info(message: String, fields: List(#(String, Json))) -> Nil {
  log(Info, message, fields)
}

pub fn warning(message: String, fields: List(#(String, Json))) -> Nil {
  log(Warning, message, fields)
}

pub fn error(message: String, fields: List(#(String, Json))) -> Nil {
  log(Error, message, fields)
}

fn log(level: Level, message: String, fields: List(#(String, Json))) -> Nil {
  let object =
    json.object([#("message", json.string(message)), ..fields])
    |> json.to_string
  // The members alone; the formatter writes timestamp and level ahead of
  // them.
  ffi_log(level, object |> string.drop_start(1) |> string.drop_end(1))
}

@external(erlang, "mist_pal_log", "log")
fn ffi_log(level: Level, members: String) -> Nil
