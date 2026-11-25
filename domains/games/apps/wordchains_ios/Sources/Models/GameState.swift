import Foundation
import SwiftUI

enum GameMode: String, CaseIterable, Identifiable {
    case quickPlay = "Quick Play"
    case dailyChallenge = "Daily Challenge"
    case timeAttack = "Time Attack"

    var id: String { rawValue }

    var description: String {
        switch self {
        case .quickPlay: "Random puzzle, your pace"
        case .dailyChallenge: "Same puzzle for everyone today"
        case .timeAttack: "Solve puzzles against the clock"
        }
    }

    var iconName: String {
        switch self {
        case .quickPlay: "shuffle"
        case .dailyChallenge: "calendar"
        case .timeAttack: "timer"
        }
    }
}

enum Difficulty: Int, CaseIterable, Identifiable {
    case easy = 3
    case medium = 4
    case hard = 5

    var id: Int { rawValue }

    var label: String {
        switch self {
        case .easy: "Easy"
        case .medium: "Medium"
        case .hard: "Hard"
        }
    }

    var letterCount: Int { rawValue }

    var color: Color {
        switch self {
        case .easy: .green
        case .medium: .orange
        case .hard: .red
        }
    }
}

struct Puzzle: Identifiable {
    let id = UUID()
    let start: String
    let target: String
    /// Words in the shortest chain, both ends included.
    let optimalLength: Int
    let difficulty: Difficulty

    /// Moves in the shortest chain.
    var optimalSteps: Int { optimalLength - 1 }
}

enum InputResult {
    case valid
    case notInDictionary
    case notOneLetterAway
    case alreadyUsed
    case wrongLength
    case reachedTarget
    case gameComplete
    case noRoute
    case timeUp
}

@Observable
final class GameState {
    var graph: WordGraph?
    var isLoading = true

    var currentMode: GameMode = .quickPlay
    var difficulty: Difficulty = .medium
    var currentPuzzle: Puzzle?
    var chain: [String] = []
    var isComplete = false

    /// Moves the player has made: the chain less its start word.
    var stepsTaken: Int { chain.count - 1 }
    /// Moves in the current puzzle's shortest chain; 0 with no puzzle.
    var optimalSteps: Int { currentPuzzle?.optimalSteps ?? 0 }
    var score = 0
    var totalScore = 0
    var streak = 0
    var bestStreak = 0
    var puzzlesSolved = 0

    // Time attack: the sprint in play, and its summary once time is up.
    var timeAttack: TimeAttack?
    var timeAttackSummary: TimeAttackSummary?
    /// The sprint's clock as the bar shows it; `tick` keeps it current.
    private(set) var timeAttackSeconds = TimeAttack.seconds(TimeAttack.length)
    let bests: TimeAttackBests

    // Animation triggers
    var lastResult: InputResult?
    var showInvalidShake = false

    // Stats
    var gamesPlayed = 0
    var optimalSolves = 0

    private(set) var generator: PuzzleGenerator?

    init(bests: TimeAttackBests = TimeAttackBests(), generator: PuzzleGenerator? = nil) {
        self.bests = bests
        self.generator = generator
    }

    func loadGraph() {
        isLoading = true
        DispatchQueue.global(qos: .userInitiated).async { [weak self] in
            let graph = WordGraph.loadBundled()
            let generator = graph.map { PuzzleGenerator.bundled(graph: $0) }
            DispatchQueue.main.async {
                self?.graph = graph
                self?.generator = generator
                self?.isLoading = false
            }
        }
    }

    func startNewGame(mode: GameMode, difficulty: Difficulty, now: ContinuousClock.Instant = .now) {
        self.currentMode = mode
        self.difficulty = difficulty
        self.isComplete = false
        self.score = 0
        self.lastResult = nil
        self.timeAttack = nil
        self.timeAttackSummary = nil

        guard let generator else { return }

        let puzzle: Puzzle?
        switch mode {
        case .dailyChallenge:
            puzzle = generator.dailyPuzzle(difficulty: difficulty)
        case .quickPlay, .timeAttack:
            puzzle = generator.randomPuzzle(difficulty: difficulty)
        }

        guard let puzzle else { return }

        self.currentPuzzle = puzzle
        self.chain = [puzzle.start]

        if mode == .timeAttack {
            timeAttack = TimeAttack(startingAt: now)
            timeAttackSeconds = TimeAttack.seconds(TimeAttack.length)
        }
    }

    func submitWord(_ word: String, now: ContinuousClock.Instant = .now) -> InputResult {
        if let sprint = timeAttack, sprint.isOver(at: now) {
            finishTimeAttack()
            return .timeUp
        }
        guard !isComplete else { return .gameComplete }
        let normalized = word.lowercased().trimmingCharacters(in: .whitespaces)
        guard let puzzle = currentPuzzle, let graph else { return .notInDictionary }
        guard let lastWord = chain.last else { return .notInDictionary }

        if normalized.count != puzzle.difficulty.letterCount {
            lastResult = .wrongLength
            showInvalidShake = true
            return .wrongLength
        }

        if chain.contains(normalized) {
            lastResult = .alreadyUsed
            showInvalidShake = true
            return .alreadyUsed
        }

        if !graph.contains(normalized) {
            lastResult = .notInDictionary
            showInvalidShake = true
            return .notInDictionary
        }

        if !Self.isOneLetterAway(lastWord, normalized) {
            lastResult = .notOneLetterAway
            showInvalidShake = true
            return .notOneLetterAway
        }

        chain.append(normalized)
        showInvalidShake = false

        if normalized == puzzle.target {
            completeRound(now: now)
            lastResult = .reachedTarget
            return .reachedTarget
        }

        lastResult = .valid
        return .valid
    }

    func hint() -> String? {
        guard !isComplete, let puzzle = currentPuzzle, let graph, let lastWord = chain.last else { return nil }
        guard let path = graph.shortestPath(from: lastWord, to: puzzle.target, avoiding: Set(chain.dropLast())) else { return nil }
        return path.count > 1 ? path[1] : nil
    }

    /// `hint()`, but a dead end leaves a message instead of nothing.
    func requestHint() -> String? {
        guard !isComplete else { return nil }
        let next = hint()
        lastResult = next == nil ? .noRoute : nil
        showInvalidShake = false
        return next
    }

    var canUndo: Bool { !isComplete && chain.count > 1 }

    func undo() {
        guard canUndo else { return }
        chain.removeLast()
        lastResult = nil
        showInvalidShake = false
    }

    private func completeRound(now: ContinuousClock.Instant) {
        guard let puzzle = currentPuzzle else { return }

        isComplete = true
        gamesPlayed += 1
        puzzlesSolved += 1

        // Undo is intentionally free in every solo mode: reward the final
        // chain, not discarded exploration. Daily and Time Attack use the
        // same rule; multiplayer scoring is owned by the hub.
        let wordsUsed = chain.count
        let optimal = puzzle.optimalLength
        let isOptimal = wordsUsed == optimal

        if isOptimal { optimalSolves += 1 }

        // Scoring: base 100, bonus for optimal, penalty for extra steps
        let baseScore = 100
        let optimalBonus = isOptimal ? 50 : 0
        let stepPenalty = max(0, (wordsUsed - optimal)) * 10
        score = max(10, baseScore + optimalBonus - stepPenalty)
        totalScore += score

        streak += 1
        bestStreak = max(bestStreak, streak)

        timeAttack?.solve(points: score, at: now)
    }

    // MARK: - Time Attack

    /// Moves the sprint along: ends it at the deadline, else brings on the
    /// next puzzle once a solve's celebration is over. The deadline is
    /// checked first, so it cancels an advance that falls due with it.
    func tick(now: ContinuousClock.Instant = .now) {
        guard let sprint = timeAttack, timeAttackSummary == nil else { return }
        if sprint.isOver(at: now) {
            finishTimeAttack()
            return
        }
        // Observation notifies on every set; redraw the bar once a second.
        let seconds = TimeAttack.seconds(sprint.remaining(at: now))
        if seconds != timeAttackSeconds { timeAttackSeconds = seconds }
        if let advanceAt = sprint.advanceAt, now >= advanceAt {
            nextTimeAttackPuzzle()
        }
    }

    private func nextTimeAttackPuzzle() {
        timeAttack?.advanced()
        guard let puzzle = generator?.randomPuzzle(difficulty: difficulty) else { return }
        currentPuzzle = puzzle
        chain = [puzzle.start]
        isComplete = false
        score = 0
        lastResult = nil
        showInvalidShake = false
    }

    private func finishTimeAttack() {
        guard let sprint = timeAttack, timeAttackSummary == nil else { return }
        timeAttackSeconds = 0
        isComplete = true
        lastResult = .timeUp
        timeAttackSummary = bests.record(solved: sprint.solved, score: sprint.score, for: difficulty)
    }

    static func isOneLetterAway(_ a: String, _ b: String) -> Bool {
        guard a.count == b.count else { return false }
        var diff = 0
        for (c1, c2) in zip(a, b) {
            if c1 != c2 { diff += 1 }
        }
        return diff == 1
    }

    var feedbackMessage: String {
        switch lastResult {
        case .notInDictionary: return "Not in dictionary"
        case .notOneLetterAway: return "Change exactly one letter"
        case .alreadyUsed: return "Already in the chain"
        case .wrongLength: return "Must be \(difficulty.letterCount) letters"
        case .gameComplete: return "Puzzle complete"
        case .noRoute: return "No unused route. Undo to try another path."
        case .timeUp: return "Time's up"
        case .valid, .reachedTarget, .none: return ""
        }
    }
}
