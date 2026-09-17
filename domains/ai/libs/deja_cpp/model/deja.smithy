$version: "2.0"

namespace moonbase.deja

/// The slice of deja (domains/ai/apis/deja) that games_hub consumes: the
/// tape, polled (#1554, #1150). deja's own Rust types are the authority —
/// this is a second spelling of them, and `recent_wire_test` is what keeps
/// the two honest, since a rename on the Rust side has nothing else to
/// fail against until a glasshouse goes quiet in production.
///
/// Every member the hub reads is @required on purpose: a field deja drops
/// or renames fails the parse loudly in CI rather than arriving as a
/// default nobody notices. Members deja adds are ignored.
service Deja {
    version: "2026-09-17"
    operations: [FetchRecent]
}

/// The events after `after`, oldest first, capped by deja's ring (200).
@readonly
@http(method: "GET", uri: "/deja/v1/recent", code: 200)
operation FetchRecent {
    input := {
        /// The newest sequence number already seen; 0 asks for everything
        /// the ring holds.
        @required
        @httpQuery("after")
        after: Long
    }

    output := {
        @required
        events: DejaEvents
    }
}

list DejaEvents {
    member: DejaEvent
}

/// One scored request. `step`, `threshold` and `ewma_loss` are the
/// baseline the verdict was judged against, before this event was itself
/// learned.
structure DejaEvent {
    @required
    seq: Long

    /// Epoch seconds, fractional.
    @required
    ts: Double

    /// A slot number standing for one client; never an address.
    @required
    lane: Integer

    @required
    step: Long

    /// The lane's last tokens, oldest first.
    @required
    context: Tokens

    @required
    actual: String

    @required
    predictions: PredictedGuesses

    @required
    surprise: PerPredictor

    /// Absent (null on the wire) while the scorer is warming up.
    threshold: Double

    /// "warmup", "expected", "anomaly" or "novel".
    @required
    verdict: String

    @required
    @jsonName("ewma_loss")
    ewmaLoss: PerPredictor

    @required
    @jsonName("vocab_size")
    vocabSize: Integer
}

/// Each predictor's top guesses, best first. The bigram's list is empty
/// until it has seen the lane's previous token.
structure PredictedGuesses {
    @required
    bigram: Guesses

    @required
    net: Guesses
}

/// One value per predictor.
structure PerPredictor {
    @required
    bigram: Double

    @required
    net: Double
}

list Tokens {
    member: String
}

list Guesses {
    member: Guess
}

structure Guess {
    @required
    token: String

    @required
    p: Double
}
