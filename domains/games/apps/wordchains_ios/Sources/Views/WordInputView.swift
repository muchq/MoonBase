import SwiftUI

struct WordInputView: View {
    @Bindable var game: GameState
    @State private var inputText = ""
    @State private var shakeAttempt = 0
    @FocusState private var isFocused: Bool

    var body: some View {
        VStack(spacing: 12) {
            if !game.feedbackMessage.isEmpty {
                feedbackBanner
            }

            HStack(spacing: 12) {
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
    }

    // MARK: - Text Field

    private var textField: some View {
        TextField("Enter a word...", text: $inputText)
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
            .onSubmit(submit)
            .onAppear { isFocused = true }
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

    // MARK: - Submit Button

    private var submitButton: some View {
        Button(action: submit) {
            Image(systemName: "arrow.up.circle.fill")
                .font(.system(size: 36))
                .foregroundStyle(
                    inputText.isEmpty ? Color.secondary : Theme.gradientStart
                )
        }
        .disabled(inputText.isEmpty)
        .buttonStyle(.plain)
    }

    // MARK: - Hint Button

    private var hintButton: some View {
        Button {
            if let hint = game.hint() {
                withAnimation(Theme.quickSpring) {
                    inputText = hint
                }
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
        }
        .buttonStyle(.plain)
    }

    // MARK: - Feedback Banner

    private var feedbackBanner: some View {
        HStack(spacing: 6) {
            Image(systemName: feedbackIcon)
                .font(.system(size: 13, weight: .semibold))
            Text(game.feedbackMessage)
                .font(Theme.captionFont)
        }
        .foregroundStyle(Theme.errorRed)
        .padding(.horizontal, 14)
        .padding(.vertical, 8)
        .background(
            Capsule()
                .fill(Theme.errorRed.opacity(0.1))
        )
        .transition(.move(edge: .bottom).combined(with: .opacity))
    }

    private var feedbackIcon: String {
        switch game.lastResult {
        case .notInDictionary: return "book.closed"
        case .notOneLetterAway: return "character.cursor.ibeam"
        case .alreadyUsed: return "arrow.uturn.backward"
        case .wrongLength: return "ruler"
        default: return "exclamationmark.triangle"
        }
    }

    // MARK: - Actions

    private func submit() {
        guard !inputText.isEmpty else { return }

        let result = game.submitWord(inputText)

        switch result {
        case .valid, .reachedTarget:
            inputText = ""
        case .notInDictionary, .notOneLetterAway, .alreadyUsed, .wrongLength:
            withAnimation(Theme.quickSpring) {
                shakeAttempt += 1
            }
            hapticError()
        }
    }

    private func hapticError() {
        #if canImport(UIKit)
        let generator = UINotificationFeedbackGenerator()
        generator.notificationOccurred(.error)
        #endif
    }
}
