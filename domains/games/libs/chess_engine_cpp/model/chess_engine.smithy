$version: "2.0"

namespace moonbase.chessengine

/// chess_engine (domains/games/apis/chess_engine) as games_hub consumes it:
/// Stockfish's move for a chess bot's seat (#1618). The Go types in
/// query.go and handler.go are the authority; `best_move_wire_test` and the
/// Go `TestTheWireIsPinned` hold the two spellings to the same bytes.
service ChessEngine {
    version: "2026-10-01"
    operations: [BestMove]
}

/// The move from `fen` after `moves`, thought over for `movetimeMs` at
/// `elo` (absent: full strength). 400 for a query out of bounds, 422 for a
/// position with no move, 503 when no engine answered in time.
@http(method: "POST", uri: "/chess_engine/v1/bestmove", code: 200)
operation BestMove {
    input := {
        @required
        fen: String

        moves: Moves

        movetimeMs: Integer

        elo: Integer
    }

    output := {
        /// The move in UCI.
        @required
        uci: String
    }
}

list Moves {
    member: String
}
