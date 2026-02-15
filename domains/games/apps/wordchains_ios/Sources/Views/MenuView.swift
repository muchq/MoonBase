import SwiftUI

struct MenuView: View {
    @Bindable var game: GameState
    @State private var selectedMode: GameMode = .quickPlay
    @State private var selectedDifficulty: Difficulty = .medium
    @State private var showGame = false
    @State private var animateIn = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: Theme.sectionSpacing) {
                    header
                    modeSelector
                    difficultySelector
                    playButton
                    statsCard
                }
                .padding(.horizontal, Theme.cardPadding)
                .padding(.top, 8)
                .padding(.bottom, 40)
            }
            .background(Theme.secondaryBackground.ignoresSafeArea())
            .navigationDestination(isPresented: $showGame) {
                GameView(game: game)
            }
            .onAppear {
                withAnimation(Theme.springAnimation.delay(0.1)) {
                    animateIn = true
                }
            }
        }
    }

    // MARK: - Header

    private var header: some View {
        VStack(spacing: 8) {
            Text("Word Chains")
                .font(Theme.titleFont)
                .foregroundStyle(Theme.headerGradient)

            Text("Change one letter at a time")
                .font(Theme.bodyFont)
                .foregroundStyle(.secondary)
        }
        .padding(.top, 20)
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 20)
    }

    // MARK: - Mode Selector

    private var modeSelector: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Game Mode")
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
                .textCase(.uppercase)
                .tracking(1)

            VStack(spacing: 8) {
                ForEach(GameMode.allCases) { mode in
                    Button {
                        withAnimation(Theme.quickSpring) {
                            selectedMode = mode
                        }
                    } label: {
                        HStack(spacing: 14) {
                            Image(systemName: mode.iconName)
                                .font(.system(size: 20))
                                .foregroundStyle(selectedMode == mode ? .white : Theme.gradientStart)
                                .frame(width: 36, height: 36)
                                .background(
                                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                                        .fill(selectedMode == mode ? Theme.gradientStart : Theme.gradientStart.opacity(0.12))
                                )

                            VStack(alignment: .leading, spacing: 2) {
                                Text(mode.rawValue)
                                    .font(.system(size: 16, weight: .semibold, design: .rounded))
                                    .foregroundStyle(.primary)
                                Text(mode.description)
                                    .font(Theme.captionFont)
                                    .foregroundStyle(.secondary)
                            }

                            Spacer()

                            if selectedMode == mode {
                                Image(systemName: "checkmark.circle.fill")
                                    .foregroundStyle(Theme.gradientStart)
                                    .transition(.scale.combined(with: .opacity))
                            }
                        }
                        .padding(14)
                        .background(
                            RoundedRectangle(cornerRadius: 14, style: .continuous)
                                .fill(selectedMode == mode ? Theme.gradientStart.opacity(0.08) : Color.clear)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 14, style: .continuous)
                                .stroke(selectedMode == mode ? Theme.gradientStart.opacity(0.3) : Color.clear, lineWidth: 1.5)
                        )
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .cardStyle()
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 30)
    }

    // MARK: - Difficulty

    private var difficultySelector: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Difficulty")
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
                .textCase(.uppercase)
                .tracking(1)

            HStack(spacing: 10) {
                ForEach(Difficulty.allCases) { diff in
                    Button {
                        withAnimation(Theme.quickSpring) {
                            selectedDifficulty = diff
                        }
                    } label: {
                        VStack(spacing: 6) {
                            Text(diff.label)
                                .font(.system(size: 15, weight: .semibold, design: .rounded))
                            Text("\(diff.letterCount) letters")
                                .font(Theme.captionFont)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 14)
                        .foregroundStyle(selectedDifficulty == diff ? .white : .primary)
                        .background(
                            RoundedRectangle(cornerRadius: 12, style: .continuous)
                                .fill(selectedDifficulty == diff ? diff.color : diff.color.opacity(0.1))
                        )
                    }
                    .buttonStyle(.plain)
                }
            }
        }
        .cardStyle()
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 40)
    }

    // MARK: - Play Button

    private var playButton: some View {
        Button {
            game.startNewGame(mode: selectedMode, difficulty: selectedDifficulty)
            showGame = true
        } label: {
            HStack(spacing: 10) {
                Image(systemName: "play.fill")
                    .font(.system(size: 18))
                Text("Play")
                    .font(.system(size: 20, weight: .bold, design: .rounded))
            }
            .foregroundStyle(.white)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 18)
            .background(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .fill(
                        LinearGradient(
                            colors: [Theme.gradientStart, Theme.gradientEnd],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    )
            )
            .shadow(color: Theme.gradientStart.opacity(0.4), radius: 12, y: 6)
        }
        .buttonStyle(.plain)
        .disabled(game.isLoading || game.graph == nil)
        .opacity(game.isLoading ? 0.6 : 1)
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 50)
    }

    // MARK: - Stats

    private var statsCard: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Stats")
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
                .textCase(.uppercase)
                .tracking(1)

            HStack(spacing: 0) {
                statItem(value: "\(game.puzzlesSolved)", label: "Solved")
                Divider().frame(height: 36)
                statItem(value: "\(game.optimalSolves)", label: "Optimal")
                Divider().frame(height: 36)
                statItem(value: "\(game.bestStreak)", label: "Best Streak")
                Divider().frame(height: 36)
                statItem(value: "\(game.totalScore)", label: "Score")
            }
        }
        .cardStyle()
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 60)
    }

    private func statItem(value: String, label: String) -> some View {
        VStack(spacing: 4) {
            Text(value)
                .font(.system(size: 22, weight: .bold, design: .rounded))
                .foregroundStyle(.primary)
            Text(label)
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
}
