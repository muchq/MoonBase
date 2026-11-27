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
        let words = chain.map { $0.uppercased() }.joined(separator: " → ")
        let moves = stepsTaken == 1 ? "move" : "moves"
        return """
        Word Chains Daily Challenge · \(dailyChallenge.day) · \(dailyChallenge.difficulty.label)
        \(words)
        \(stepsTaken) \(moves) · \(score) points
        """
    }
}
