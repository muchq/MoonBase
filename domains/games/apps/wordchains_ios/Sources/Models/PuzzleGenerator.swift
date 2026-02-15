import Foundation

/// Generates puzzles by picking word pairs with known shortest paths from the word graph.
struct PuzzleGenerator {
    let graph: WordGraph

    /// Random puzzle for the given difficulty.
    func randomPuzzle(difficulty: Difficulty) -> Puzzle? {
        let candidates = graph.words(ofLength: difficulty.letterCount)
        guard candidates.count >= 2 else { return nil }

        // Try to find a pair with a path of reasonable length (3-7 steps)
        for _ in 0..<100 {
            let start = candidates.randomElement()!
            let target = candidates.randomElement()!
            if start == target { continue }

            if let path = graph.shortestPath(from: start, to: target) {
                let len = path.count
                if len >= 3 && len <= 8 {
                    return Puzzle(
                        start: start,
                        target: target,
                        optimalLength: len,
                        difficulty: difficulty
                    )
                }
            }
        }

        // Fallback: accept any connected pair
        for _ in 0..<50 {
            let start = candidates.randomElement()!
            let target = candidates.randomElement()!
            if start == target { continue }
            if let path = graph.shortestPath(from: start, to: target) {
                return Puzzle(
                    start: start,
                    target: target,
                    optimalLength: path.count,
                    difficulty: difficulty
                )
            }
        }

        return nil
    }

    /// Deterministic daily puzzle — same puzzle for everyone on the same day.
    func dailyPuzzle(difficulty: Difficulty) -> Puzzle? {
        let candidates = graph.words(ofLength: difficulty.letterCount).sorted()
        guard candidates.count >= 2 else { return nil }

        let calendar = Calendar.current
        let today = calendar.startOfDay(for: Date())
        let daysSinceEpoch = calendar.dateComponents([.day], from: Date(timeIntervalSince1970: 0), to: today).day ?? 0

        // Use day as seed for deterministic selection
        var seed = UInt64(bitPattern: Int64(daysSinceEpoch &* 2654435761 &+ difficulty.rawValue &* 7919))

        for _ in 0..<100 {
            seed = seed &* 6364136223846793005 &+ 1442695040888963407 // LCG
            let startIdx = Int(seed >> 33) % candidates.count
            seed = seed &* 6364136223846793005 &+ 1442695040888963407
            let targetIdx = Int(seed >> 33) % candidates.count

            if startIdx == targetIdx { continue }

            let start = candidates[startIdx]
            let target = candidates[targetIdx]

            if let path = graph.shortestPath(from: start, to: target), path.count >= 3, path.count <= 8 {
                return Puzzle(
                    start: start,
                    target: target,
                    optimalLength: path.count,
                    difficulty: difficulty
                )
            }
        }

        return randomPuzzle(difficulty: difficulty)
    }
}
