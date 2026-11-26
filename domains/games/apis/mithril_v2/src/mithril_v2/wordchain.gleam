//// Word-ladder search over the graph shared with the Rust solver and iOS.

import gleam/bool
import gleam/dict.{type Dict}
import gleam/dynamic/decode
import gleam/json
import gleam/list
import gleam/option.{type Option, None, Some}
import gleam/result
import gleam/string
import mithril_v2/runtime

pub opaque type Graph {
  Graph(
    index_of: Dict(String, Int),
    words: runtime.Array(String),
    neighbours: runtime.Array(List(Int)),
  )
}

/// Where the generated graph sits in the runfiles tree.
pub fn data_dir() -> String {
  runtime.runfiles_dir() <> "/domains/games/apps/wordchains"
}

/// Load the same nodes and neighbour order used by the Rust solver.
pub fn from_json(bytes: BitArray) -> Result(Graph, String) {
  let decoder = {
    use words <- decode.field("nodes", decode.list(decode.string))
    use neighbours <- decode.field(
      "edges",
      decode.list(decode.list(decode.int)),
    )
    decode.success(#(words, neighbours))
  }
  use #(words, neighbours) <- result.try(
    json.parse_bits(bytes, decoder)
    |> result.replace_error("invalid word_graph.json"),
  )
  let count = list.length(words)
  let valid_indices =
    list.all(neighbours, fn(row) {
      list.all(row, fn(index) { index >= 0 && index < count })
    })
  use <- bool.guard(
    list.length(neighbours) != count || !valid_indices,
    Error("word graph has invalid adjacency"),
  )
  Ok(Graph(
    index_of: dict.from_list(list.index_map(words, fn(word, i) { #(word, i) })),
    words: runtime.array_from_list(words),
    neighbours: runtime.array_from_list(neighbours),
  ))
}

pub fn load(dir: String) -> Result(Graph, String) {
  runtime.read_bits(dir <> "/word_graph.json")
  |> result.try(from_json)
}

/// The shortest ladder from `start` to `end`, both ends included. A word is
/// its own ladder whether or not the dictionary has it; words of different
/// lengths, or off the dictionary, have none.
pub fn shortest_path(
  graph: Graph,
  start: String,
  end: String,
) -> Option(List(String)) {
  use <- bool.guard(start == end, Some([start]))
  use <- bool.guard(string.byte_size(start) != string.byte_size(end), None)
  case dict.get(graph.index_of, start), dict.get(graph.index_of, end) {
    Ok(from), Ok(to) ->
      search(graph, [from], [], dict.from_list([#(from, from)]), to)
      |> option.map(fn(parents) { ladder(graph, parents, to, []) })
    _, _ -> None
  }
}

// Breadth-first, one level at a time. Each word's parent is fixed when it is
// first seen, so stopping as soon as the target is seen answers the ladder a
// queue would reach on dequeuing it.
fn search(
  graph: Graph,
  level: List(Int),
  next: List(Int),
  parents: Dict(Int, Int),
  target: Int,
) -> Option(Dict(Int, Int)) {
  case level, next {
    [], [] -> None
    [], _ -> search(graph, list.reverse(next), [], parents, target)
    [word, ..rest], _ -> {
      let visit =
        list.fold_until(
          runtime.array_get(graph.neighbours, word),
          #(next, parents, False),
          fn(acc, neighbour) {
            let #(next, parents, _) = acc
            case dict.has_key(parents, neighbour) {
              True -> list.Continue(acc)
              False -> {
                let parents = dict.insert(parents, neighbour, word)
                case neighbour == target {
                  True -> list.Stop(#(next, parents, True))
                  False -> list.Continue(#([neighbour, ..next], parents, False))
                }
              }
            }
          },
        )
      case visit {
        #(_, parents, True) -> Some(parents)
        #(next, parents, False) -> search(graph, rest, next, parents, target)
      }
    }
  }
}

fn ladder(
  graph: Graph,
  parents: Dict(Int, Int),
  word: Int,
  acc: List(String),
) -> List(String) {
  let acc = [runtime.array_get(graph.words, word), ..acc]
  case dict.get(parents, word) {
    Ok(parent) if parent != word -> ladder(graph, parents, parent, acc)
    _ -> acc
  }
}
