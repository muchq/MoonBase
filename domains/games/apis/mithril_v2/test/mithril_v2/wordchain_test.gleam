import gleam/bit_array
import gleam/crypto
import gleam/list
import gleam/option.{None, Some}
import gleam/string
import gleeunit/should
import mithril_v2/runtime
import mithril_v2/wordchain

// An EUnit test with its own time limit, for the tests that load the shipped
// files: EUnit's default is five seconds.
pub type Eunit {
  Timeout
}

// Index pairs as the .graph file stores them: two big-endian 64-bit words.
fn edges(pairs: List(#(Int, Int))) -> BitArray {
  list.fold(pairs, <<>>, fn(acc, pair) {
    <<acc:bits, { pair.0 }:size(64), { pair.1 }:size(64)>>
  })
}

pub fn dictionary_is_trimmed_lowercased_deduplicated_and_sorted_test() {
  wordchain.read_dictionary("Dog\n  cat \ndog\nBird\n")
  |> should.equal(["bird", "cat", "dog"])
}

pub fn dictionary_drops_words_of_nine_bytes_or_more_test() {
  wordchain.read_dictionary("eightchr\nninechars\ntencharsxx\n")
  |> should.equal(["eightchr"])
}

pub fn dictionary_keeps_a_blank_line_but_not_the_final_newline_test() {
  wordchain.read_dictionary("cat\n\ndog\n")
  |> should.equal(["", "cat", "dog"])
}

pub fn a_graph_naming_a_word_past_the_dictionary_is_refused_test() {
  wordchain.from_edges(["cat", "cot"], edges([#(0, 2)]))
  |> should.be_error
}

pub fn a_graph_with_a_partial_record_is_refused_test() {
  wordchain.from_edges(["cat", "cot"], <<0:size(64), 1:size(64), 0:size(8)>>)
  |> should.be_error
}

// cat-cot-cog-dog and cat-cot-dot-dog are both shortest; the edge order in the
// file decides which is answered, as it does in the Rust solver.
fn two_route_graph(pairs: List(#(Int, Int))) -> wordchain.Graph {
  // 0 cat, 1 cog, 2 cot, 3 dog, 4 dot
  let assert Ok(graph) =
    wordchain.from_edges(["cat", "cog", "cot", "dog", "dot"], edges(pairs))
  graph
}

pub fn the_first_edge_in_the_file_wins_a_tie_test() {
  two_route_graph([#(0, 2), #(2, 1), #(2, 4), #(1, 3), #(4, 3)])
  |> wordchain.shortest_path("cat", "dog")
  |> should.equal(Some(["cat", "cot", "cog", "dog"]))

  two_route_graph([#(0, 2), #(2, 4), #(2, 1), #(4, 3), #(1, 3)])
  |> wordchain.shortest_path("cat", "dog")
  |> should.equal(Some(["cat", "cot", "dot", "dog"]))
}

pub fn an_edge_is_walkable_in_both_directions_test() {
  two_route_graph([#(0, 2), #(2, 1), #(2, 4), #(1, 3), #(4, 3)])
  |> wordchain.shortest_path("dog", "cat")
  |> should.equal(Some(["dog", "cog", "cot", "cat"]))
}

pub fn a_word_to_itself_is_itself_even_off_the_dictionary_test() {
  two_route_graph([])
  |> wordchain.shortest_path("zzz", "zzz")
  |> should.equal(Some(["zzz"]))
}

pub fn words_of_different_lengths_have_no_path_test() {
  two_route_graph([#(0, 2)])
  |> wordchain.shortest_path("cat", "cots")
  |> should.equal(None)
}

pub fn a_word_off_the_dictionary_has_no_path_test() {
  two_route_graph([#(0, 2)])
  |> wordchain.shortest_path("cat", "cut")
  |> should.equal(None)
}

pub fn disconnected_words_have_no_path_test() {
  two_route_graph([#(0, 2)])
  |> wordchain.shortest_path("cat", "dog")
  |> should.equal(None)
}

pub fn lookups_are_case_sensitive_test() {
  two_route_graph([#(0, 2)])
  |> wordchain.shortest_path("Cat", "cot")
  |> should.equal(None)
}

// The .graph file is named for a digest of the word list it indexes: SHA-256
// over the sorted words, concatenated, in base64 with "/" and "=" as "_". A
// dictionary read differently from the Rust solver's — one more word, one
// fewer, a different order — shifts every index, and this is what notices.
pub fn the_shipped_graph_indexes_the_dictionary_as_read_test_() {
  #(Timeout, 60, fn() {
    let assert Ok(words) = wordchain.load_dictionary(wordchain.data_dir())
    let digest =
      crypto.hash(crypto.Sha256, bit_array.from_string(string.concat(words)))
      |> bit_array.base64_encode(True)
      |> string.replace("/", "_")
      |> string.replace("=", "_")
    should.equal(digest <> ".graph", wordchain.graph_file)
  })
}

// testdata/v1_ladders.tsv is 500 random same-length pairs, each with the
// ladder mithril v1 answered for it ("-" for none), captured from the Rust
// service serving these same files.
pub fn the_shipped_graph_answers_as_mithril_v1_does_test_() {
  #(Timeout, 60, fn() {
    let assert Ok(graph) = wordchain.load(wordchain.data_dir())
    let assert Ok(corpus) =
      runtime.read_text(
        runtime.runfiles_dir()
        <> "/domains/games/apis/mithril_v2/testdata/v1_ladders.tsv",
      )
    let rows =
      corpus
      |> string.trim_end
      |> string.split("\n")
      |> list.map(fn(row) {
        let assert [start, end, ladder] = string.split(row, "\t")
        let want = case ladder {
          "-" -> None
          _ -> Some(string.split(ladder, " "))
        }
        #(start, end, want)
      })
    should.equal(list.length(rows), 500)

    rows
    |> list.filter(fn(row) {
      wordchain.shortest_path(graph, row.0, row.1) != row.2
    })
    |> should.equal([])

    wordchain.shortest_path(graph, "cold", "warm")
    |> should.equal(Some(["cold", "cord", "card", "ward", "warm"]))
  })
}
