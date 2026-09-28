$version: "2.0"

namespace moonbase.mithril

/// mithril (domains/games/apis/mithril) as games_hub consumes it: the
/// wordchain solver behind the /wordchain chat command. The Rust types in
/// src/main.rs are the authority; `wordchain_wire_test` and the Rust
/// `wordchain_*_is_pinned_on_the_wire` tests hold the two spellings to the
/// same bytes.
service Mithril {
    version: "2026-09-27"
    operations: [Wordchain]
}

/// The shortest ladder from `start` to `end`, one letter changed per rung.
/// Words are 3 to 9 letters; anything else answers 400.
@http(method: "POST", uri: "/mithril/v1/wordchain", code: 200)
operation Wordchain {
    input := {
        @required
        start: String

        @required
        end: String
    }

    output := {
        /// `start` through `end`; null when no ladder joins them.
        path: Words
    }
}

list Words {
    member: String
}
