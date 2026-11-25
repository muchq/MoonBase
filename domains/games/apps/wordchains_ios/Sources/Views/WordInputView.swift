import SwiftUI

struct WordInputView: View {
    @Bindable var game: GameState
    @State private var draft = WordDraft(original: "")
    @State private var letterInput = ""
    @State private var shakeAttempt = 0
    @FocusState private var focusedIndex: Int?
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        VStack(spacing: 10) {
            Text(instruction)
                .font(Theme.captionFont)
                .foregroundStyle(game.feedbackMessage.isEmpty ? Color.secondary : Theme.errorRed)
                .multilineTextAlignment(.center)
                .lineLimit(2, reservesSpace: true)
                .accessibilityAddTraits(.updatesFrequently)
                .accessibilityIdentifier("word-feedback")

            HStack(spacing: Theme.tileSpacing) {
                ForEach(Array(draft.word.enumerated()), id: \.offset) { index, letter in
                    letterTile(letter, at: index)
                }
            }
            .modifier(ShakeEffect(animatableData: CGFloat(shakeAttempt)))

            HStack {
                Button {
                    withAnimation(reduceMotion ? nil : Theme.quickSpring) {
                        game.undo()
                    }
                    resetDraft()
                } label: {
                    Label("Undo", systemImage: "arrow.uturn.backward")
                        .frame(minHeight: 44)
                }
                .disabled(!game.canUndo)
                .accessibilityIdentifier("word-undo")

                Spacer(minLength: 8)

                Button(action: requestHint) {
                    Label(hintLabel, systemImage: "lightbulb")
                        .frame(minHeight: 44)
                }
                .accessibilityIdentifier("word-hint")

                Spacer(minLength: 8)

                Button(action: submit) {
                    Text("Play")
                        .fontWeight(.semibold)
                        .frame(minWidth: 44, minHeight: 44)
                }
                .buttonStyle(.borderedProminent)
                .disabled(game.isComplete || !draft.canSubmit)
                .accessibilityLabel("Play \(draft.word.uppercased())")
                .accessibilityIdentifier("word-play")
            }
            .font(.subheadline)
            .tint(Theme.gradientStart)
        }
        .padding(.horizontal, Theme.cardPadding)
        .padding(.vertical, 12)
        .background(
            Theme.cardBackground
                .shadow(color: .black.opacity(0.1), radius: 20, y: -8)
        )
        .onChange(of: game.chain, initial: true) { _, _ in resetDraft() }
        .onChange(of: game.currentPuzzle?.id) { _, _ in resetDraft(keepingSelection: false) }
        .onChange(of: game.feedbackMessage) { _, message in announce(message) }
        .onChange(of: letterInput) { _, typed in
            draft.replace(with: typed)
            // Keep native text storage in sync too. A transforming Binding
            // alone can leave lowercase or multiple characters on screen.
            letterInput = draft.replacement.uppercased()
            game.lastResult = nil
            game.showInvalidShake = false
        }
    }

    private func letterTile(_ letter: Character, at index: Int) -> some View {
        let selected = draft.selectedIndex == index
        let hinted = hintedIndex == index
        let original = String(Array(draft.original)[index]).uppercased()

        return ZStack {
            if selected {
                TextField(original, text: $letterInput, prompt: Text(original).foregroundStyle(.primary))
                    .multilineTextAlignment(.center)
                    .typedAs(.characters)
                    #if os(iOS)
                    .keyboardType(.asciiCapable)
                    #endif
                    .submitLabel(.go)
                    .focused($focusedIndex, equals: index)
                    .onSubmit(submit)
                    .task { focusedIndex = index }
                    .accessibilityLabel("Replace letter \(index + 1), \(original)")
                    .accessibilityHint("Type one letter, then tap Play")
                    .accessibilityIdentifier("word-letter-\(index)")
            } else {
                Button {
                    draft.select(index)
                    letterInput = ""
                } label: {
                    Text(String(letter).uppercased())
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Letter \(index + 1), \(String(letter).uppercased())")
                .accessibilityHint("Double tap to replace this letter")
                .accessibilityIdentifier("word-letter-\(index)")
            }
        }
        .font(Theme.tileFont)
        .frame(width: Theme.tileSize, height: Theme.tileSize)
        .background(
            RoundedRectangle(cornerRadius: 10)
                .fill(hinted ? Theme.hintBlue.opacity(0.18) : Theme.tertiaryBackground)
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10)
                .stroke(selected || hinted ? Theme.gradientStart : Color.clear, lineWidth: 2)
        )
        .overlay(alignment: .bottom) {
            if hinted {
                Circle().fill(Theme.gradientStart).frame(width: 5, height: 5).padding(.bottom, 3)
            }
        }
    }

    private var hintedIndex: Int? {
        switch game.currentHint {
        case .letter(let index): return index
        case .word(let word):
            return zip(draft.original, word).enumerated().first(where: { $0.element.0 != $0.element.1 })?.offset
        default: return nil
        }
    }

    private var hintLabel: String {
        if case .letter = game.currentHint { return "Reveal" }
        return "Hint"
    }

    private var instruction: String {
        if !game.feedbackMessage.isEmpty { return game.feedbackMessage }
        switch game.currentHint {
        case .letter(let index): return "Try changing letter \(index + 1). Tap Reveal for the answer."
        case .word(let word): return "Try \(word.uppercased()). Tap Play to add it."
        case .unavailable: return "No unused route from here. Undo to try another path."
        case nil: return "Tap a letter to change it, then Play."
        }
    }

    private func requestHint() {
        switch game.requestHint() {
        case .letter(let index):
            draft.select(index)
            letterInput = ""
            focusedIndex = index
        case .word(let word):
            if let index = hintedIndex {
                draft.select(index)
                draft.replace(with: String(Array(word)[index]))
                letterInput = draft.replacement.uppercased()
                focusedIndex = index
            }
        case .unavailable: break
        }
        announce(instruction)
    }

    private func resetDraft(keepingSelection: Bool = true) {
        let selection = keepingSelection ? draft.selectedIndex : nil
        draft = WordDraft(original: game.chain.last ?? "")
        letterInput = ""
        if let selection {
            draft.select(selection)
        } else {
            focusedIndex = nil
        }
    }

    private func submit() {
        guard !game.isComplete, draft.canSubmit else { return }
        let result = withAnimation(reduceMotion ? nil : Theme.springAnimation) {
            game.submitWord(draft.word)
        }
        switch result {
        case .gameComplete:
            return
        case .valid, .reachedTarget:
            resetDraft()
            #if canImport(UIKit)
            UIImpactFeedbackGenerator(style: .light).impactOccurred()
            #endif
        case .notInDictionary, .notOneLetterAway, .alreadyUsed, .wrongLength:
            if !reduceMotion {
                withAnimation(Theme.quickSpring) { shakeAttempt += 1 }
            }
            #if canImport(UIKit)
            UINotificationFeedbackGenerator().notificationOccurred(.error)
            #endif
        }
    }

    private func announce(_ message: String) {
        #if os(iOS)
        if !message.isEmpty { AccessibilityNotification.Announcement(message).post() }
        #endif
    }
}
