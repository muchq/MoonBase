$version: "2.0"

namespace moonbase.lichess

/// The portion of Lichess's API consumed by the indexer.
///
/// Bearer auth because the games export answers anonymous callers with 404
/// even for an account that exists — verified against a live account whose
/// /api/user and /@/ pages both return 200, while OPTIONS on the export
/// route returns 204 and a malformed token returns 401 "No such token". The
/// token is optional in configuration: unset, no header is sent, which is
/// the correct request to make if Lichess ever reopens the endpoint.
@httpBearerAuth
service Lichess {
    version: "2026-09-13"
    operations: [ExportGames]
}

/// A player's games over a half-open millisecond range, as concatenated PGN.
///
/// PGN rather than NDJSON because the indexer already parses PGN headers —
/// ECO, Opening, and the WhiteTitle/BlackTitle that chess.com does not send
/// at all. NDJSON would give perfType and status structurally and then need
/// the PGN anyway for the stored `pgn` column.
@readonly
@http(method: "GET", uri: "/api/games/user/{username}", code: 200)
operation ExportGames {
    input := {
        @required
        @httpLabel
        username: String

        /// Epoch milliseconds, inclusive. Lichess counts in milliseconds
        /// where chess.com's end_time counts in seconds.
        @httpQuery("since")
        since: Long

        /// Epoch milliseconds, exclusive.
        @httpQuery("until")
        until: Long

        /// The format selector, and the reason PGN arrives rather than
        /// whatever the server would otherwise choose. The payload's own
        /// content type is only a default since opal-cpp #220, so a modeled
        /// value now survives to the wire.
        @httpHeader("Accept")
        accept: String
    }

    output := {
        @required
        @httpPayload
        games: Blob
    }

    errors: [GamesNotFound]
}

/// No members on purpose. With Accept: application/x-chess-pgn the 404 body
/// is a 13 KB HTML page, so there is nothing here a JSON deserializer could
/// read. Callers key on the error's presence, not on anything inside it.
@error("client")
@httpError(404)
structure GamesNotFound {}
