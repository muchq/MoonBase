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
