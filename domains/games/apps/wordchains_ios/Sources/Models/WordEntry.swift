import Foundation

/// What the player has typed, and how typing drives the game. The view feeds
/// every edit of a multi-line field to `change(to:in:)`: Return arrives as a
/// trailing newline, which submits instead of dismissing the keyboard.
struct WordEntry {
    private(set) var text = ""
    /// The word a Return just accepted, until the text changes or the chain
    /// moves: the field can deliver that Return a second time, after the
    /// text has cleared.
    private var accepted: String?

    /// Returns the submit result when the change was a Return, else nil.
    @discardableResult
    mutating func change(to new: String, in game: GameState) -> InputResult? {
        let replay = accepted
        if new != text { accepted = nil }
        guard new.contains("\n") else {
            if new != text {
                game.lastResult = nil
                game.showInvalidShake = false
            }
            text = new
            return nil
        }
        // A newline with typing after it is one the field has not cleared
        // yet, not a Return: whatever follows an accepted word starts the next.
        guard new.hasSuffix("\n") else {
            if let replay, text.isEmpty, new.hasPrefix(replay + "\n") {
                return change(to: String(new.dropFirst(replay.count + 1)).replacingOccurrences(of: "\n", with: ""), in: game)
            }
            return change(to: new.replacingOccurrences(of: "\n", with: ""), in: game)
        }
        let word = new.replacingOccurrences(of: "\n", with: "")
        if text.isEmpty && word == replay {
            accepted = replay
            return nil
        }
        text = word
        let result = submit(in: game)
        if result == .valid || result == .reachedTarget { accepted = word }
        return result
    }

    @discardableResult
    mutating func submit(in game: GameState) -> InputResult? {
        guard !text.isEmpty else { return nil }
        let result = game.submitWord(text)
        if result == .valid || result == .reachedTarget { text = "" }
        return result
    }

    mutating func hint(in game: GameState) {
        accepted = nil
        if let next = game.requestHint() { text = next }
    }

    mutating func undo(in game: GameState) {
        accepted = nil
        game.undo()
        text = ""
    }
}
