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

pub fn a_graph_with_an_invalid_neighbour_is_refused_test() {
  wordchain.from_json(<<
    "{\"nodes\":[\"cat\",\"cot\"],\"edges\":[[2],[0]]}":utf8,
  >>)
  |> should.be_error
}

pub fn a_graph_with_a_missing_adjacency_row_is_refused_test() {
  wordchain.from_json(<<"{\"nodes\":[\"cat\",\"cot\"],\"edges\":[[1]]}":utf8>>)
  |> should.be_error
}

pub fn malformed_json_is_refused_test() {
  wordchain.from_json(<<"{\"nodes\":[":utf8>>)
  |> should.be_error
}

// 0 cat, 1 cog, 2 cot, 3 dog, 4 dot.
fn fixture(adjacency: String) -> wordchain.Graph {
  let json =
    "{\"nodes\":[\"cat\",\"cog\",\"cot\",\"dog\",\"dot\"],\"edges\":"
    <> adjacency
    <> "}"
  let assert Ok(graph) = wordchain.from_json(<<json:utf8>>)
  graph
}

pub fn the_first_neighbour_in_the_graph_wins_a_tie_test() {
  fixture("[[2],[2,3],[0,1,4],[1,4],[2,3]]")
  |> wordchain.shortest_path("cat", "dog")
  |> should.equal(Some(["cat", "cot", "cog", "dog"]))

  fixture("[[2],[2,3],[0,4,1],[1,4],[2,3]]")
  |> wordchain.shortest_path("cat", "dog")
  |> should.equal(Some(["cat", "cot", "dot", "dog"]))
}

pub fn an_edge_is_walkable_in_both_directions_test() {
  fixture("[[2],[2,3],[0,1,4],[1,4],[2,3]]")
  |> wordchain.shortest_path("dog", "cat")
  |> should.equal(Some(["dog", "cog", "cot", "cat"]))
}

pub fn a_word_to_itself_is_itself_even_off_the_dictionary_test() {
  fixture("[[],[],[],[],[]]")
  |> wordchain.shortest_path("zzz", "zzz")
  |> should.equal(Some(["zzz"]))
}

pub fn words_of_different_lengths_have_no_path_test() {
  fixture("[[2],[],[0],[],[]]")
  |> wordchain.shortest_path("cat", "cots")
  |> should.equal(None)
}

pub fn a_word_off_the_dictionary_has_no_path_test() {
  fixture("[[2],[],[0],[],[]]")
  |> wordchain.shortest_path("cat", "cut")
  |> should.equal(None)
}

pub fn disconnected_words_have_no_path_test() {
  fixture("[[2],[],[0],[],[]]")
  |> wordchain.shortest_path("cat", "dog")
  |> should.equal(None)
}

pub fn lookups_are_case_sensitive_test() {
  fixture("[[2],[],[0],[],[]]")
  |> wordchain.shortest_path("Cat", "cot")
  |> should.equal(None)
}

// testdata/v1_ladders.tsv pins the Rust solver's answers ("-" for none)
// across 500 same-length pairs on the shared graph.
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
