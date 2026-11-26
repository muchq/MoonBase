import XCTest
@testable import WordChains

final class WordEntryTests: XCTestCase {
    private func game() -> GameState {
        let game = GameState()
        let words = ["cat", "cot", "cog", "dog", "dat", "dot"]
        let edges = words.map { word in
            words.indices.filter { GameState.isOneLetterAway(word, words[$0]) }
        }
        game.graph = WordGraph(nodes: words, edges: edges)
        game.difficulty = .easy
        game.currentPuzzle = Puzzle(start: "cat", target: "dog", optimalLength: 4, difficulty: .easy)
        game.chain = ["cat"]
        return game
    }

    func testReturnSubmitsAValidWordAndClearsTheText() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "cot", in: game)
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        XCTAssertEqual(game.chain, ["cat", "cot"])
        XCTAssertEqual(entry.text, "")
    }

    // The multi-line field can hand the same Return over twice: the second
    // arrives after the accepted word has cleared the text, and is not a move.
    func testARepeatedReturnForAnAcceptedWordIsIgnored() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "cot", in: game)
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        XCTAssertNil(entry.change(to: "cot\n", in: game))
        XCTAssertEqual(game.chain, ["cat", "cot"])
        XCTAssertEqual(entry.text, "")
        XCTAssertEqual(game.feedbackMessage, "")
    }

    func testARepeatedReturnForTheTargetWordIsIgnored() {
        let game = game()
        game.chain = ["cat", "cot", "cog"]
        var entry = WordEntry()
        XCTAssertEqual(entry.change(to: "dog\n", in: game), .reachedTarget)
        XCTAssertNil(entry.change(to: "dog\n", in: game))
        XCTAssertEqual(game.feedbackMessage, "")
    }

    func testReturnOnAHintedWordAfterUndoingItSubmitsIt() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "cot\n", in: game)
        entry.undo(in: game)
        entry.hint(in: game)
        XCTAssertEqual(entry.text, "cot")
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        XCTAssertEqual(game.chain, ["cat", "cot"])
    }

    func testAnEditAfterAcceptingForgetsTheAcceptedWord() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "cot\n", in: game)
        entry.change(to: "c", in: game)
        entry.change(to: "", in: game)
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .alreadyUsed, "A paste of the word, not a replay")
    }

    func testAnUnchangedTextBetweenRepeatedReturnsStillIgnoresTheRepeat() {
        let game = game()
        var entry = WordEntry()
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        XCTAssertNil(entry.change(to: "", in: game))
        XCTAssertNil(entry.change(to: "cot\n", in: game))
        XCTAssertEqual(game.chain, ["cat", "cot"])
    }

    func testAfterUndoTheUndoneWordCanBeReturnedAgain() {
        let game = game()
        var entry = WordEntry()
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        entry.undo(in: game)
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        XCTAssertEqual(game.chain, ["cat", "cot"])
    }

    // The field shows a Return's rewrite a turn later; a keystroke in that
    // turn arrives after the old newline, and is the start of the next word.
    func testAKeystrokeBeforeTheFieldClearsStartsTheNextWord() {
        let game = game()
        var entry = WordEntry()
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
        XCTAssertNil(entry.change(to: "cot\nc", in: game))
        XCTAssertEqual(entry.text, "c")
        XCTAssertEqual(game.chain, ["cat", "cot"])
        XCTAssertEqual(game.feedbackMessage, "")
    }

    func testAKeystrokeAfterARejectedReturnAppendsWithoutResubmitting() {
        let game = game()
        var entry = WordEntry()
        XCTAssertEqual(entry.change(to: "zzz\n", in: game), .notInDictionary)
        XCTAssertNil(entry.change(to: "zzz\na", in: game))
        XCTAssertEqual(entry.text, "zzza")
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertNil(game.lastResult, "Editing clears the error, as typing does")
    }

    func testRetypingAnAcceptedWordStillSaysItIsInTheChain() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "cot\n", in: game)
        entry.change(to: "cot", in: game)
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .alreadyUsed)
        XCTAssertEqual(game.feedbackMessage, "Already in the chain")
    }

    func testReturnOnARejectedWordKeepsTheTextAndTheError() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "zzz", in: game)
        XCTAssertEqual(entry.change(to: "zzz\n", in: game), .notInDictionary)
        XCTAssertEqual(entry.text, "zzz", "A rejected word stays for editing, without the newline")
        XCTAssertEqual(game.feedbackMessage, "Not in dictionary")
    }

    func testReturnOnAnEmptyFieldDoesNothing() {
        let game = game()
        var entry = WordEntry()
        XCTAssertNil(entry.change(to: "\n", in: game))
        XCTAssertEqual(entry.text, "")
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertNil(game.lastResult)
    }

    func testEditingAfterARejectionClearsTheStaleError() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "zzz\n", in: game)
        XCTAssertEqual(game.lastResult, .notInDictionary)
        entry.change(to: "zz", in: game)
        XCTAssertNil(game.lastResult)
        XCTAssertFalse(game.showInvalidShake)
        XCTAssertEqual(game.feedbackMessage, "")
    }

    func testHintFillsTheTextWithoutSubmitting() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "zzz\n", in: game)
        entry.hint(in: game)
        XCTAssertEqual(entry.text, "cot")
        XCTAssertEqual(game.chain, ["cat"], "The chain has not advanced")
        XCTAssertEqual(game.feedbackMessage, "", "The old error doesn't label the hinted word")
        XCTAssertEqual(entry.change(to: "cot\n", in: game), .valid)
    }

    func testHintAtADeadEndKeepsTheTextAndSaysToUndo() {
        let game = game()
        game.chain = ["cat", "cot", "dot", "dat"]
        var entry = WordEntry()
        entry.change(to: "da", in: game)
        entry.hint(in: game)
        XCTAssertEqual(entry.text, "da")
        XCTAssertEqual(game.lastResult, .noRoute)
    }

    func testUndoRemovesTheLastWordAndClearsTheText() {
        let game = game()
        var entry = WordEntry()
        entry.change(to: "cot\n", in: game)
        entry.change(to: "co", in: game)
        entry.undo(in: game)
        XCTAssertEqual(game.chain, ["cat"])
        XCTAssertEqual(entry.text, "")
    }
}
