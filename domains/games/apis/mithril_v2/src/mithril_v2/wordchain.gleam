//// Word-ladder search over mithril's dictionary and its precomputed graph.
////
//// Answers match the Rust solver (//domains/games/libs/wordchains) exactly:
//// the same dictionary reading yields the same word indices, the shipped
//// .graph file supplies the edges in the same order, and the breadth-first
//// search visits neighbours in that order, so ties between equally short
//// ladders break the same way.

import gleam/bit_array
import gleam/bool
import gleam/dict.{type Dict}
import gleam/int
import gleam/list
import gleam/option.{type Option, None, Some}
import gleam/result
import gleam/set
import gleam/string
import mithril_v2/runtime

/// Named for the digest of the word list it indexes; see the test that pins
/// the two together.
pub const graph_file = "j4YL7Lh1H2PAgHy_a625LegUoxk94ZOimjzzx0QKI+Y_.graph"

const dictionary_file = "words.txt"

/// The dictionary keeps words shorter than this many bytes.
const max_word_bytes = 9

pub opaque type Graph {
  Graph(
    index_of: Dict(String, Int),
    words: runtime.Array(String),
    neighbours: runtime.Array(List(Int)),
  )
}

/// Where the data files sit in the runfiles tree.
pub fn data_dir() -> String {
  runtime.runfiles_dir() <> "/domains/games/apis/mithril/data"
}

/// The dictionary as the Rust solver reads it: one word per line, trimmed and
/// lowercased, deduplicated, shorter than nine bytes, sorted bytewise.
pub fn read_dictionary(contents: String) -> List(String) {
  let lines = string.split(contents, "\n")
  // A final newline ends the last line rather than starting an empty one.
  let lines = case list.last(lines) {
    Ok("") -> list.take(lines, list.length(lines) - 1)
    _ -> lines
  }
  lines
  |> list.map(fn(line) { string.lowercase(string.trim(line)) })
  |> list.filter(fn(word) { string.byte_size(word) < max_word_bytes })
  |> set.from_list
  |> set.to_list
  |> list.sort(string.compare)
}

pub fn load_dictionary(dir: String) -> Result(List(String), String) {
  runtime.read_text(dir <> "/" <> dictionary_file)
  |> result.map(read_dictionary)
}

/// A graph from the dictionary and the .graph file's edges: pairs of word
/// indices, each a big-endian 64-bit integer.
pub fn from_edges(
  words: List(String),
  bytes: BitArray,
) -> Result(Graph, String) {
  let count = list.length(words)
  use adjacency <- result.try(read_edges(bytes, count, dict.new()))
  let neighbours =
    list.repeat(Nil, count)
    |> list.index_map(fn(_, i) {
      dict.get(adjacency, i) |> result.unwrap([]) |> list.reverse
    })
  Ok(Graph(
    index_of: dict.from_list(list.index_map(words, fn(word, i) { #(word, i) })),
    words: runtime.array_from_list(words),
    neighbours: runtime.array_from_list(neighbours),
  ))
}

// Neighbour lists are built newest-first and reversed once at the end.
fn read_edges(
  bytes: BitArray,
  count: Int,
  adjacency: Dict(Int, List(Int)),
) -> Result(Dict(Int, List(Int)), String) {
  case bytes {
    <<>> -> Ok(adjacency)
    <<i:size(64), j:size(64), rest:bits>> if i < count && j < count ->
      adjacency
      |> prepend(i, j)
      |> prepend(j, i)
      |> read_edges(rest, count, _)
    <<i:size(64), j:size(64), _:bits>> ->
      Error(
        "edge "
        <> int.to_string(i)
        <> "-"
        <> int.to_string(j)
        <> " names a word past the dictionary's "
        <> int.to_string(count),
      )
    _ ->
      Error(
        "graph ends in a partial edge of "
        <> int.to_string(bit_array.byte_size(bytes))
        <> " bytes",
      )
  }
}

fn prepend(adjacency: Dict(Int, List(Int)), from: Int, to: Int) {
  dict.upsert(adjacency, from, fn(existing) {
    [to, ..option.unwrap(existing, [])]
  })
}

pub fn load(dir: String) -> Result(Graph, String) {
  use words <- result.try(load_dictionary(dir))
  use bytes <- result.try(runtime.read_bits(dir <> "/" <> graph_file))
  from_edges(words, bytes)
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
