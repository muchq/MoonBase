$version: "2.0"

namespace moonbase.chess_com

/// The portion of chess.com's public API consumed by the indexer.
service ChessCom {
    version: "2026-08-18"
    operations: [FetchPlayer, FetchArchive]
}

@readonly
@http(method: "GET", uri: "/pub/player/{username}", code: 200)
operation FetchPlayer {
    input := {
        @required
        @httpLabel
        username: String
    }

    output := {
        title: String
    }

    errors: [PlayerNotFound]
}

@readonly
@http(method: "GET", uri: "/pub/player/{username}/games/{year}/{month}", code: 200)
operation FetchArchive {
    input := {
        @required
        @httpLabel
        username: String

        @required
        @httpLabel
        year: String

        @required
        @httpLabel
        month: String
    }

    output := {
        @required
        games: PlayedGameList
    }

    errors: [ArchiveNotFound]
}

list PlayedGameList {
    member: PlayedGame
}

/// Fields consumed by the indexer. Unneeded chess.com metadata is deliberately
/// not modeled. Response members remain optional because one incomplete game
/// must not prevent the indexer from processing the rest of the month.
structure PlayedGame {
    url: String

    pgn: String

    @jsonName("end_time")
    @timestampFormat("epoch-seconds")
    endTime: Timestamp

    @jsonName("time_class")
    timeClass: String

    white: PlayerResult

    black: PlayerResult

    eco: String
}

structure PlayerResult {
    username: String

    rating: Integer

    result: String
}

@error("client")
@httpError(404)
structure PlayerNotFound {
    message: String
}

@error("client")
@httpError(404)
structure ArchiveNotFound {
    message: String
}
