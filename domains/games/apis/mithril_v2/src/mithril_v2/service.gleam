//// The HTTP surface, kept free of the server so tests drive it directly.
//// Status codes follow mithril v1's server_pal stack: 406 for a client that
//// will not take JSON, on every route; 413 past the body limit; 415 for a
//// body not declared JSON; 400 for one that does not parse; 422 for one of
//// the wrong shape or with a word that fails validation.

import gleam/bool
import gleam/dynamic/decode
import gleam/http
import gleam/http/request.{type Request}
import gleam/http/response.{type Response}
import gleam/json
import gleam/list
import gleam/option.{type Option}
import gleam/result
import gleam/string
import mist_pal.{type Body, Body, TooLarge, Unreadable}
import mithril_v2/wordchain.{type Graph}

pub const wordchain_route = "/mithril/v2/wordchain"

/// The largest body read, in bytes.
pub const max_body_bytes = 4_194_304

pub fn handle(req: Request(Body), graph: Graph) -> Response(String) {
  use <- bool.guard(!accepts_json(req), text(406, ""))
  case req.path, req.method {
    "/health", http.Get | "/health", http.Head -> text(200, "Ok")
    "/health", _ -> text(405, "")
    "/mithril/v2/wordchain", http.Post -> wordchain(req, graph)
    "/mithril/v2/wordchain", _ -> text(405, "")
    _, _ -> text(404, "Not Found")
  }
}

/// The metric route label for a request path.
pub fn route_label(path: String) -> String {
  case path {
    "/health" -> mist_pal.health_route
    "/mithril/v2/wordchain" -> wordchain_route
    _ -> mist_pal.unmatched_route
  }
}

fn wordchain(req: Request(Body), graph: Graph) -> Response(String) {
  let outcome = {
    use bytes <- result.try(case req.body {
      Body(bytes) -> Ok(bytes)
      TooLarge -> Error(text(413, "length limit exceeded"))
      Unreadable -> Error(text(400, "body could not be read"))
    })
    use <- bool.guard(
      !declares_json(req),
      Error(text(415, "Expected request with `Content-Type: application/json`")),
    )
    use #(start, end) <- result.try(decode(bytes))
    use start <- result.try(validate("start", start))
    use end <- result.try(validate("end", end))
    Ok(wordchain.shortest_path(graph, start, end))
  }
  case outcome {
    Ok(path) ->
      response.new(200)
      |> response.set_header("content-type", "application/json")
      |> response.set_body(encode(path))
    Error(resp) -> resp
  }
}

fn decode(bytes: BitArray) -> Result(#(String, String), Response(String)) {
  let decoder = {
    use start <- decode.field("start", decode.string)
    use end <- decode.field("end", decode.string)
    decode.success(#(start, end))
  }
  case json.parse_bits(bytes, decoder) {
    Ok(words) -> Ok(words)
    Error(json.UnableToDecode(_)) ->
      Error(text(422, "body is not {\"start\": word, \"end\": word}"))
    Error(_) -> Error(text(400, "body is not JSON"))
  }
}

fn validate(field: String, word: String) -> Result(String, Response(String)) {
  let bytes = string.byte_size(word)
  let problem = case word {
    "" -> Error("word cannot be empty")
    _ if bytes < 3 -> Error("word must be at least 3 chars")
    _ if bytes > 9 -> Error("word must be at most 9 chars")
    _ ->
      case only_letters(word) {
        True -> Ok(word)
        False -> Error("word must contain only letters")
      }
  }
  result.map_error(problem, fn(why) { text(422, field <> ": " <> why) })
}

@external(erlang, "mithril_v2_ffi", "only_letters")
fn only_letters(word: String) -> Bool

pub fn encode(path: Option(List(String))) -> String {
  json.object([#("path", json.nullable(path, json.array(_, json.string)))])
  |> json.to_string
}

// No Accept header accepts anything; of several, any one will do.
fn accepts_json(req: Request(a)) -> Bool {
  case list.key_filter(req.headers, "accept") {
    [] -> True
    accepts ->
      list.flat_map(accepts, string.split(_, ","))
      |> list.any(fn(range) {
        case media_type(range) {
          "*/*" | "application/*" | "application/json" -> True
          _ -> False
        }
      })
  }
}

// application/json, or any application/...+json.
fn declares_json(req: Request(a)) -> Bool {
  case request.get_header(req, "content-type") {
    Error(Nil) -> False
    Ok(content_type) ->
      case media_type(content_type) {
        "application/json" -> True
        "application/" <> subtype -> string.ends_with(subtype, "+json")
        _ -> False
      }
  }
}

// The type/subtype of a media type, lowercased, its parameters dropped.
fn media_type(value: String) -> String {
  let #(media, _) = string.split_once(value, ";") |> result.unwrap(#(value, ""))
  string.lowercase(string.trim(media))
}

fn text(status: Int, body: String) -> Response(String) {
  response.new(status)
  |> response.set_header("content-type", "text/plain; charset=utf-8")
  |> response.set_body(body)
}
