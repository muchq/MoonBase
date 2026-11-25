import Foundation

/// The words single-player puzzles start and end on: SCOWL's common words,
/// the list races draw from (`//domains/games/libs/wordchain_race:common_words.txt`,
/// bundled with its SCOWL_COPYRIGHT). The graph is every word a player may
/// type on the way; this is the smaller set a puzzle opens and closes on.
enum PuzzleWords {
    static func parse(_ text: String) -> Set<String> {
        Set(text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces).lowercased() }
            .filter { !$0.isEmpty })
    }

    /// nil when the bundle has no list.
    static func loadBundled() -> Set<String>? {
        guard let url = Bundle.main.url(forResource: "common_words", withExtension: "txt"),
              let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        return parse(text)
    }
}
