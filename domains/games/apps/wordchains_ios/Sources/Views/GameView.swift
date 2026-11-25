import SwiftUI

struct GameView: View {
    @Bindable var game: GameState
    @Environment(\.dismiss) private var dismiss
    @State private var showResults = false
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    private var isTimeAttack: Bool { game.currentMode == .timeAttack }

    var body: some View {
        ZStack {
            Theme.secondaryBackground.ignoresSafeArea()

            if let summary = game.timeAttackSummary {
                TimeAttackSummaryView(
                    summary: summary,
                    difficulty: game.difficulty,
                    onReplay: { game.startNewGame(mode: .timeAttack, difficulty: game.difficulty) },
                    onMenu: { dismiss() }
                )
                .transition(.opacity)
            } else if !isTimeAttack && game.isComplete && showResults {
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
        }
        // One clock for the sprint: suspension pauses this loop, not the
        // deadline, and the first tick back settles what time it is. A
        // sprint left mid-way stops here, unrecorded.
        .task(id: game.timeAttack?.deadline) {
            while game.timeAttack != nil, game.timeAttackSummary == nil, !Task.isCancelled {
                game.tick()
                try? await Task.sleep(for: .milliseconds(200))
            }
        }
        .onChange(of: game.timeAttack?.solved) { old, new in
            if let old, let new, new > old { celebrateSolve() }
        }
        .onChange(of: game.isComplete) {
            if game.isComplete && !isTimeAttack {
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
            if isTimeAttack {
                TimeAttackBar(game: game)
                // Mounted through each solve, so the keyboard stays up.
                WordInputView(game: game)
            } else if !game.isComplete {
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
                Text("\(game.stepsTaken)")
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
        .overlay {
            if isTimeAttack && game.isComplete && game.timeAttackSummary == nil {
                solvedBadge
                    .transition(reduceMotion ? .opacity : .scale.combined(with: .opacity))
            }
        }
        .animation(reduceMotion ? nil : Theme.springAnimation, value: game.isComplete)
    }

    // MARK: - Time Attack

    private var solvedBadge: some View {
        Text("Solved! +\(game.score)")
            .font(.system(size: 22, weight: .bold, design: .rounded))
            .foregroundStyle(.white)
            .padding(.horizontal, 20)
            .padding(.vertical, 10)
            .background(Capsule().fill(Theme.validGreen))
            .accessibilityHidden(true)
    }

    private func celebrateSolve() {
        #if os(iOS)
        AccessibilityNotification.Announcement("Solved, plus \(game.score) points").post()
        #endif
        #if canImport(UIKit)
        UINotificationFeedbackGenerator().notificationOccurred(.success)
        #endif
    }
}

/// Time left, puzzles solved and points, just above the input. Fixed height
/// and monospaced digits, so a ticking clock never moves the input row.
struct TimeAttackBar: View {
    let game: GameState

    var body: some View {
        HStack(spacing: 0) {
            item(icon: "timer", value: "\(game.timeAttackSeconds)s", color: timerColor)
            item(icon: "checkmark.circle", value: "\(game.timeAttack?.solved ?? 0)", color: .primary)
            item(icon: "flame", value: "\(game.timeAttack?.score ?? 0)", color: .primary)
        }
        .frame(height: 36)
        .background(Theme.cardBackground)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(spoken)
        .accessibilityAddTraits(.updatesFrequently)
        .accessibilityIdentifier("time-attack-bar")
    }

    private var spoken: String {
        let solved = game.timeAttack?.solved ?? 0
        return "\(game.timeAttackSeconds) seconds left, \(solved) solved, \(game.timeAttack?.score ?? 0) points"
    }

    private func item(icon: String, value: String, color: Color) -> some View {
        HStack(spacing: 6) {
            Image(systemName: icon)
                .font(.system(size: 15, weight: .semibold))
            Text(value)
                .font(.system(size: 18, weight: .bold, design: .rounded))
                .monospacedDigit()
                .lineLimit(1)
        }
        .foregroundStyle(color)
        .frame(maxWidth: .infinity)
    }

    private var timerColor: Color {
        if game.timeAttackSeconds <= 10 { return Theme.errorRed }
        if game.timeAttackSeconds <= 30 { return Theme.warningOrange }
        return Theme.gradientStart
    }
}
