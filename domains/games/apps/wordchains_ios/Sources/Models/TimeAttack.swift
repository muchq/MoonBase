import Foundation

/// One Time Attack sprint. The deadline is on the continuous clock, which
/// keeps counting while the app is suspended and the phone sleeps: leaving
/// the app never buys time, and coming back past the deadline ends the sprint.
struct TimeAttack {
    /// Longer words take longer to change, so they get a longer sprint.
    static func length(for difficulty: Difficulty) -> Duration {
        switch difficulty {
        case .easy: .seconds(90)
        case .medium: .seconds(120)
        case .hard: .seconds(180)
        }
    }
    /// How long a solved puzzle stays on screen before the next one.
    static let celebration: Duration = .milliseconds(800)

    let deadline: ContinuousClock.Instant
    private(set) var solved = 0
    private(set) var score = 0
    /// When the solved puzzle gives way to the next; nil while one is in play.
    private(set) var advanceAt: ContinuousClock.Instant?

    init(startingAt now: ContinuousClock.Instant, difficulty: Difficulty) {
        deadline = now + Self.length(for: difficulty)
    }

    func isOver(at now: ContinuousClock.Instant) -> Bool { now >= deadline }

    func remaining(at now: ContinuousClock.Instant) -> Duration {
        max(.zero, now.duration(to: deadline))
    }

    /// Whole seconds as the clock reads them: 0 only once time is up.
    static func seconds(_ duration: Duration) -> Int {
        let (seconds, attoseconds) = duration.components
        return Int(seconds) + (attoseconds > 0 ? 1 : 0)
    }

    mutating func solve(points: Int, at now: ContinuousClock.Instant) {
        solved += 1
        score += points
        advanceAt = now + Self.celebration
    }

    mutating func advanced() { advanceAt = nil }
}

/// The end-of-sprint screen's numbers.
struct TimeAttackSummary: Equatable {
    let solved: Int
    let score: Int
    /// The best including this sprint.
    let best: Int
    let isNewBest: Bool
}

/// Personal bests, one per difficulty: points are not comparable across them.
struct TimeAttackBests {
    var defaults: UserDefaults = .standard

    private func key(_ difficulty: Difficulty) -> String { "timeAttackBest.\(difficulty.letterCount)" }

    func best(for difficulty: Difficulty) -> Int { defaults.integer(forKey: key(difficulty)) }

    /// Records a finished sprint and says how it compares.
    func record(solved: Int, score: Int, for difficulty: Difficulty) -> TimeAttackSummary {
        let previous = best(for: difficulty)
        let isNewBest = score > previous
        if isNewBest { defaults.set(score, forKey: key(difficulty)) }
        return TimeAttackSummary(solved: solved, score: score, best: max(previous, score), isNewBest: isNewBest)
    }
}
