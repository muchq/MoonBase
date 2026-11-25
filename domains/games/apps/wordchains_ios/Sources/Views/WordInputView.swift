import SwiftUI

struct WordInputView: View {
    @Bindable var game: GameState
    @State private var inputText = ""
    @State private var shakeAttempt = 0
    @FocusState private var isFocused: Bool
    /// Set by Return: the system resigns focus after onSubmit, and this
    /// takes it straight back so the keyboard stays up between words.
    @State private var refocusAfterSubmit = false
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    /// Nil under Reduce Motion, so state changes land without movement.
    private var motion: Animation? { reduceMotion ? nil : Theme.quickSpring }

    var body: some View {
        VStack(spacing: 12) {
            // Always laid out, so an error never shifts the input row.
            feedbackBanner

            HStack(spacing: 12) {
                undoButton
                textField
                submitButton
                hintButton
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
        TextField("Enter a word...", text: $inputText)
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
            .onSubmit {
                refocusAfterSubmit = true
                submit()
            }
            .onChange(of: isFocused) { _, focused in
                guard !focused, refocusAfterSubmit else { return }
                refocusAfterSubmit = false
                isFocused = true
            }
            .onAppear { isFocused = true }
            .onChange(of: inputText) {
                game.lastResult = nil
                game.showInvalidShake = false
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
            withAnimation(motion) { game.undo() }
            inputText = ""
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
        Button(action: submit) {
            Image(systemName: "arrow.up.circle.fill")
                .font(.system(size: 36))
                .foregroundStyle(
                    inputText.isEmpty ? Color.secondary : Theme.gradientStart
                )
                .frame(width: 44, height: 44)
                .contentShape(Rectangle())
        }
        .disabled(inputText.isEmpty)
        .buttonStyle(.plain)
        .accessibilityLabel("Submit")
        .accessibilityIdentifier("word-submit")
    }

    // MARK: - Hint Button

    private var hintButton: some View {
        Button {
            if let hint = game.requestHint() {
                withAnimation(motion) { inputText = hint }
            }
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

    private var feedbackBanner: some View {
        HStack(spacing: 6) {
            Image(systemName: feedbackIcon)
                .font(.system(size: 13, weight: .semibold))
            // A space keeps the row's height while there is nothing to say.
            Text(game.feedbackMessage.isEmpty ? " " : game.feedbackMessage)
                .font(Theme.captionFont)
                .lineLimit(1)
                .minimumScaleFactor(0.8)
                .accessibilityIdentifier("word-feedback")
        }
        .foregroundStyle(Theme.errorRed)
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(
            Capsule()
                .fill(Theme.errorRed.opacity(0.1))
        )
        .opacity(game.feedbackMessage.isEmpty ? 0 : 1)
        .accessibilityHidden(game.feedbackMessage.isEmpty)
        .accessibilityAddTraits(.updatesFrequently)
    }

    private var feedbackIcon: String {
        switch game.lastResult {
        case .notInDictionary: return "book.closed"
        case .notOneLetterAway: return "character.cursor.ibeam"
        case .alreadyUsed: return "arrow.uturn.backward"
        case .wrongLength: return "ruler"
        case .noRoute: return "lightbulb.slash"
        default: return "exclamationmark.triangle"
        }
    }

    // MARK: - Actions

    private func submit() {
        guard !inputText.isEmpty else { return }

        let result = game.submitWord(inputText)

        switch result {
        case .gameComplete, .noRoute:
            return
        case .valid, .reachedTarget:
            inputText = ""
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
