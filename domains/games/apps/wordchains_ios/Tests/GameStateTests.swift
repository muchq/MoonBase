import XCTest
@testable import WordChains

// The results screen's Steps and Optimal tiles, side by side, in one unit.
final class GameStateTests: XCTestCase {

    func testAStepOverOptimalReadsOneHigherThanOptimal() {
        let game = GameState()
        game.currentPuzzle = Puzzle(start: "newt", target: "pint", optimalLength: 5, difficulty: .medium)
        game.chain = ["newt", "neat", "peat", "pelt", "pent", "pint"]
        XCTAssertEqual(game.stepsTaken, 5)
        XCTAssertEqual(game.optimalSteps, 4)
    }

    func testAnOptimalChainReadsTheSameOnBothTiles() {
        let game = GameState()
        game.currentPuzzle = Puzzle(start: "newt", target: "pint", optimalLength: 5, difficulty: .medium)
        game.chain = ["newt", "neat", "peat", "pent", "pint"]
        XCTAssertEqual(game.stepsTaken, game.optimalSteps)
    }

    func testWithoutAPuzzleOptimalIsZero() {
        XCTAssertEqual(GameState().optimalSteps, 0)
    }
}
