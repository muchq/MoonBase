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
/// not modeled. A player side remains optional because chess.com can omit it,
/// in which case the indexer stores null player fields.
structure PlayedGame {
    @required
    url: String

    @required
    pgn: String

    @required
    @jsonName("end_time")
    @timestampFormat("epoch-seconds")
    endTime: Timestamp

    @required
    @jsonName("time_class")
    timeClass: String

    white: PlayerResult

    black: PlayerResult

    eco: String
}

structure PlayerResult {
    @required
    username: String

    @required
    rating: Integer

    @required
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
