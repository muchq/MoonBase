import XCTest
@testable import WordChains

final class WordGraphTests: XCTestCase {

    // MARK: - Test Graph

    /// Build a small test graph matching the Rust lib's test fixtures.
    /// Words: cat, cot, cog, dog, cag
    private func makeTestGraph() -> WordGraph {
        let words = ["cat", "cot", "cog", "dog", "cag"]
        var edges: [[Int]] = Array(repeating: [], count: words.count)

        // Build edges: words differing by exactly one letter
        for i in 0..<words.count {
            for j in (i + 1)..<words.count {
                if GameState.isOneLetterAway(words[i], words[j]) {
                    edges[i].append(j)
                    edges[j].append(i)
                }
            }
        }

        return WordGraph(nodes: words, edges: edges)
    }

    // MARK: - Shortest Path Tests

    func testShortestPathIdentity() {
        let graph = makeTestGraph()
        let path = graph.shortestPath(from: "cat", to: "cat")
        XCTAssertEqual(path, ["cat"])
    }

    func testShortestPathSimple() {
        let graph = makeTestGraph()
        let path = graph.shortestPath(from: "cat", to: "dog")
        XCTAssertNotNil(path)
        XCTAssertEqual(path?.first, "cat")
        XCTAssertEqual(path?.last, "dog")
        XCTAssertEqual(path?.count, 4) // cat -> cot -> cog -> dog
    }

    func testShortestPathNoPath() {
        // cat and dog not connected (no cog)
        let graph = WordGraph(
            nodes: ["cat", "cot", "dog"],
            edges: [[1], [0], []]
        )
        let path = graph.shortestPath(from: "cat", to: "dog")
        XCTAssertNil(path)
    }

    func testShortestPathDifferentLengths() {
        let graph = makeTestGraph()
        let path = graph.shortestPath(from: "cat", to: "dogs")
        XCTAssertNil(path, "Words of different lengths should return nil")
    }

    func testShortestPathMissingWord() {
        let graph = makeTestGraph()
        let path = graph.shortestPath(from: "cat", to: "fox")
        XCTAssertNil(path, "Word not in graph should return nil")
    }

    // MARK: - All Shortest Paths Tests

    func testAllShortestPathsIdentity() {
        let graph = makeTestGraph()
        let paths = graph.allShortestPaths(from: "cat", to: "cat")
        XCTAssertEqual(paths, [["cat"]])
    }

    func testAllShortestPaths() {
        let graph = makeTestGraph()
        let paths = graph.allShortestPaths(from: "cat", to: "dog")
        XCTAssertFalse(paths.isEmpty)

        for path in paths {
            XCTAssertEqual(path.count, 4)
            XCTAssertEqual(path.first, "cat")
            XCTAssertEqual(path.last, "dog")
        }
        // Should find 2 shortest paths: cat-cot-cog-dog and cat-cag-cog-dog
        XCTAssertEqual(paths.count, 2)
    }

    func testAllShortestPathsNoPath() {
        let graph = WordGraph(nodes: ["cat", "dog"], edges: [[], []])
        let paths = graph.allShortestPaths(from: "cat", to: "dog")
        XCTAssertTrue(paths.isEmpty)
    }

    // MARK: - One Letter Away Tests

    func testIsOneLetterAway() {
        XCTAssertTrue(GameState.isOneLetterAway("cat", "cot"))
        XCTAssertTrue(GameState.isOneLetterAway("cat", "bat"))
        XCTAssertTrue(GameState.isOneLetterAway("star", "stat"))
        XCTAssertFalse(GameState.isOneLetterAway("cat", "dog"))
        XCTAssertFalse(GameState.isOneLetterAway("cat", "cats"))
        XCTAssertFalse(GameState.isOneLetterAway("cat", "cat"))
    }

    // MARK: - Graph Query Tests

    func testContains() {
        let graph = makeTestGraph()
        XCTAssertTrue(graph.contains("cat"))
        XCTAssertTrue(graph.contains("dog"))
        XCTAssertFalse(graph.contains("fox"))
    }

    func testNeighbors() {
        let graph = makeTestGraph()
        let neighbors = graph.neighbors(of: "cog")
        XCTAssertTrue(neighbors.contains("cot"))
        XCTAssertTrue(neighbors.contains("dog"))
        XCTAssertTrue(neighbors.contains("cag"))
    }

    func testWordsOfLength() {
        let graph = makeTestGraph()
        let threeLetters = graph.words(ofLength: 3)
        XCTAssertEqual(threeLetters.count, 5)
        let fourLetters = graph.words(ofLength: 4)
        XCTAssertTrue(fourLetters.isEmpty)
    }
}
