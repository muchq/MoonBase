$version: "2.0"

namespace moonbase.goengine

/// go_engine (domains/games/apis/go_engine) as games_hub consumes it: GNU
/// Go's move, or its dead stones, for a go bot's seat (#1668). The Go types
/// in query.go and handler.go are the authority; `genmove_wire_test` and
/// the Go `TestTheWireIsPinned` hold the two spellings to the same bytes.
service GoEngine {
    version: "2026-10-20"
    operations: [Genmove]
}

/// The move for the side to move after `moves` (GTP vertices, Black's
/// first) on a `size` board at `komi`, at `level`; with `scoring`, the
/// stones the engine counts dead instead. 400 for a query out of bounds,
/// 422 for a game the engine refuses a move of, 503 when no engine
/// answered in time.
@http(method: "POST", uri: "/go_engine/v1/genmove", code: 200)
operation Genmove {
    input := {
        @required
        size: Integer

        @required
        komi: Double

        moves: Moves

        @required
        level: Integer

        scoring: Boolean
    }

    output := {
        /// A vertex, "pass" or "resign"; in the scoring, the dead stones'
        /// vertices, space separated.
        @required
        answer: String
    }
}

list Moves {
    member: String
}
