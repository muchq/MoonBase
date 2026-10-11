$version: "2.0"

namespace moonbase.oned4

/// one_d4 (domains/games/apis/one_d4) as games_hub consumes it: an index
/// request for a player's month, so a chess game that ends in a published
/// room reaches 1d4 from the hub's public feed. The Java IndexRequest
/// record and IndexController are the authority; `index_wire_test` pins
/// the bytes the hub sends.
service OneD4 {
    version: "2026-10-11"
    operations: [CreateIndex]
}

/// Queues an index of `player` on `platform` for the months from
/// `startMonth` to `endMonth` ("2026-10"). A request already live for the
/// same range is answered with that one rather than a second.
@http(method: "POST", uri: "/v1/index", code: 200)
operation CreateIndex {
    input := {
        @required
        player: String

        @required
        platform: String

        @required
        startMonth: String

        @required
        endMonth: String
    }

    output := {
        id: String

        status: String
    }
}
