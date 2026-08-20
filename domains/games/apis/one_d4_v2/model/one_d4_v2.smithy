$version: "2.0"

namespace moonbase.one_d4

/// The one_d4 v2 API (#1389 phase 6): the seed that eventually replaces the
/// Java service. Analyze runs the same fifteen detectors the C++ indexer
/// runs, so an ad-hoc analysis and the indexed corpus cannot disagree about
/// what a motif is.
///
/// The output is field-for-field /v1/analyze's wire shape — motifs
/// lowercased, occurrences keyed by motif name, the attack primitive never
/// a key — so a caller moving to v2 changes a URL, not its parsing. What
/// changes is the answer: v2's detector set is a superset of Java's ten.
///
/// Bounds live in the handler, not here: the byte cap is about what was
/// read off the wire and the ply cap is about work, and both reject with
/// InvalidPgnError rather than a constraint trait so the message can say
/// which limit fired and by how much.
service OneD4V2 {
    version: "2026-08-20"
    operations: [Analyze]
}

/// Finds the motifs in one PGN nobody indexed.
@http(method: "POST", uri: "/1d4/v2/analyze", code: 200)
operation Analyze {
    input := {
        @required
        pgn: String
    }

    output := {
        /// The last move number, as indexed rows count it.
        @required
        numMoves: Integer

        /// Exactly the key set of occurrences, carried separately so a
        /// caller that only wants "what happened" does not walk the lists.
        @required
        motifs: MotifNames

        @required
        occurrences: OccurrenceMap
    }

    errors: [InvalidPgnError]
}

list MotifNames {
    member: String
}

map OccurrenceMap {
    key: String
    value: OccurrenceList
}

list OccurrenceList {
    member: AnalyzedOccurrence
}

/// One motif occurrence. ply distinguishes two occurrences inside the same
/// move number; the motif is the map key this arrives under.
structure AnalyzedOccurrence {
    @required
    ply: Integer

    @required
    moveNumber: Integer

    @required
    side: String

    @required
    description: String

    movedPiece: String

    attacker: String

    target: String

    @required
    isDiscovered: Boolean

    @required
    isMate: Boolean

    pinType: String
}

/// The PGN is missing, oversized, unparseable, or longer than any chess
/// game — the caller's input, and the caller's problem.
@error("client")
@httpError(400)
structure InvalidPgnError {
    @required
    message: String
}
