import XCTest
@testable import WordChains

final class WordEditingTests: XCTestCase {
    private func game() -> GameState {
        let game = GameState()
        // CAB is a dead end reachable only through CAT; DOG has two routes.
        let words = ["cat", "cot", "cog", "dog", "dat", "dot", "cab"]
        let edges = words.map { word in
            words.indices.filter { GameState.isOneLetterAway(word, words[$0]) }
        }
        game.graph = WordGraph(nodes: words, edges: edges)
        game.difficulty = .easy
        game.currentPuzzle = Puzzle(start: "cat", target: "dog", optimalLength: 4, difficulty: .easy)
        game.chain = ["cat"]
        return game
    }

    // MARK: - Hints

    func testHintFindsAnUnusedRouteInsteadOfReturningARejectedWord() {
        let game = game()
        game.chain = ["cat", "cot", "dot", "dat"]
        // Both neighbors of DAT have already been used. Returning CAT or DOT
        // would invite a move submitWord refuses.
        XCTAssertNil(game.hint())
        XCTAssertEqual(game.submitWord("xxx"), .notInDictionary)
        game.undo()
        XCTAssertEqual(game.hint(), "dog")
    }

    func testDeadEndHintSaysSoAndUndoClearsIt() {
        let game = game()
        game.chain = ["cat", "cot", "dot", "dat"]
        XCTAssertNil(game.requestHint())
        XCTAssertEqual(game.lastResult, .noRoute)
        XCTAssertEqual(game.feedbackMessage, "No unused route. Undo to try another path.")
        game.undo()
        XCTAssertEqual(game.feedbackMessage, "")
        XCTAssertEqual(game.requestHint(), "dog")
        XCTAssertNil(game.lastResult, "A found hint leaves no message behind")
    }

    func testShortestPathCanAvoidUsedWordsWithoutChangingOrdinarySearch() {
        let graph = game().graph!
        XCTAssertEqual(graph.shortestPath(from: "cat", to: "dog"), ["cat", "cot", "cog", "dog"])
        XCTAssertEqual(graph.shortestPath(from: "cat", to: "dog", avoiding: ["cot"]), ["cat", "dat", "dot", "dog"])
        XCTAssertNil(graph.shortestPath(from: "cat", to: "dog", avoiding: ["cot", "dat"]))
    }

    // MARK: - Undo and scoring

    func testUndoIsFreeAndOnlyTheFinalChainDeterminesScoreInEverySoloMode() {
        for mode in GameMode.allCases {
            let game = game()
            game.currentMode = mode
            if mode == .timeAttack { game.timeAttack = TimeAttack(startingAt: .now, difficulty: game.difficulty) }
            XCTAssertEqual(game.submitWord("dat"), .valid)
            XCTAssertEqual(game.submitWord("dot"), .valid)
            game.undo()
            game.undo()
            XCTAssertEqual(game.chain, ["cat"])
            XCTAssertEqual(game.submitWord("cot"), .valid)
            XCTAssertEqual(game.submitWord("cog"), .valid)
            XCTAssertEqual(game.submitWord("dog"), .reachedTarget)
            XCTAssertEqual(game.chain, ["cat", "cot", "cog", "dog"])
            XCTAssertEqual(game.score, 150, "Undo must remain free in \(mode.rawValue)")
            XCTAssertEqual(game.totalScore, 150)
            XCTAssertEqual(game.optimalSolves, 1)
            XCTAssertEqual(game.timeAttack?.score, mode == .timeAttack ? 150 : nil)
        }
    }

    func testUndoRestoresPreviousWordAndClearsFeedbackWithoutChangingStats() {
        let game = game()
        XCTAssertFalse(game.canUndo)
        game.undo()
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(game.submitWord("cot"), .valid)
        XCTAssertEqual(game.submitWord("xxx"), .notInDictionary)
        XCTAssertTrue(game.canUndo)
        game.undo()
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(game.stepsTaken, 0)
        XCTAssertNil(game.lastResult)
        XCTAssertFalse(game.showInvalidShake)
        XCTAssertEqual(game.totalScore, 0)
        XCTAssertEqual(game.puzzlesSolved, 0)
        XCTAssertEqual(game.submitWord("cot"), .valid, "An undone word can be played again")
    }

    // MARK: - Completion and validation

    func testCompletedPuzzleCannotBeUndoneOrScoredTwice() {
        let game = game()
        XCTAssertEqual(game.submitWord("cot"), .valid)
        XCTAssertEqual(game.submitWord("cog"), .valid)
        XCTAssertEqual(game.submitWord("dog"), .reachedTarget)
        XCTAssertEqual(game.totalScore, 150)
        XCTAssertFalse(game.canUndo)
        game.undo()
        XCTAssertEqual(game.chain, ["cat", "cot", "cog", "dog"])
        XCTAssertNil(game.hint())
        XCTAssertEqual(game.submitWord("dot"), .gameComplete)
        XCTAssertEqual(game.submitWord("dog"), .gameComplete)
        XCTAssertEqual(game.lastResult, .reachedTarget, "Late input must preserve the winning result")
        XCTAssertEqual(game.chain, ["cat", "cot", "cog", "dog"])
        XCTAssertEqual(game.puzzlesSolved, 1)
        XCTAssertEqual(game.totalScore, 150)
    }

    func testUsedWordStillReportsAlreadyUsedDuringAnActivePuzzle() {
        let game = game()
        XCTAssertEqual(game.submitWord("cot"), .valid)
        XCTAssertEqual(game.submitWord("cat"), .alreadyUsed)
        XCTAssertEqual(game.feedbackMessage, "Already in the chain")
        XCTAssertEqual(game.chain, ["cat", "cot"])
        XCTAssertEqual(game.score, 0)
    }
}
