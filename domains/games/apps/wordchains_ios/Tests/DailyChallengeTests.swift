import XCTest
@testable import WordChains

final class DailyChallengeTests: XCTestCase {
    private var utc: Calendar {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0)!
        return calendar
    }

    private let today = Date(timeIntervalSince1970: 1_700_000_000)

    private func makeGame(defaults: UserDefaults) -> GameState {
        let words = ["cat", "cot", "cog", "dog", "cold", "cord", "card", "ward", "warm"]
        let edges = words.map { word in
            words.indices.filter { GameState.isOneLetterAway(word, words[$0]) }
        }
        let graph = WordGraph(nodes: words, edges: edges)
        let game = GameState(defaults: defaults, generator: PuzzleGenerator(graph: graph))
        game.graph = graph
        return game
    }

    private func solve(_ game: GameState) throws -> [String] {
        let puzzle = try XCTUnwrap(game.currentPuzzle)
        let path = try XCTUnwrap(game.graph?.shortestPath(from: puzzle.start, to: puzzle.target))
        for word in path.dropFirst().dropLast() {
            XCTAssertEqual(game.submitWord(word), .valid)
        }
        XCTAssertEqual(game.submitWord(puzzle.target), .reachedTarget)
        return path
    }

    func testCompletedDailyCannotBeReplayedForPointsAfterRelaunch() throws {
        let defaults = isolatedDefaults()
        let playing = makeGame(defaults: defaults)
        XCTAssertTrue(playing.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: today, calendar: utc))
        let chain = try solve(playing)
        XCTAssertFalse(playing.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: today, calendar: utc))

        let relaunched = makeGame(defaults: defaults)
        XCTAssertFalse(relaunched.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: today, calendar: utc))
        XCTAssertNil(relaunched.currentPuzzle)
        XCTAssertEqual(relaunched.puzzlesSolved, 1)
        XCTAssertEqual(relaunched.totalScore, 150)
        let saved = try XCTUnwrap(relaunched.dailyResult(difficulty: .easy, date: today, calendar: utc))
        XCTAssertEqual(saved.chain, chain)
        XCTAssertEqual(saved.stepsTaken, chain.count - 1)
        XCTAssertEqual(saved.optimalSteps, saved.stepsTaken)
        XCTAssertEqual(saved.score, 150)
        XCTAssertEqual(saved.shareText, playing.puzzleResult.shareText)
    }

    func testShareUsesTilesWithoutRevealingWordsOrChangedPositions() {
        let challenge = DailyChallenge(date: today, difficulty: .easy, calendar: utc)
        let first = PuzzleResult(chain: ["cat", "cot", "cog", "dog"], optimalSteps: 3, score: 150, dailyChallenge: challenge)
        let second = PuzzleResult(chain: ["bat", "cat", "cot", "cog"], optimalSteps: 3, score: 150, dailyChallenge: challenge)

        XCTAssertEqual(first.shareText, """
            Word Chains Daily Challenge · 2023-11-14 · Easy
            3 moves · 150 points

            ⬜⬜⬜
            ⬜⬜⬜
            🟩🟩🟩
            """)
        XCTAssertEqual(second.shareText, first.shareText)
    }

    func testCompletionIsSpecificToTheStartDateAndDifficulty() throws {
        let defaults = isolatedDefaults()
        let playing = makeGame(defaults: defaults)
        let tomorrow = today.addingTimeInterval(86_400)
        XCTAssertTrue(playing.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: today, calendar: utc))
        let firstChain = try solve(playing)
        XCTAssertNil(playing.dailyResult(difficulty: .medium, date: today, calendar: utc))
        XCTAssertNil(playing.dailyResult(difficulty: .easy, date: tomorrow, calendar: utc))

        XCTAssertTrue(playing.startNewGame(mode: .dailyChallenge, difficulty: .medium, date: today, calendar: utc))
        let secondChain = try solve(playing)
        XCTAssertTrue(playing.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: tomorrow, calendar: utc))
        let nextChain = try solve(playing)

        let relaunched = makeGame(defaults: defaults)
        XCTAssertEqual(relaunched.dailyResult(difficulty: .easy, date: today, calendar: utc)?.chain, firstChain)
        XCTAssertEqual(relaunched.dailyResult(difficulty: .medium, date: today, calendar: utc)?.chain, secondChain)
        XCTAssertEqual(relaunched.dailyResult(difficulty: .easy, date: tomorrow, calendar: utc)?.chain, nextChain)
        XCTAssertEqual(relaunched.puzzlesSolved, 3)
    }

    func testAnUnfinishedDailyCanStillBePlayedAfterRelaunch() {
        let defaults = isolatedDefaults()
        let playing = makeGame(defaults: defaults)
        XCTAssertTrue(playing.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: today, calendar: utc))
        XCTAssertEqual(playing.submitWord("xxx"), .notInDictionary)

        let relaunched = makeGame(defaults: defaults)
        XCTAssertNil(relaunched.dailyResult(difficulty: .easy, date: today, calendar: utc))
        XCTAssertTrue(relaunched.startNewGame(mode: .dailyChallenge, difficulty: .easy, date: today, calendar: utc))
        XCTAssertEqual(relaunched.totalScore, 0)
    }

    func testStorageDateUsesTheSameLocalDayAsTheDailyPuzzle() {
        var pacific = utc
        pacific.timeZone = TimeZone(identifier: "America/Los_Angeles")!
        let shortlyAfterMidnightUTC = Date(timeIntervalSince1970: 1_700_008_000)
        XCTAssertEqual(DailyChallenge(date: shortlyAfterMidnightUTC, difficulty: .medium, calendar: utc).day, "2023-11-15")
        XCTAssertEqual(DailyChallenge(date: shortlyAfterMidnightUTC, difficulty: .medium, calendar: pacific).day, "2023-11-14")
    }
}
