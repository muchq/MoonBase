import XCTest
@testable import WordChains

// A Time Attack sprint on a clock the test holds: every call takes `now`.
final class TimeAttackTests: XCTestCase {
    private var defaults: UserDefaults!
    private var suite: String!
    private let t0 = ContinuousClock.now

    override func setUp() {
        suite = "time-attack-\(UUID())"
        defaults = UserDefaults(suiteName: suite)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suite)
    }

    private func at(_ seconds: Double) -> ContinuousClock.Instant { t0 + .milliseconds(Int(seconds * 1000)) }

    /// A sprint on CAT → DOG, with a generator to deal the next puzzles from.
    private func sprint() -> GameState {
        let words = ["cat", "cot", "cog", "dog", "dat", "dot", "cab"]
        let edges = words.map { word in
            words.indices.filter { GameState.isOneLetterAway(word, words[$0]) }
        }
        let graph = WordGraph(nodes: words, edges: edges)
        let game = GameState(bests: TimeAttackBests(defaults: defaults), generator: PuzzleGenerator(graph: graph))
        game.graph = graph
        game.startNewGame(mode: .timeAttack, difficulty: .easy, now: t0)
        deal(game)
        return game
    }

    private func deal(_ game: GameState) {
        game.currentPuzzle = Puzzle(start: "cat", target: "dog", optimalLength: 4, difficulty: .easy)
        game.chain = ["cat"]
    }

    private func solve(_ game: GameState, at seconds: Double) {
        XCTAssertEqual(game.submitWord("cot", now: at(seconds)), .valid)
        XCTAssertEqual(game.submitWord("cog", now: at(seconds)), .valid)
        XCTAssertEqual(game.submitWord("dog", now: at(seconds)), .reachedTarget)
    }

    // MARK: - The clock

    func testTheClockCountsDownFromSixtyAndReadsZeroOnlyAtTheDeadline() {
        let game = sprint()
        XCTAssertEqual(game.timeAttackSeconds, 60)
        game.tick(now: at(0.2))
        XCTAssertEqual(game.timeAttackSeconds, 60, "59.8s left reads as 60")
        game.tick(now: at(30))
        XCTAssertEqual(game.timeAttackSeconds, 30)
        game.tick(now: at(59.9))
        XCTAssertEqual(game.timeAttackSeconds, 1)
        XCTAssertNil(game.timeAttackSummary)
        game.tick(now: at(60))
        XCTAssertEqual(game.timeAttackSeconds, 0)
        XCTAssertNotNil(game.timeAttackSummary)
    }

    func testSuspensionDoesNotPauseTheSprint() {
        // Suspended, the app stops ticking; the time still counts.
        let game = sprint()
        game.tick(now: at(10))
        game.tick(now: at(40))
        XCTAssertEqual(game.timeAttackSeconds, 20)
        XCTAssertNil(game.timeAttackSummary)
    }

    func testReturningAfterTheDeadlineEndsTheSprintOnTheFirstMove() {
        // Back from the background, a word can beat the first tick.
        let game = sprint()
        XCTAssertEqual(game.submitWord("cot", now: at(10)), .valid)
        XCTAssertEqual(game.submitWord("cog", now: at(300)), .timeUp)
        XCTAssertEqual(game.chain, ["cat", "cot"])
        XCTAssertEqual(game.timeAttackSummary, TimeAttackSummary(solved: 0, score: 0, best: 0, isNewBest: false))
        XCTAssertEqual(game.feedbackMessage, "Time's up")
    }

    // MARK: - Moves at the deadline

    func testTimeoutDuringTypingRefusesTheWordAndEveryMoveAfter() {
        let game = sprint()
        XCTAssertEqual(game.submitWord("cot", now: at(20)), .valid)
        game.tick(now: at(60))
        XCTAssertEqual(game.submitWord("cog", now: at(60)), .timeUp)
        XCTAssertFalse(game.canUndo)
        game.undo()
        XCTAssertNil(game.requestHint())
        XCTAssertEqual(game.chain, ["cat", "cot"])
    }

    func testASolveJustBeforeTheDeadlineCountsAndOneAtItDoesNot() {
        let early = sprint()
        XCTAssertEqual(early.submitWord("cot", now: at(50)), .valid)
        XCTAssertEqual(early.submitWord("cog", now: at(50)), .valid)
        XCTAssertEqual(early.submitWord("dog", now: at(59.999)), .reachedTarget)
        early.tick(now: at(60))
        XCTAssertEqual(early.timeAttackSummary, TimeAttackSummary(solved: 1, score: 150, best: 150, isNewBest: true))

        let late = sprint()
        XCTAssertEqual(late.submitWord("cot", now: at(50)), .valid)
        XCTAssertEqual(late.submitWord("cog", now: at(50)), .valid)
        XCTAssertEqual(late.submitWord("dog", now: at(60)), .timeUp)
        XCTAssertEqual(late.timeAttackSummary, TimeAttackSummary(solved: 0, score: 0, best: 150, isNewBest: false), "The early sprint set the best")
    }

    // MARK: - Advancing

    func testASolveCelebratesThenAdvancesKeepingTimeAndScore() {
        let game = sprint()
        solve(game, at: 10)
        let solved = game.currentPuzzle?.id
        XCTAssertTrue(game.isComplete)
        XCTAssertNil(game.requestHint())
        XCTAssertEqual(game.lastResult, .reachedTarget, "No dead-end message while celebrating")
        XCTAssertEqual(game.submitWord("cot", now: at(10.2)), .gameComplete)

        game.tick(now: at(10.5))
        XCTAssertEqual(game.currentPuzzle?.id, solved, "Still celebrating")

        game.tick(now: at(10.8))
        XCTAssertNotEqual(game.currentPuzzle?.id, solved)
        XCTAssertFalse(game.isComplete)
        XCTAssertEqual(game.chain.count, 1)
        XCTAssertEqual(game.chain.first, game.currentPuzzle?.start)
        XCTAssertEqual(game.score, 0)
        XCTAssertEqual(game.timeAttack?.solved, 1)
        XCTAssertEqual(game.timeAttack?.score, 150)
        XCTAssertEqual(game.timeAttackSeconds, 50)

        deal(game)
        solve(game, at: 20)
        XCTAssertEqual(game.timeAttack?.solved, 2)
        XCTAssertEqual(game.timeAttack?.score, 300)
    }

    func testTheDeadlineCancelsAPendingAdvance() {
        let game = sprint()
        solve(game, at: 59.5)
        let solved = game.currentPuzzle?.id
        game.tick(now: at(61))
        XCTAssertEqual(game.currentPuzzle?.id, solved)
        XCTAssertTrue(game.isComplete)
        XCTAssertEqual(game.timeAttackSummary, TimeAttackSummary(solved: 1, score: 150, best: 150, isNewBest: true))
    }

    func testWithNoPuzzleToDealTheSprintStillEndsAndCountsTheSolve() {
        let empty = PuzzleGenerator(graph: WordGraph(nodes: [], edges: []))
        let game = GameState(bests: TimeAttackBests(defaults: defaults), generator: empty)
        game.graph = sprint().graph
        game.currentMode = .timeAttack
        game.difficulty = .easy
        game.timeAttack = TimeAttack(startingAt: t0)
        deal(game)
        solve(game, at: 10)
        game.tick(now: at(11))
        XCTAssertTrue(game.isComplete)
        game.tick(now: at(60))
        XCTAssertEqual(game.timeAttackSummary, TimeAttackSummary(solved: 1, score: 150, best: 150, isNewBest: true))
    }

    // MARK: - The summary and the best

    func testTheSummaryIsRecordedOnceAndKeepsTheBest() {
        let game = sprint()
        solve(game, at: 10)
        game.tick(now: at(60))
        XCTAssertEqual(game.timeAttackSummary, TimeAttackSummary(solved: 1, score: 150, best: 150, isNewBest: true))
        game.tick(now: at(61))
        _ = game.submitWord("cot", now: at(62))
        XCTAssertEqual(
            game.timeAttackSummary, TimeAttackSummary(solved: 1, score: 150, best: 150, isNewBest: true),
            "Finishing again must not re-record"
        )
        XCTAssertEqual(TimeAttackBests(defaults: defaults).best(for: .easy), 150)

        // Beat your best: a fresh sprint, and a lower score keeps the best.
        game.startNewGame(mode: .timeAttack, difficulty: .easy, now: at(100))
        XCTAssertNil(game.timeAttackSummary)
        XCTAssertEqual(game.timeAttack?.solved, 0)
        XCTAssertEqual(game.timeAttack?.score, 0)
        XCTAssertEqual(game.timeAttackSeconds, 60)
        game.tick(now: at(160))
        XCTAssertEqual(game.timeAttackSummary, TimeAttackSummary(solved: 0, score: 0, best: 150, isNewBest: false))
    }

    func testBestsAreKeptPerDifficulty() {
        let bests = TimeAttackBests(defaults: defaults)
        XCTAssertTrue(bests.record(solved: 2, score: 300, for: .easy).isNewBest)
        XCTAssertEqual(bests.best(for: .hard), 0)
        XCTAssertTrue(bests.record(solved: 1, score: 100, for: .hard).isNewBest)
        XCTAssertFalse(bests.record(solved: 2, score: 300, for: .easy).isNewBest, "A tie is not a new best")
    }

    func testBeatingAStandingBestReplacesIt() {
        let bests = TimeAttackBests(defaults: defaults)
        _ = bests.record(solved: 1, score: 150, for: .medium)
        XCTAssertEqual(
            bests.record(solved: 2, score: 300, for: .medium),
            TimeAttackSummary(solved: 2, score: 300, best: 300, isNewBest: true)
        )
        XCTAssertEqual(bests.best(for: .medium), 300)
    }

    // MARK: - Leaving and restarting

    func testRestartingDropsTheOldSprintsPendingAdvance() {
        let game = sprint()
        solve(game, at: 10)
        game.startNewGame(mode: .timeAttack, difficulty: .easy, now: at(10.1))
        deal(game)
        let fresh = game.currentPuzzle?.id
        XCTAssertEqual(game.submitWord("cot", now: at(10.5)), .valid)
        game.tick(now: at(11))
        XCTAssertEqual(game.currentPuzzle?.id, fresh)
        XCTAssertEqual(game.chain, ["cat", "cot"])
        XCTAssertEqual(game.timeAttack?.solved, 0)
        XCTAssertEqual(game.timeAttack?.score, 0)
        XCTAssertEqual(game.timeAttackSeconds, 60, "59.1s left on the new clock")
    }

    func testOtherModesHaveNoClock() {
        let game = sprint()
        game.startNewGame(mode: .quickPlay, difficulty: .easy, now: at(0))
        deal(game)
        game.tick(now: at(500))
        solve(game, at: 500)
        XCTAssertNil(game.timeAttack)
        XCTAssertNil(game.timeAttackSummary)
        XCTAssertEqual(game.score, 150)
    }
}
