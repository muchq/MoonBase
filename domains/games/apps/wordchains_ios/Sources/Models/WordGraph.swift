import Foundation

/// Mirror of the Rust `wordchains` library's Graph struct.
/// Loaded from the same JSON format produced by `wordchains generate-graph`.
struct WordGraph: Codable {
    let nodes: [String]
    let edges: [[Int]]

    private var wordToIndex: [String: Int] {
        Dictionary(uniqueKeysWithValues: nodes.enumerated().map { ($1, $0) })
    }

    /// BFS for a single shortest path — mirrors `bfs_for_target` in the Rust lib.
    func shortestPath(from start: String, to target: String) -> [String]? {
        if start == target { return [start] }
        if start.count != target.count { return nil }

        let index = wordToIndex
        guard let startIdx = index[start], index[target] != nil else { return nil }

        var parent: [Int: Int] = [:]
        var visited: Set<Int> = [startIdx]
        var queue: [Int] = [startIdx]
        var head = 0

        while head < queue.count {
            let current = queue[head]
            head += 1

            if nodes[current] == target {
                var path: [String] = []
                var node = current
                while node != startIdx {
                    path.append(nodes[node])
                    node = parent[node]!
                }
                path.append(nodes[startIdx])
                return path.reversed()
            }

            for neighbor in edges[current] {
                if !visited.contains(neighbor) {
                    visited.insert(neighbor)
                    parent[neighbor] = current
                    queue.append(neighbor)
                }
            }
        }
        return nil
    }

    /// Find all shortest paths — mirrors `find_all_shortest_paths` in the Rust lib.
    func allShortestPaths(from start: String, to target: String) -> [[String]] {
        if start == target { return [[start]] }

        let index = wordToIndex
        guard let startIdx = index[start], let targetIdx = index[target] else { return [] }

        var dist: [Int: Int] = [startIdx: 0]
        var parents: [Int: [Int]] = [:]
        var queue: [Int] = [startIdx]
        var head = 0
        var foundMinDist = Int.max

        while head < queue.count {
            let current = queue[head]
            head += 1
            let d = dist[current]!

            if d >= foundMinDist { continue }

            for neighbor in edges[current] {
                if neighbor == targetIdx {
                    foundMinDist = d + 1
                    parents[neighbor, default: []].append(current)
                } else if dist[neighbor] == nil {
                    dist[neighbor] = d + 1
                    parents[neighbor, default: []].append(current)
                    queue.append(neighbor)
                } else if dist[neighbor] == d + 1 {
                    parents[neighbor, default: []].append(current)
                }
            }
        }

        if foundMinDist == Int.max { return [] }

        var results: [[Int]] = []
        var path = [targetIdx]
        backtrack(current: targetIdx, start: startIdx, parents: parents, path: &path, results: &results)
        return results.map { $0.map { nodes[$0] } }
    }

    private func backtrack(current: Int, start: Int, parents: [Int: [Int]], path: inout [Int], results: inout [[Int]]) {
        if current == start {
            results.append(path.reversed())
            return
        }
        guard let pars = parents[current] else { return }
        for p in pars {
            path.append(p)
            backtrack(current: p, start: start, parents: parents, path: &path, results: &results)
            path.removeLast()
        }
    }

    func contains(_ word: String) -> Bool {
        wordToIndex[word] != nil
    }

    func neighbors(of word: String) -> [String] {
        guard let idx = wordToIndex[word] else { return [] }
        return edges[idx].map { nodes[$0] }
    }

    func words(ofLength length: Int) -> [String] {
        nodes.filter { $0.count == length }
    }

    /// Load graph from bundled JSON resource.
    static func loadBundled() -> WordGraph? {
        guard let url = Bundle.main.url(forResource: "word_graph", withExtension: "json") else {
            return nil
        }
        return load(from: url)
    }

    static func load(from url: URL) -> WordGraph? {
        guard let data = try? Data(contentsOf: url) else { return nil }
        return try? JSONDecoder().decode(WordGraph.self, from: data)
    }
}
