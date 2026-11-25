import Foundation

/// A candidate changes only the selected letter; choosing another tile starts
/// again from the current chain word, rather than accumulating letter changes.
struct WordDraft {
    let original: String
    private(set) var selectedIndex: Int?
    private(set) var replacement = ""

    var word: String {
        var letters = Array(original)
        if let selectedIndex, letters.indices.contains(selectedIndex), let letter = replacement.first {
            letters[selectedIndex] = letter
        }
        return String(letters)
    }

    var canSubmit: Bool { word != original }

    mutating func select(_ index: Int) {
        guard index >= 0, index < original.count else { return }
        selectedIndex = index
        replacement = ""
    }

    mutating func replace(with input: String) {
        guard selectedIndex != nil else { return }
        if input.isEmpty {
            replacement = ""
        } else if let letter = input.lowercased().last,
                  letter.unicodeScalars.count == 1,
                  let scalar = letter.unicodeScalars.first, (97...122).contains(scalar.value) {
            // Typing again replaces the candidate, even if the keyboard appends.
            replacement = String(letter)
        }
    }
}
