import XCTest
@testable import WordChains

// What the quick race's waiting screen shows, from the hub's view and the
// clock: looking (and for how long), expired, or counting down. The
// screen only draws it.

private let t0: Int64 = 1_700_000_000_000

private func runner(_ id: String) -> WordchainRunner {
    WordchainRunner(playerId: id, words: 0, finishedAtMs: nil, left: false, ladder: nil, wins: 0)
}

private func seeking(letters: Int? = nil, deadlineMs: Int64 = t0 + 90_000, expired: Bool = false) -> WordchainView {
    var view = WordchainView(gameId: "W1", phase: .waiting, start: nil, target: nil, par: nil,
                             startedAtMs: nil, deadlineMs: nil, runners: [runner("alice")], result: nil)
    view.seek = WordchainSeek(letters: letters, deadlineMs: deadlineMs, expired: expired)
    return view
}

private func matched(startedAtMs: Int64 = t0 + 3000) -> WordchainView {
    WordchainView(gameId: "W1", phase: .racing, start: "cold", target: "warm", par: 5,
                  startedAtMs: startedAtMs, deadlineMs: startedAtMs + 180_000,
                  runners: [runner("alice"), runner("bouncy-coral-quokka")], result: nil)
}

final class QuickRaceTests: XCTestCase {

    private func race(_ view: WordchainView) -> QuickRace { QuickRace(view: view, viewer: "alice") }

    // MARK: - Looking

    func testASeekLooksUntilItsDeadline() {
        XCTAssertEqual(race(seeking()).stage(nowMs: t0), .looking(msLeft: 90_000))
        XCTAssertEqual(race(seeking()).stage(nowMs: t0 + 17_500), .looking(msLeft: 72_500))
        XCTAssertEqual(race(seeking()).stage(nowMs: t0 + 90_000), .expired)
    }

    // The hub's word is final, whatever this clock says.
    func testTheHubSayingExpiredIsExpired() {
        XCTAssertEqual(race(seeking(expired: true)).stage(nowMs: t0), .expired)
    }

    // The ring drains over the hub's 90 s: full at the seek, empty at its end.
    func testTheRingDrainsWithTheDeadline() {
        XCTAssertEqual(race(seeking()).ringLeft(nowMs: t0), 1, accuracy: 1e-9)
        XCTAssertEqual(race(seeking()).ringLeft(nowMs: t0 + 45_000), 0.5, accuracy: 1e-9)
        XCTAssertEqual(race(seeking()).ringLeft(nowMs: t0 + 99_000), 0, accuracy: 1e-9)
        XCTAssertEqual(race(seeking(expired: true)).ringLeft(nowMs: t0), 0, accuracy: 1e-9)
    }

    func testTheStatusSaysHowLongIsLeftOnScreenAndAloud() {
        XCTAssertEqual(race(seeking()).status(nowMs: t0 + 17_500), "Looking for a player… 1:13")
        XCTAssertEqual(race(seeking()).spoken(nowMs: t0 + 17_500), "Looking for a player, 1 minute 13 seconds left")
        XCTAssertEqual(race(seeking()).spoken(nowMs: t0 + 89_500), "Looking for a player, 1 second left")
        XCTAssertEqual(race(seeking(expired: true)).status(nowMs: t0), "Nobody came. Keep looking?")
    }

    // MARK: - Matched

    func testAMatchCountsDownThreeTwoOneFromTheOpponent() {
        XCTAssertEqual(race(matched()).stage(nowMs: t0), .starting(opponent: "bouncy-coral-quokka", count: 3))
        XCTAssertEqual(race(matched()).stage(nowMs: t0 + 1001), .starting(opponent: "bouncy-coral-quokka", count: 2))
        XCTAssertEqual(race(matched()).stage(nowMs: t0 + 2999), .starting(opponent: "bouncy-coral-quokka", count: 1))
        XCTAssertEqual(race(matched()).stage(nowMs: t0 + 3000), .racing)
        XCTAssertEqual(race(matched()).spoken(nowMs: t0), "Playing bouncy-coral-quokka, starting")
    }

    // The waiting screen holds until the race's first word; then the table.
    func testTheWaitingScreenHoldsUntilTheStart() {
        XCTAssertTrue(race(seeking()).waits(nowMs: t0))
        XCTAssertTrue(race(seeking(expired: true)).waits(nowMs: t0))
        XCTAssertTrue(race(matched()).waits(nowMs: t0 + 2999))
        XCTAssertFalse(race(matched()).waits(nowMs: t0 + 3000))
        var over = matched()
        over = WordchainView(gameId: over.gameId, phase: .over, start: over.start, target: over.target, par: over.par,
                             startedAtMs: over.startedAtMs, deadlineMs: over.deadlineMs, runners: over.runners, result: nil)
        XCTAssertFalse(race(over).waits(nowMs: t0))
    }

    // The ladder's length follows the seek's letters, or 4 for any.
    func testTheLadderIsAsLongAsTheSeeksLetters() {
        XCTAssertEqual(race(seeking(letters: 5)).ladderLetters, 5)
        XCTAssertEqual(race(seeking()).ladderLetters, 4)
    }
}
