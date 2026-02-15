import SwiftUI

struct GameView: View {
    @Bindable var game: GameState
    @Environment(\.dismiss) private var dismiss
    @State private var showResults = false

    var body: some View {
        ZStack {
            Theme.secondaryBackground.ignoresSafeArea()

            if game.isComplete && showResults {
                ResultsView(
                    game: game,
                    onNewGame: {
                        withAnimation(Theme.springAnimation) {
                            showResults = false
                            game.startNewGame(mode: game.currentMode, difficulty: game.difficulty)
                        }
                    },
                    onMenu: {
                        dismiss()
                    }
                )
                .transition(.opacity.combined(with: .move(edge: .trailing)))
            } else {
                gameContent
                    .transition(.opacity.combined(with: .move(edge: .leading)))
            }
        }
        .navigationBarBackButtonHidden(true)
        .toolbar {
            ToolbarItem(placement: .cancellationAction) {
                Button {
                    dismiss()
                } label: {
                    HStack(spacing: 4) {
                        Image(systemName: "chevron.left")
                            .font(.system(size: 14, weight: .semibold))
                        Text("Menu")
                            .font(.system(size: 16, weight: .medium, design: .rounded))
                    }
                    .foregroundStyle(Theme.gradientStart)
                }
            }

            if game.currentMode == .timeAttack {
                ToolbarItem(placement: .principal) {
                    timerDisplay
                }
            }
        }
        .onChange(of: game.isComplete) {
            if game.isComplete {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    withAnimation(Theme.springAnimation) {
                        showResults = true
                    }
                }
            }
        }
    }

    // MARK: - Game Content

    private var gameContent: some View {
        VStack(spacing: 0) {
            puzzleHeader
            Divider()
            chainSection
            Divider()
            if !game.isComplete {
                WordInputView(game: game)
            }
        }
    }

    // MARK: - Puzzle Header

    private var puzzleHeader: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                HStack(spacing: 6) {
                    Text(game.difficulty.label)
                        .font(Theme.captionFont)
                        .foregroundStyle(.white)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 4)
                        .background(Capsule().fill(game.difficulty.color))

                    Text(game.currentMode.rawValue)
                        .font(Theme.captionFont)
                        .foregroundStyle(.secondary)
                }

                if let puzzle = game.currentPuzzle {
                    HStack(spacing: 8) {
                        Text(puzzle.start.uppercased())
                            .font(.system(size: 18, weight: .bold, design: .monospaced))
                            .foregroundStyle(Theme.chainStart)

                        Image(systemName: "arrow.right")
                            .font(.system(size: 14, weight: .bold))
                            .foregroundStyle(.secondary)

                        Text(puzzle.target.uppercased())
                            .font(.system(size: 18, weight: .bold, design: .monospaced))
                            .foregroundStyle(Theme.chainEnd)
                    }
                }
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 4) {
                Text("Steps")
                    .font(Theme.captionFont)
                    .foregroundStyle(.secondary)
                Text("\(game.chain.count - 1)")
                    .font(.system(size: 24, weight: .bold, design: .rounded))
                    .foregroundStyle(Theme.gradientStart)
            }
        }
        .padding(.horizontal, Theme.cardPadding)
        .padding(.vertical, 14)
        .background(Theme.cardBackground)
    }

    // MARK: - Chain

    private var chainSection: some View {
        Group {
            if let puzzle = game.currentPuzzle {
                ChainView(
                    chain: game.chain,
                    target: puzzle.target,
                    difficulty: game.difficulty
                )
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Theme.secondaryBackground)
    }

    // MARK: - Timer

    private var timerDisplay: some View {
        HStack(spacing: 6) {
            Image(systemName: "timer")
                .foregroundStyle(timerColor)
            Text(String(format: "%.0f", game.timeRemaining))
                .font(.system(size: 20, weight: .bold, design: .rounded))
                .foregroundStyle(timerColor)
                .monospacedDigit()
        }
    }

    private var timerColor: Color {
        if game.timeRemaining <= 10 { return Theme.errorRed }
        if game.timeRemaining <= 30 { return Theme.warningOrange }
        return Theme.gradientStart
    }
}
