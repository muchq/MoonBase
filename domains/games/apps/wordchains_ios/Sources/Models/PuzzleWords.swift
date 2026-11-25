import Foundation

/// The words single-player puzzles start and end on: SCOWL's common words,
/// the list races draw from (`//domains/games/libs/wordchain_race:common_words.txt`,
/// bundled with its SCOWL_COPYRIGHT). The graph is every word a player may
/// type on the way; this is the smaller set a puzzle opens and closes on.
enum PuzzleWords {
    /// The bundled file's name: BUILD's resource and app_store_test agree.
    static let fileName = "common_words.txt"

    static func parse(_ text: String) -> Set<String> {
        Set(text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces).lowercased() }
            .filter { !$0.isEmpty })
    }

    /// nil when the bundle has no list.
    static func load(from bundle: Bundle) -> Set<String>? {
        let name = (fileName as NSString).deletingPathExtension
        let ext = (fileName as NSString).pathExtension
        guard let url = bundle.url(forResource: name, withExtension: ext),
              let text = try? String(contentsOf: url, encoding: .utf8) else { return nil }
        return parse(text)
    }
}
