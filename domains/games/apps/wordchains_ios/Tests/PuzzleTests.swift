import XCTest
@testable import WordChains

final class PuzzleTests: XCTestCase {

    // newt → neat → peat → pent → pint: five words, four moves. The results
    // screen shows this beside the player's step count.
    func testOptimalStepsCountsMovesNotWords() {
        let puzzle = Puzzle(start: "newt", target: "pint", optimalLength: 5, difficulty: .medium)
        XCTAssertEqual(puzzle.optimalSteps, 4)
    }
}
