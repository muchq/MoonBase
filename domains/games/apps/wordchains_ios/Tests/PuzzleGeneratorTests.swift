import XCTest
@testable import WordChains

// The generator draws every puzzle's ends from the puzzle words, random and
// daily alike (#1738).
final class PuzzleGeneratorTests: XCTestCase {

    // A ring of 3-letter words, every pair joined within a few steps.
    private let graph = WordGraph(
        nodes: ["bat", "cat", "cot", "cog", "dog", "dot", "hot", "hat"],
        edges: [[1, 7], [0, 2], [1, 3, 5, 6], [2, 4], [3, 5], [2, 4, 6], [2, 5, 7], [0, 6]]
    )

    func testRandomPuzzlesStartAndEndOnPuzzleWords() {
        let ends: Set<String> = ["cat", "dog", "hat"]
        let generator = PuzzleGenerator(graph: graph, ends: ends)
        for _ in 0..<50 {
            guard let puzzle = generator.randomPuzzle(difficulty: .easy) else { return XCTFail("no puzzle") }
            XCTAssertTrue(ends.contains(puzzle.start), puzzle.start)
            XCTAssertTrue(ends.contains(puzzle.target), puzzle.target)
        }
    }

    // The app builds its generator this way: with the bundle's puzzle words,
    // never without them while the bundle has them.
    func testTheAppsGeneratorTakesTheBundlesPuzzleWords() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("generator-words-\(UUID())")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        try "cat\ndog\n".write(to: dir.appendingPathComponent(PuzzleWords.fileName), atomically: true, encoding: .utf8)
        let generator = PuzzleGenerator.bundled(graph: graph, in: try XCTUnwrap(Bundle(url: dir)))
        XCTAssertEqual(generator.ends, ["cat", "dog"])
    }

    func testTheDailyPuzzleStartsAndEndsOnPuzzleWords() {
        let ends: Set<String> = ["cat", "dog", "hat"]
        guard let puzzle = PuzzleGenerator(graph: graph, ends: ends).dailyPuzzle(difficulty: .easy) else {
            return XCTFail("no puzzle")
        }
        XCTAssertTrue(ends.contains(puzzle.start), puzzle.start)
        XCTAssertTrue(ends.contains(puzzle.target), puzzle.target)
    }
}
