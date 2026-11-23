//// mithril v2: the word-ladder solver behind /mithril/v2/wordchain.

import mist_pal
import mist_pal/log
import mithril_v2/service
import mithril_v2/wordchain.{type Graph}

pub fn main() -> Nil {
  let dir = wordchain.data_dir()
  log.info("reading word graph from " <> dir, [])
  let graph = case wordchain.load(dir) {
    Ok(graph) -> graph
    Error(reason) -> {
      log.error("cannot load the word graph: " <> reason, [])
      panic as { "cannot load the word graph: " <> reason }
    }
  }

  // Read by every connection's process without a copy.
  store_graph(graph)

  mist_pal.new(fn(req) { service.handle(req, stored_graph()) })
  |> mist_pal.max_body_bytes(service.max_body_bytes)
  |> mist_pal.route_label(service.route_label)
  |> mist_pal.serve
}

@external(erlang, "mithril_v2_ffi", "store_graph")
fn store_graph(graph: Graph) -> Nil

@external(erlang, "mithril_v2_ffi", "stored_graph")
fn stored_graph() -> Graph
