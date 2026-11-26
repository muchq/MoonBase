import SwiftUI

struct WordInputView: View {
    @Bindable var game: GameState
    @State private var entry = WordEntry()
    /// What the field shows. It follows `entry.text`, but owns its own state:
    /// see `settleField`.
    @State private var fieldText = ""
    @State private var shakeAttempt = 0
    @FocusState private var isFocused: Bool
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Nil under Reduce Motion, so state changes land without movement.
    private var motion: Animation? { reduceMotion ? nil : Theme.quickSpring }

    var body: some View {
        VStack(spacing: 12) {
            // The hidden twin holds the row's height, so an error never
            // shifts the input row; the real banner exists only with a message.
            ZStack {
                feedbackBanner(" ").hidden()
                if !game.feedbackMessage.isEmpty {
                    feedbackBanner(game.feedbackMessage)
                }
            }

            HStack(spacing: 12) {
                undoButton
                textField
                submitButton
                if game.hintsAllowed { hintButton }
            }
        }
        .padding(.horizontal, Theme.cardPadding)
        .padding(.vertical, 14)
        .background(
            Theme.cardBackground
                .shadow(color: .black.opacity(0.1), radius: 20, y: -8)
        )
        .onChange(of: game.feedbackMessage) { _, message in announce(message) }
    }

    // MARK: - Text Field

    private var textField: some View {
        // Vertical axis: Return types a newline instead of submitting, and
        // submitting is what makes SwiftUI dismiss the keyboard. WordEntry
        // turns the newline into a submit, so the keyboard stays up.
        TextField("Enter a word...", text: $fieldText, axis: .vertical)
            .lineLimit(1)
            .accessibilityIdentifier("word-input")
            .font(.system(size: 18, weight: .medium, design: .rounded))
            #if os(iOS)
            .textInputAutocapitalization(.never)
            #endif
            .autocorrectionDisabled()
            .focused($isFocused)
            .padding(.horizontal, 16)
            .padding(.vertical, 12)
            .background(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Theme.secondaryBackground)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .stroke(borderColor, lineWidth: isFocused ? 2 : 0)
            )
            .modifier(ShakeEffect(animatableData: CGFloat(shakeAttempt)))
            .onAppear { isFocused = true }
            .onChange(of: fieldText) { _, typed in
                guard typed != entry.text else { return }
                react(to: entry.change(to: typed, in: game))
                settleField()
            }
    }

    private var borderColor: Color {
        switch game.lastResult {
        case .notInDictionary, .notOneLetterAway, .alreadyUsed, .wrongLength:
            return Theme.errorRed
        case .valid:
            return Theme.validGreen
        default:
            return Theme.gradientStart
        }
    }

    // MARK: - Undo Button

    private var undoButton: some View {
        Button {
            withAnimation(motion) { entry.undo(in: game) }
            fieldText = entry.text
        } label: {
            Image(systemName: "arrow.uturn.backward")
                .font(.system(size: 18, weight: .semibold))
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .disabled(!game.canUndo)
        .buttonStyle(.plain)
        .foregroundStyle(game.canUndo ? Theme.gradientStart : Color.secondary)
        .accessibilityLabel("Undo")
        .accessibilityIdentifier("word-undo")
    }

    // MARK: - Submit Button

    private var submitButton: some View {
        Button {
            react(to: entry.submit(in: game))
            fieldText = entry.text
        } label: {
            Image(systemName: "arrow.up.circle.fill")
                .font(.system(size: 36))
                .foregroundStyle(
                    entry.text.isEmpty ? Color.secondary : Theme.gradientStart
                )
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .disabled(entry.text.isEmpty)
        .buttonStyle(.plain)
        .accessibilityLabel("Submit")
        .accessibilityIdentifier("word-submit")
    }

    // MARK: - Hint Button

    private var hintButton: some View {
        Button {
            withAnimation(motion) { entry.hint(in: game) }
            fieldText = entry.text
        } label: {
            Image(systemName: "lightbulb.fill")
                .font(.system(size: 20))
                .foregroundStyle(Theme.hintBlue)
                .frame(width: 36, height: 36)
                .background(
                    Circle()
                        .fill(Theme.hintBlue.opacity(0.12))
                )
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Hint")
        .accessibilityIdentifier("word-hint")
    }

    // MARK: - Feedback Banner

    private func feedbackBanner(_ message: String) -> some View {
        HStack(spacing: 6) {
            Image(systemName: feedbackIcon)
                .font(.system(size: 13, weight: .semibold))
            Text(message)
                .font(Theme.captionFont)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
                .accessibilityIdentifier("word-feedback")
                .accessibilityAddTraits(.updatesFrequently)
        }
        .foregroundStyle(Theme.errorRed)
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(
            Capsule()
                .fill(Theme.errorRed.opacity(0.1))
        )
    }

    private var feedbackIcon: String {
        switch game.lastResult {
        case .notInDictionary: return "book.closed"
        case .notOneLetterAway: return "1.circle"
        case .alreadyUsed: return "arrow.uturn.backward"
        case .wrongLength: return "ruler"
        case .noRoute: return "lightbulb.slash"
        default: return "exclamationmark.triangle"
        }
    }

    // MARK: - Actions

    /// When Return rewrites what was typed (the newline dropped, an accepted
    /// word cleared), the field must show the rewrite. Set during the field's
    /// own edit, SwiftUI keeps the text view's old contents and the next
    /// keystroke lands after the hidden word; set on the next turn, it takes.
    private func settleField() {
        guard fieldText != entry.text else { return }
        DispatchQueue.main.async { fieldText = entry.text }
    }

    /// Shake and buzz on a rejected word; WordEntry already did the rest.
    private func react(to result: InputResult?) {
        switch result {
        case nil, .gameComplete, .noRoute, .valid, .reachedTarget, .timeUp:
            return
        case .notInDictionary, .notOneLetterAway, .alreadyUsed, .wrongLength:
            if !reduceMotion {
                withAnimation(Theme.quickSpring) { shakeAttempt += 1 }
            }
            hapticError()
        }
    }

    private func announce(_ message: String) {
        #if os(iOS)
        if !message.isEmpty { AccessibilityNotification.Announcement(message).post() }
        #endif
    }

    private func hapticError() {
        #if canImport(UIKit)
        let generator = UINotificationFeedbackGenerator()
        generator.notificationOccurred(.error)
        #endif
    }
}
