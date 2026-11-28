import XCTest
@testable import WordChains

final class WordEditingTests: XCTestCase {
    private func game(defaults: UserDefaults? = nil) -> GameState {
        // CAB is a dead end reachable only through CAT; DOG has two routes.
        let words = ["cat", "cot", "cog", "dog", "dat", "dot", "cab"]
        let edges = words.map { word in
            words.indices.filter { GameState.isOneLetterAway(word, words[$0]) }
        }
        let graph = WordGraph(nodes: words, edges: edges)
        let game = GameState(defaults: defaults ?? isolatedDefaults(), generator: PuzzleGenerator(graph: graph))
        game.graph = graph
        game.difficulty = .easy
        deal(game)
        return game
    }

    private func deal(_ game: GameState) {
        game.currentPuzzle = Puzzle(start: "cat", target: "dog", optimalLength: 4, difficulty: .easy)
        game.chain = ["cat"]
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

    func testSoloStatsSurviveRelaunchAndAccumulateAcrossModes() {
        let defaults = isolatedDefaults()
        let quickPlay = game(defaults: defaults)
        XCTAssertEqual(quickPlay.submitWord("cot"), .valid)
        XCTAssertEqual(quickPlay.submitWord("cog"), .valid)
        XCTAssertEqual(quickPlay.submitWord("dog"), .reachedTarget)

        let daily = game(defaults: defaults)
        daily.currentMode = .dailyChallenge
        for word in ["dat", "dot", "cot", "cog"] {
            XCTAssertEqual(daily.submitWord(word), .valid)
        }
        XCTAssertEqual(daily.submitWord("dog"), .reachedTarget)
        XCTAssertEqual(daily.score, 80)

        let sprint = game(defaults: defaults)
        sprint.currentMode = .timeAttack
        let now = ContinuousClock.now
        sprint.timeAttack = TimeAttack(startingAt: now, difficulty: .easy)
        XCTAssertEqual(sprint.submitWord("cot", now: now), .valid)
        XCTAssertEqual(sprint.submitWord("cog", now: now), .valid)
        XCTAssertEqual(sprint.submitWord("dog", now: now), .reachedTarget)
        XCTAssertEqual(sprint.submitWord("dog", now: now), .gameComplete)
        sprint.tick(now: now + TimeAttack.length(for: .easy))

        let relaunched = GameState(defaults: defaults)
        XCTAssertEqual(relaunched.puzzlesSolved, 3)
        XCTAssertEqual(relaunched.optimalSolves, 2)
        XCTAssertEqual(relaunched.streak, 0)
        XCTAssertEqual(relaunched.bestStreak, 1)
        XCTAssertEqual(relaunched.totalScore, 380)
        XCTAssertEqual(relaunched.bests.best(for: .easy), 150)
        XCTAssertEqual(relaunched.score, 0)
        XCTAssertNil(relaunched.currentPuzzle)
    }

    func testBestStreakSurvivesRelaunchAndAShorterSession() {
        let defaults = isolatedDefaults()
        let playing = game(defaults: defaults)
        for _ in 0..<2 {
            playing.startNewGame(mode: .quickPlay, difficulty: .easy)
            deal(playing)
            XCTAssertEqual(playing.submitWord("cot"), .valid)
            XCTAssertEqual(playing.submitWord("cog"), .valid)
            XCTAssertEqual(playing.submitWord("dog"), .reachedTarget)
        }
        XCTAssertEqual(playing.streak, 2)

        let relaunched = game(defaults: defaults)
        XCTAssertEqual(relaunched.streak, 0)
        XCTAssertEqual(relaunched.bestStreak, 2)
        XCTAssertEqual(relaunched.submitWord("cot"), .valid)
        XCTAssertEqual(relaunched.submitWord("cog"), .valid)
        XCTAssertEqual(relaunched.submitWord("dog"), .reachedTarget)
        XCTAssertEqual(relaunched.streak, 1)
        let nextLaunch = GameState(defaults: defaults)
        XCTAssertEqual(nextLaunch.bestStreak, 2)
        XCTAssertEqual(nextLaunch.puzzlesSolved, 3)
    }

    func testStatsBlobMissingAFieldKeepsOtherCounters() throws {
        let defaults = isolatedDefaults()
        let data = try PropertyListSerialization.data(
            fromPropertyList: ["puzzlesSolved": 6, "streak": 3, "bestStreak": 3, "totalScore": 680],
            format: .binary, options: 0
        )
        defaults.set(data, forKey: "soloStats")
        let loaded = GameState(defaults: defaults)
        XCTAssertEqual(loaded.puzzlesSolved, 6)
        XCTAssertEqual(loaded.bestStreak, 3)
        XCTAssertEqual(loaded.totalScore, 680)
        XCTAssertEqual(loaded.optimalSolves, 0)
        XCTAssertEqual(loaded.streak, 0)
    }

    func testUnreadableStatsStartFreshAndTheNextSolveReplacesThem() {
        let defaults = isolatedDefaults()
        defaults.set(Data("invalid plist".utf8), forKey: "soloStats")
        let playing = game(defaults: defaults)
        XCTAssertEqual(playing.puzzlesSolved, 0)
        XCTAssertEqual(playing.totalScore, 0)
        XCTAssertEqual(playing.submitWord("cot"), .valid)
        XCTAssertEqual(playing.submitWord("cog"), .valid)
        XCTAssertEqual(playing.submitWord("dog"), .reachedTarget)
        XCTAssertEqual(GameState(defaults: defaults).totalScore, 150)
    }

    func testStartingANewPuzzleDoesNotCountTheAbandonedOne() {
        let defaults = isolatedDefaults()
        let unfinished = game(defaults: defaults)
        XCTAssertEqual(unfinished.submitWord("cot"), .valid)
        XCTAssertEqual(unfinished.submitWord("xxx"), .notInDictionary)
        let abandoned = unfinished.currentPuzzle?.id
        unfinished.startNewGame(mode: .quickPlay, difficulty: .easy)
        XCTAssertNotEqual(unfinished.currentPuzzle?.id, abandoned)
        XCTAssertEqual(unfinished.stepsTaken, 0)

        let relaunched = GameState(defaults: defaults)
        XCTAssertEqual(relaunched.puzzlesSolved, 0)
        XCTAssertEqual(relaunched.optimalSolves, 0)
        XCTAssertEqual(relaunched.streak, 0)
        XCTAssertEqual(relaunched.bestStreak, 0)
        XCTAssertEqual(relaunched.totalScore, 0)
    }

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
        let defaults = isolatedDefaults()
        let game = game(defaults: defaults)
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
        let relaunched = GameState(defaults: defaults)
        XCTAssertEqual(relaunched.puzzlesSolved, 1)
        XCTAssertEqual(relaunched.totalScore, 150)
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
