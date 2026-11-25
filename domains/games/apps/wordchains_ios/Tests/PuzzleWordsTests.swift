import XCTest
@testable import WordChains

// Single-player puzzles start and end on SCOWL's common words, as races do
// (#1738); the graph, ENABLE, is every word a player may type on the way.
final class PuzzleWordsTests: XCTestCase {

    // cat-cot-cog-dog, and kex, a real but obscure ENABLE word.
    private let graph = WordGraph(
        nodes: ["cat", "cog", "cot", "dog", "kex", "card"],
        edges: [[2], [2, 3], [0, 1], [1], [], []]
    )

    // MARK: - Reading the bundled list

    func testTheListIsOneLowercaseWordPerLine() {
        XCTAssertEqual(PuzzleWords.parse("cat\nDog\n\n  cot \r\n"), ["cat", "dog", "cot"])
    }

    // The app looks the list up by this name, and BUILD bundles it by the
    // same one (app_store_test checks that side): a rename on either side
    // would ship the file and silently fall back to any graph word.
    func testTheListIsLookedUpAsCommonWordsTxt() {
        XCTAssertEqual(PuzzleWords.fileName, "common_words.txt")
    }

    func testTheListIsReadFromTheBundleItShipsIn() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("puzzle-words-\(UUID())")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        try "cat\ndog\n".write(to: dir.appendingPathComponent(PuzzleWords.fileName), atomically: true, encoding: .utf8)
        XCTAssertEqual(PuzzleWords.load(from: try XCTUnwrap(Bundle(url: dir))), ["cat", "dog"])
    }

    func testABundleWithoutTheListHasNone() throws {
        let dir = FileManager.default.temporaryDirectory.appendingPathComponent("no-puzzle-words-\(UUID())")
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: dir) }
        XCTAssertNil(PuzzleWords.load(from: try XCTUnwrap(Bundle(url: dir))))
    }

    // MARK: - Choosing puzzle ends

    func testPuzzleEndsAreGraphWordsOfTheLengthOnTheList() {
        XCTAssertEqual(graph.puzzleEnds(ofLength: 3, among: ["cat", "dog", "card", "zzz"]), ["cat", "dog"])
    }

    // A word that is accepted on the way is not, for that, a puzzle's end.
    func testAnAcceptedWordOffTheListIsNeverAnEnd() {
        XCTAssertEqual(graph.puzzleEnds(ofLength: 3, among: ["cat", "dog"]).contains("kex"), false)
        XCTAssertTrue(graph.contains("kex"))
    }

    // Without a list (a bundle missing it) every graph word may be an end,
    // as before: the graph is ENABLE, so they are still real words.
    func testWithoutAListEveryGraphWordOfTheLengthIsAnEnd() {
        XCTAssertEqual(graph.puzzleEnds(ofLength: 3, among: nil), ["cat", "cog", "cot", "dog", "kex"])
    }
}
