import Foundation

/// Ladders to watch while a quick race looks for a player: real shortest
/// ones between puzzle words, every rung a word the player could type, each
/// next one starting where the last ended, so the tour never stops.
struct LadderTour {
    private let graph: WordGraph
    private let ends: [String]
    private var rng: SplitMix64
    private var at: String?

    init(graph: WordGraph, puzzleWords: Set<String>?, letters: Int, seed: UInt64) {
        self.graph = graph
        self.ends = graph.puzzleEnds(ofLength: letters, among: puzzleWords)
        self.rng = SplitMix64(seed: seed)
    }

    /// The next ladder, at least three words; nil when none can be found.
    mutating func next() -> [String]? {
        guard ends.count >= 2 else { return nil }
        let from = at ?? pick()
        for _ in 0..<32 {
            let to = pick()
            guard to != from, let ladder = graph.shortestPath(from: from, to: to), ladder.count >= 3 else { continue }
            at = to
            return ladder
        }
        return nil
    }

    private mutating func pick() -> String { ends[Int(rng.next() % UInt64(ends.count))] }
}

/// A small seeded generator, so a tour can be replayed.
struct SplitMix64: RandomNumberGenerator {
    private var state: UInt64
    init(seed: UInt64) { state = seed }
    mutating func next() -> UInt64 {
        state &+= 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }
}
