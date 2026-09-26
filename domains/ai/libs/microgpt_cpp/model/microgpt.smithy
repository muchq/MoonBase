$version: "2.0"

namespace moonbase.microgpt

/// The slice of microgpt-serve (domains/ai/apis/microgpt_serve) that
/// games_hub consumes: chat, for the room bot (#1591). The Rust types in
/// src/types.rs are the authority; `chat_wire_test` and the Rust
/// `chat_*_is_pinned_on_the_wire` tests hold the two spellings to the same
/// bytes.
service Microgpt {
    version: "2026-09-26"
    operations: [Chat]
}

/// The assistant's next turn in `messages`. A model trained without
/// `--chat` answers 400.
@http(method: "POST", uri: "/microgpt/v1/chat", code: 200)
operation Chat {
    input := {
        @required
        messages: Messages

        temperature: Double

        seed: Long

        @jsonName("max_tokens")
        maxTokens: Integer
    }

    output := {
        @required
        role: String

        @required
        content: String

        /// Tokens dropped from the oldest history to fit the context.
        @required
        @jsonName("tokens_dropped")
        tokensDropped: Integer
    }
}

list Messages {
    member: Message
}

structure Message {
    /// "user" or "assistant".
    @required
    role: String

    @required
    content: String
}
