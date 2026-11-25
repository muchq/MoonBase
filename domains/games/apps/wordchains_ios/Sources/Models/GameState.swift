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
}

enum WordHint: Equatable {
    case letter(Int)
    case word(String)
    case unavailable
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

    // Time attack
    var timeRemaining: Double = 60
    var timeAttackActive = false
    var timeAttackScore = 0

    // Animation triggers
    var lastResult: InputResult?
    private(set) var currentHint: WordHint?
    var showInvalidShake = false
    var showCompletionCelebration = false

    // Stats
    var gamesPlayed = 0
    var optimalSolves = 0

    private var generator: PuzzleGenerator?

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

    func startNewGame(mode: GameMode, difficulty: Difficulty) {
        self.currentMode = mode
        self.difficulty = difficulty
        self.isComplete = false
        self.score = 0
        self.showCompletionCelebration = false
        self.lastResult = nil
        self.currentHint = nil

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
            timeRemaining = 60
            timeAttackActive = true
            timeAttackScore = 0
        }
    }

    func submitWord(_ word: String) -> InputResult {
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
        currentHint = nil

        if normalized == puzzle.target {
            completeRound()
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

    @discardableResult
    func requestHint() -> WordHint {
        lastResult = nil
        showInvalidShake = false
        guard let next = hint(), let last = chain.last,
              let index = zip(last, next).enumerated().first(where: { $0.element.0 != $0.element.1 })?.offset else {
            currentHint = .unavailable
            return .unavailable
        }
        let result: WordHint
        switch currentHint {
        case .letter, .word: result = .word(next)
        case .none, .unavailable: result = .letter(index)
        }
        currentHint = result
        return result
    }

    var canUndo: Bool { !isComplete && chain.count > 1 }

    func undo() {
        guard canUndo else { return }
        chain.removeLast()
        lastResult = nil
        showInvalidShake = false
        currentHint = nil
    }

    private func completeRound() {
        guard let puzzle = currentPuzzle else { return }

        isComplete = true
        showCompletionCelebration = true
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

        if currentMode == .timeAttack {
            timeAttackScore += score
        }
    }

    func nextTimeAttackPuzzle() {
        guard let generator, timeAttackActive else { return }
        let puzzle = generator.randomPuzzle(difficulty: difficulty)
        currentPuzzle = puzzle
        chain = [puzzle?.start ?? ""]
        isComplete = false
        showCompletionCelebration = false
        score = 0
        lastResult = nil
        currentHint = nil
    }

    func endTimeAttack() {
        timeAttackActive = false
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
        case .valid, .reachedTarget, .none: return ""
        }
    }
}
