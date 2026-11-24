import Foundation

// The race said the way the web table says it (muchq.github.io
// src/apps/wordchains/race.ts): a step is a word played after the start,
// the target among them.
enum RaceText {
    /// The hub's par counts both ends of a shortest ladder; in steps it is
    /// one fewer.
    static func parSteps(_ par: Int) -> Int { par - 1 }

    static func steps(_ count: Int) -> String { "\(count) \(count == 1 ? "step" : "steps")" }

    static func you(_ id: String, viewer: String) -> String { id == viewer ? "you" : id }

    /// "You won in 4 steps.", "alice and bob share the win in 5 steps.",
    /// "Time ran out: nobody finished."
    static func result(_ result: WordchainResult, runners: [WordchainRunner], viewer: String) -> String {
        let why: String
        switch result.ending {
        case .finished: why = ""
        case .timeout: why = "Time ran out: "
        case .leave: why = "Too few seats stayed: "
        }
        guard let first = result.winners.first else { return "\(why)nobody finished." }
        let how = runners.first { $0.playerId == first }.map { " in \(steps($0.words))" } ?? ""
        // Only "you" opens a sentence capitalized: a player's id is as they chose it.
        var names = result.winners.map { you($0, viewer: viewer) }
        if why.isEmpty, names[0] == "you" { names[0] = "You" }
        let opened = names.count == 1 ? names[0] : names.dropLast().joined(separator: ", ") + " and " + names.last!
        let verb = names.count > 1 ? "share the win" : "won"
        return "\(why)\(opened) \(verb)\(how)."
    }

    /// m:ss left, never below zero.
    static func clock(msLeft: Int64) -> String {
        let seconds = max(0, (msLeft + 999) / 1000)
        return String(format: "%d:%02d", seconds / 60, seconds % 60)
    }
}
