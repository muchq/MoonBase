import Foundation

struct DailyChallenge: Codable, Hashable {
    let day: String
    let difficulty: Difficulty

    init(date: Date, difficulty: Difficulty, calendar: Calendar) {
        day = Date.ISO8601FormatStyle(timeZone: calendar.timeZone).year().month().day().format(date)
        self.difficulty = difficulty
    }

    var storageKey: String { "\(day).\(difficulty.letterCount)" }
}

struct PuzzleResult: Codable, Hashable {
    let chain: [String]
    let optimalSteps: Int
    let score: Int
    let dailyChallenge: DailyChallenge?

    var stepsTaken: Int { max(0, chain.count - 1) }
    var isOptimal: Bool { stepsTaken == optimalSteps }

    var shareText: String? {
        guard let dailyChallenge else { return nil }
        let moves = stepsTaken == 1 ? "move" : "moves"
        let tiles = (0..<stepsTaken).map { step in
            String(repeating: step == stepsTaken - 1 ? "🟩" : "⬜", count: dailyChallenge.difficulty.letterCount)
        }.joined(separator: "\n")
        return """
        Word Chains Daily Challenge · \(dailyChallenge.day) · \(dailyChallenge.difficulty.label)
        \(stepsTaken) \(moves) · \(score) points

        \(tiles)
        """
    }
}
