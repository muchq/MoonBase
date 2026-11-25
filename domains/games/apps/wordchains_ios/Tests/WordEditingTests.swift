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

    func testLetterEditingKeepsOtherLettersAndSwitchingStartsFresh() {
        var draft = WordDraft(original: "cat")
        XCTAssertFalse(draft.canSubmit)
        draft.select(1)
        draft.replace(with: "O")
        XCTAssertEqual(draft.word, "cot")
        XCTAssertTrue(draft.canSubmit)
        draft.replace(with: "OU") // A second key replaces the candidate letter.
        XCTAssertEqual(draft.word, "cut")
        draft.select(0)
        XCTAssertEqual(draft.word, "cat")
        draft.replace(with: "b")
        XCTAssertEqual(draft.word, "bat")
        draft.replace(with: "") // Backspace restores the original letter.
        XCTAssertEqual(draft.word, "cat")
        XCTAssertFalse(draft.canSubmit)
    }

    func testInvalidLetterInputDoesNotCorruptTheDraft() {
        var draft = WordDraft(original: "stone")
        draft.select(99)
        draft.replace(with: "x")
        XCTAssertEqual(draft.word, "stone")
        draft.select(0)
        for input in ["1", " ", "!", "🙂", "é"] {
            draft.replace(with: input)
            XCTAssertEqual(draft.word, "stone")
        }
        draft.replace(with: "s")
        XCTAssertFalse(draft.canSubmit)
    }

    func testHintsRevealPositionThenWordWithoutPlayingIt() {
        let game = game()
        XCTAssertEqual(game.requestHint(), .letter(1))
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(game.requestHint(), .word("cot"))
        XCTAssertEqual(game.requestHint(), .word("cot"))
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(game.score, 0)
        XCTAssertEqual(game.submitWord("cot"), .valid)
        XCTAssertNil(game.currentHint)
        XCTAssertEqual(game.requestHint(), .letter(2))
    }

    func testHintFindsAnUnusedRouteInsteadOfReturningARejectedWord() {
        let game = game()
        game.chain = ["cat", "cot", "dot", "dat"]
        // Both neighbors of DAT have already been used. Returning CAT or DOT
        // would invite a move submitWord refuses.
        XCTAssertNil(game.hint())
        XCTAssertEqual(game.submitWord("xxx"), .notInDictionary)
        XCTAssertEqual(game.requestHint(), .unavailable)
        XCTAssertEqual(game.feedbackMessage, "", "An old error must not hide the hint explanation")
        game.undo()
        XCTAssertNil(game.currentHint)
        XCTAssertEqual(game.hint(), "dog")
    }

    func testShortestPathCanAvoidUsedWordsWithoutChangingOrdinarySearch() {
        let graph = game().graph!
        XCTAssertEqual(graph.shortestPath(from: "cat", to: "dog"), ["cat", "cot", "cog", "dog"])
        XCTAssertEqual(graph.shortestPath(from: "cat", to: "dog", avoiding: ["cot"]), ["cat", "dat", "dot", "dog"])
        XCTAssertNil(graph.shortestPath(from: "cat", to: "dog", avoiding: ["cot", "dat"]))
    }

    func testUndoRestoresPreviousWordAndClearsFeedbackWithoutChangingStats() {
        let game = game()
        XCTAssertFalse(game.canUndo)
        game.undo()
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(game.submitWord("cot"), .valid)
        game.requestHint()
        XCTAssertEqual(game.submitWord("xxx"), .notInDictionary)
        XCTAssertTrue(game.canUndo)
        game.undo()
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(game.stepsTaken, 0)
        XCTAssertNil(game.lastResult)
        XCTAssertNil(game.currentHint)
        XCTAssertFalse(game.showInvalidShake)
        XCTAssertEqual(game.totalScore, 0)
        XCTAssertEqual(game.puzzlesSolved, 0)
        XCTAssertEqual(game.submitWord("cot"), .valid, "An undone word can be played again")
    }

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
        XCTAssertEqual(game.submitWord("dot"), .alreadyUsed)
        XCTAssertEqual(game.puzzlesSolved, 1)
        XCTAssertEqual(game.totalScore, 150)
    }
}
