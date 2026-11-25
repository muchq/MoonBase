import Foundation

/// What the player has typed, and how typing drives the game. The view binds
/// a multi-line field to `text` through `change(to:in:)`: Return arrives as a
/// newline, which submits instead of dismissing the keyboard.
struct WordEntry {
    private(set) var text = ""

    /// Returns the submit result when the change was a Return, else nil.
    @discardableResult
    mutating func change(to new: String, in game: GameState) -> InputResult? {
        guard new.contains("\n") else {
            if new != text {
                game.lastResult = nil
                game.showInvalidShake = false
            }
            text = new
            return nil
        }
        text = new.replacingOccurrences(of: "\n", with: "")
        return submit(in: game)
    }

    @discardableResult
    mutating func submit(in game: GameState) -> InputResult? {
        guard !text.isEmpty else { return nil }
        let result = game.submitWord(text)
        if result == .valid || result == .reachedTarget { text = "" }
        return result
    }

    mutating func hint(in game: GameState) {
        if let next = game.requestHint() { text = next }
    }

    mutating func undo(in game: GameState) {
        game.undo()
        text = ""
    }
}
