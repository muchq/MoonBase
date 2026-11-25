import XCTest
@testable import WordChains

// The waiting screen's ladders: real ones, a rung at a time between puzzle
// words, each next one from the last one's end, so the tour never stops.

final class LadderTourTests: XCTestCase {

    // cat-cot-cog-dog-dig, with cot-cut and kex, a real but obscure word off
    // every ladder; puzzle words cat, dog, dig, cut.
    private let graph = WordGraph(
        nodes: ["cat", "cot", "cog", "dog", "dig", "cut", "kex", "card"],
        edges: [[1], [0, 2, 5], [1, 3], [2, 4], [3], [1], [], []]
    )
    private let puzzleWords: Set<String> = ["cat", "dog", "dig", "cut", "card"]

    private func tour(seed: UInt64 = 7) -> LadderTour {
        LadderTour(graph: graph, puzzleWords: puzzleWords, letters: 3, seed: seed)
    }

    private func oneLetterApart(_ a: String, _ b: String) -> Bool {
        a.count == b.count && zip(a, b).filter { $0 != $1 }.count == 1
    }

    func testEveryLadderClimbsOneLetterAtATimeBetweenPuzzleWordsOnGraphWords() throws {
        var tour = tour()
        var from = try XCTUnwrap(tour.next())
        for _ in 0..<20 {
            let ladder = try XCTUnwrap(tour.next())
            XCTAssertGreaterThanOrEqual(ladder.count, 3, "\(ladder)")
            XCTAssertTrue(puzzleWords.contains(ladder.first!), "\(ladder)")
            XCTAssertTrue(puzzleWords.contains(ladder.last!), "\(ladder)")
            XCTAssertTrue(ladder.allSatisfy { graph.contains($0) && $0.count == 3 }, "\(ladder)")
            XCTAssertTrue(zip(ladder, ladder.dropFirst()).allSatisfy { oneLetterApart($0, $1) }, "\(ladder)")
            // Each next ladder begins where the last ended.
            XCTAssertEqual(ladder.first, from.last, "\(from) then \(ladder)")
            from = ladder
        }
    }

    // A shortest ladder: no rung the climb could skip.
    func testALadderIsAShortestOne() throws {
        var tour = tour()
        for _ in 0..<10 {
            let ladder = try XCTUnwrap(tour.next())
            XCTAssertEqual(ladder, graph.shortestPath(from: ladder.first!, to: ladder.last!), "\(ladder)")
        }
    }

    func testTheSameSeedTakesTheSameTour() {
        var a = tour(seed: 3)
        var b = tour(seed: 3)
        XCTAssertEqual((0..<5).map { _ in a.next() }, (0..<5).map { _ in b.next() })
    }

    // Too few puzzle words of the length to climb between: no tour, and
    // the screen shows the ring alone.
    func testNoLaddersAtALengthWithoutTwoConnectedPuzzleWords() {
        var tour = LadderTour(graph: graph, puzzleWords: puzzleWords, letters: 4, seed: 1)
        XCTAssertNil(tour.next())
    }
}
