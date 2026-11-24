import XCTest
@testable import WordChains

// The race said the same way as on the web table (muchq.github.io
// src/apps/wordchains/race.ts): a step is a word played after the start.
final class RaceTextTests: XCTestCase {

    private let runners = [
        WordchainRunner(playerId: "alice", words: 4, finishedAtMs: 1, left: false, ladder: nil, wins: 1),
        WordchainRunner(playerId: "bob", words: 4, finishedAtMs: 1, left: false, ladder: nil, wins: 1),
    ]

    // The hub's par counts both ends of a shortest ladder.
    func testParIsSaidInSteps() {
        XCTAssertEqual(RaceText.parSteps(5), 4)
        XCTAssertEqual(RaceText.steps(1), "1 step")
        XCTAssertEqual(RaceText.steps(4), "4 steps")
    }

    func testTheResultNamesTheWinnerASharedWinOrNobody() {
        func said(_ ending: WordchainEnding, _ winners: [String], to viewer: String) -> String {
            RaceText.result(WordchainResult(ending: ending, winners: winners, standings: []), runners: runners, viewer: viewer)
        }
        XCTAssertEqual(said(.finished, ["alice"], to: "alice"), "You won in 4 steps.")
        XCTAssertEqual(said(.finished, ["bob"], to: "alice"), "bob won in 4 steps.")
        XCTAssertEqual(said(.finished, ["alice", "bob"], to: "alice"), "You and bob share the win in 4 steps.")
        XCTAssertEqual(said(.timeout, ["alice", "bob"], to: "cat"), "Time ran out: alice and bob share the win in 4 steps.")
        XCTAssertEqual(said(.leave, [], to: "cat"), "Too few seats stayed: nobody finished.")
    }

    func testTheClockReadsMinutesAndSecondsAndStopsAtZero() {
        XCTAssertEqual(RaceText.clock(msLeft: 180_000), "3:00")
        XCTAssertEqual(RaceText.clock(msLeft: 61_001), "1:02")
        XCTAssertEqual(RaceText.clock(msLeft: 999), "0:01")
        XCTAssertEqual(RaceText.clock(msLeft: -5), "0:00")
    }
}
