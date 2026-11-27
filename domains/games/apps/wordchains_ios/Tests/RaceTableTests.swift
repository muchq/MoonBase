import XCTest
@testable import WordChains

// What the race table screen decides, from the hub's view and who is
// looking: the screen only draws it.

private func runner(_ id: String, words: Int = 0, ladder: [String]? = nil, finished: Bool = false, left: Bool = false) -> WordchainRunner {
    WordchainRunner(playerId: id, words: words, finishedAtMs: finished ? 9 : nil, left: left, ladder: ladder, wins: 0)
}

private func view(_ phase: WordchainPhase, start: String? = "cold", startedAtMs: Int64 = 0, practice: Bool = false,
                  runners: [WordchainRunner], result: WordchainResult? = nil) -> WordchainView {
    var view = WordchainView(
        gameId: "W1", phase: phase, start: start, target: start == nil ? nil : "warm", par: start == nil ? nil : 5,
        startedAtMs: start == nil ? nil : startedAtMs, deadlineMs: start == nil ? nil : startedAtMs + 180_000,
        runners: runners, result: result
    )
    view.practice = practice
    return view
}

private let alone = [runner("alice")]
private let pair = [runner("alice", words: 1, ladder: ["cord"]), runner("bob", words: 3)]

final class RaceTableTests: XCTestCase {

    private func table(_ view: WordchainView, viewer: String = "alice") -> RaceTable { RaceTable(view: view, viewer: viewer) }

    // MARK: - Starting a race

    // Alone, a start is practice (#1730); with company, a race.
    func testTheStartButtonSaysWhatItStarts() {
        XCTAssertEqual(table(view(.waiting, start: nil, runners: alone)).startLabel, "Practise")
        XCTAssertEqual(table(view(.over, practice: true, runners: alone)).startLabel, "Practise again")
        XCTAssertEqual(table(view(.waiting, start: nil, runners: pair)).startLabel, "Start the race")
        XCTAssertEqual(table(view(.over, runners: pair)).startLabel, "Next race")
    }

    // A seat that left still has a runner; it does not make company.
    func testALeaverDoesNotCountAsASeat() {
        XCTAssertEqual(table(view(.over, runners: [runner("alice"), runner("bob", left: true)])).startLabel, "Practise again")
    }

    func testTheNextRaceOffersTheLastOnesLetters() {
        XCTAssertEqual(table(view(.over, start: "heats", runners: pair)).lastLetters, 5)
        XCTAssertNil(table(view(.waiting, start: nil, runners: pair)).lastLetters)
    }

    // MARK: - What the table says

    func testTheHintFollowsThePhaseAndWhetherItIsPractice() {
        XCTAssertEqual(table(view(.waiting, start: nil, runners: alone)).hint, "Practise alone, or wait for someone to join.")
        XCTAssertEqual(table(view(.waiting, start: nil, runners: pair)).hint, "2 seated: start when everyone’s here.")
        XCTAssertEqual(table(view(.racing, practice: true, runners: alone)).hint, "Practice: one letter at a time. Anyone who joins ends it.")
        XCTAssertEqual(table(view(.racing, runners: pair)).hint, "One letter at a time, every rung a word.")
        XCTAssertEqual(table(view(.racing, runners: [runner("alice", words: 4, ladder: ["a", "b", "c", "warm"], finished: true), runner("bob")])).hint,
                       "You reached warm in 4 steps. Waiting for the others.")
        XCTAssertEqual(table(view(.over, practice: true, runners: alone)).hint, "Practise again, or wait for someone to join.")
        XCTAssertEqual(table(view(.over, runners: pair)).hint, "Anyone can start the next race.")
        XCTAssertNil(table(view(.closed, runners: pair)).hint)
    }

    // The same result reads as a win in a race and against par in practice.
    func testAResultIsAWinInARaceAndAgainstParInPractice() {
        let done = [runner("alice", words: 4, finished: true)]
        let result = WordchainResult(ending: .finished, winners: ["alice"])
        XCTAssertEqual(table(view(.over, runners: done)).said(result), "You won in 4 steps.")
        XCTAssertEqual(table(view(.over, practice: true, runners: done)).said(result), "You finished in 4 steps: on par.")
    }

    func testTheTitleNamesPractice() {
        XCTAssertEqual(table(view(.racing, runners: pair)).title, "Word chains · W1")
        XCTAssertEqual(table(view(.racing, practice: true, runners: alone)).title, "Practice · W1")
    }

    func testWinsShowOnlyInARealRace() {
        XCTAssertTrue(table(view(.over, runners: pair)).showsWins)
        XCTAssertFalse(table(view(.over, practice: true, runners: alone)).showsWins)
    }

    // MARK: - Playing

    func testOnlyAnUnfinishedRunnerInARaceClimbs() {
        XCTAssertTrue(table(view(.racing, runners: pair)).climbing)
        XCTAssertFalse(table(view(.racing, runners: [runner("alice", finished: true), runner("bob")])).climbing)
        XCTAssertFalse(table(view(.over, runners: pair)).climbing)
        XCTAssertFalse(table(view(.racing, runners: pair), viewer: "watcher").climbing)
    }

    // The word typed stays through a refusal (the view is unchanged) and
    // clears when a rung lands or is taken back, or a new race starts.
    func testTheDraftClearsOnARungOrANewRaceAndNothingElse() {
        let before = table(view(.racing, runners: pair)).draftKey
        XCTAssertEqual(table(view(.racing, runners: pair)).draftKey, before)
        XCTAssertNotEqual(table(view(.racing, runners: [runner("alice", words: 2, ladder: ["cord", "card"]), runner("bob", words: 3)])).draftKey, before)
        XCTAssertNotEqual(table(view(.racing, runners: [runner("alice"), runner("bob", words: 3)])).draftKey, before)
        XCTAssertNotEqual(table(view(.racing, startedAtMs: 500_000, runners: pair)).draftKey, before)
        // Another runner's climb is not the viewer's.
        XCTAssertEqual(table(view(.racing, runners: [runner("alice", words: 1, ladder: ["cord"]), runner("bob", words: 4)])).draftKey, before)
    }

    func testOnlyTheViewersRungChangesPlayThumps() {
        let previous = view(.racing, runners: pair)
        let stepped = view(.racing, runners: [runner("alice", words: 2), runner("bob", words: 3)])
        let tookBack = view(.racing, runners: [runner("alice"), runner("bob", words: 3)])
        let opponentStepped = view(.racing, runners: [runner("alice", words: 1), runner("bob", words: 4)])
        let nextRace = view(.racing, startedAtMs: 500_000, runners: [runner("alice"), runner("bob")])

        XCTAssertEqual(table(stepped).sounds(since: previous), [.step(2)])
        XCTAssertEqual(table(tookBack).sounds(since: previous), [.takeback(steps: 0)])
        XCTAssertEqual(table(opponentStepped).sounds(since: previous), [])
        XCTAssertEqual(table(nextRace).sounds(since: previous), [])
    }

    func testFinishingPlaysTheTargetOnceAndThenTheResult() {
        let previous = view(.racing, runners: pair)
        let finished = [runner("alice", words: 2, finished: true), runner("bob", words: 3)]

        let win = view(.over, runners: finished, result: WordchainResult(ending: .finished, winners: ["alice"]))
        XCTAssertEqual(table(win).sounds(since: previous), [.target(2), .win])
        XCTAssertEqual(table(win).sounds(since: win), [])
        let shared = view(.over, runners: finished, result: WordchainResult(ending: .finished, winners: ["alice", "bob"]))
        XCTAssertEqual(table(shared).sounds(since: previous), [.target(2), .sharedWin])
        let timedOut = view(.over, runners: pair, result: WordchainResult(ending: .timeout, winners: []))
        XCTAssertEqual(table(timedOut).sounds(since: previous), [.timeout])
    }

    func testTimeoutPlaysTheWinCueForWinners() {
        let runners = [runner("alice", words: 3, finished: true), runner("bob", words: 3, finished: true)]
        let previous = view(.racing, runners: runners)
        let win = view(.over, runners: runners, result: WordchainResult(ending: .timeout, winners: ["alice"]))
        let shared = view(.over, runners: runners, result: WordchainResult(ending: .timeout, winners: ["alice", "bob"]))

        XCTAssertEqual(table(win).sounds(since: previous), [.win])
        XCTAssertEqual(table(win, viewer: "bob").sounds(since: previous), [.timeout])
        XCTAssertEqual(table(shared).sounds(since: previous), [.sharedWin])
        XCTAssertEqual(table(shared, viewer: "watcher").sounds(since: previous), [])
    }

    func testPracticeUsesTheTargetAndTimeoutCuesWithoutCompetitiveResults() {
        let previous = view(.racing, practice: true, runners: alone)
        let finished = view(.over, practice: true, runners: [runner("alice", words: 1, finished: true)],
                            result: WordchainResult(ending: .finished, winners: ["alice"]))
        let timedOut = view(.over, practice: true, runners: alone,
                            result: WordchainResult(ending: .timeout, winners: []))

        XCTAssertEqual(table(finished).sounds(since: previous), [.target(1)])
        XCTAssertEqual(table(timedOut).sounds(since: previous), [.timeout])
    }

    func testNoWordIsLongerThanThePuzzle() {
        let racing = table(view(.racing, runners: pair))
        XCTAssertEqual(racing.capped("cards"), "card")
        XCTAssertEqual(racing.capped("car"), "car")
        XCTAssertEqual(table(view(.waiting, start: nil, runners: pair)).capped("cards"), "cards")
    }

    // Leaving mid-race gives the race up, so it is asked twice; otherwise not.
    func testOnlyLeavingARaceInPlayIsAskedTwice() {
        XCTAssertTrue(table(view(.racing, runners: pair)).asksBeforeLeaving)
        XCTAssertTrue(table(view(.racing, practice: true, runners: alone)).asksBeforeLeaving)
        XCTAssertFalse(table(view(.over, runners: pair)).asksBeforeLeaving)
        XCTAssertFalse(table(view(.waiting, start: nil, runners: pair)).asksBeforeLeaving)
    }

    // MARK: - A shortest ladder

    // Offered once the race is over, never while it runs, and not when the
    // hub sent none.
    func testAShortestLadderIsOfferedOnlyOnceTheRaceIsOver() {
        let ladder = ["cold", "cord", "card", "ward", "warm"]
        var over = view(.over, runners: pair)
        over.solution = ladder
        XCTAssertEqual(table(over).solution, ladder)
        var racing = view(.racing, runners: pair)
        racing.solution = ladder
        XCTAssertNil(table(racing).solution)
        XCTAssertNil(table(view(.over, runners: pair)).solution)
        over.solution = []
        XCTAssertNil(table(over).solution)
    }

    // A ladder shown for one race is hidden when the next one starts.
    func testTheShownLadderBelongsToItsRace() {
        XCTAssertNotEqual(table(view(.over, runners: pair)).raceKey, table(view(.over, startedAtMs: 200_000, runners: pair)).raceKey)
        XCTAssertEqual(table(view(.over, runners: pair)).raceKey, table(view(.over, runners: [runner("alice", words: 2, ladder: ["cord", "card"])])).raceKey)
    }
}
