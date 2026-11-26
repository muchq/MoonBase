import SwiftUI

struct MenuView: View {
    @Bindable var game: GameState
    @State private var selectedMode: GameMode = .quickPlay
    @State private var selectedDifficulty: Difficulty = .medium
    @State private var showGame = false
    @State private var animateIn = false
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @Environment(\.dynamicTypeSize) private var dynamicTypeSize

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: Theme.sectionSpacing) {
                    header
                    modeSelector
                    difficultySelector
                    playButton
                    raceButton
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
                withAnimation(reduceMotion ? nil : Theme.springAnimation.delay(0.1)) {
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
                .foregroundStyle(.primary)

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
                        withAnimation(reduceMotion ? nil : Theme.quickSpring) {
                            selectedMode = mode
                        }
                    } label: {
                        HStack(spacing: 14) {
                            Image(systemName: mode.iconName)
                                .font(.system(size: 20))
                                .foregroundStyle(selectedMode == mode ? .white : Theme.accent)
                                .frame(width: 36, height: 36)
                                .background(
                                    RoundedRectangle(cornerRadius: 10, style: .continuous)
                                        .fill(selectedMode == mode ? Theme.primaryFill : Theme.accent.opacity(0.12))
                                )
                                .accessibilityHidden(true)

                            Text(mode.rawValue)
                                .font(.system(.headline, design: .rounded))
                                .foregroundStyle(.primary)
                                .frame(maxWidth: .infinity, alignment: .leading)

                            Image(systemName: "checkmark.circle.fill")
                                .foregroundStyle(Theme.accent)
                                .opacity(selectedMode == mode ? 1 : 0)
                                .accessibilityHidden(true)
                        }
                        .padding(14)
                        .contentShape(Rectangle())
                        .background(
                            RoundedRectangle(cornerRadius: 14, style: .continuous)
                                .fill(selectedMode == mode ? Theme.accent.opacity(0.08) : Color.clear)
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 14, style: .continuous)
                                .stroke(selectedMode == mode ? Theme.accent.opacity(0.3) : Color.clear, lineWidth: 1.5)
                        )
                    }
                    .buttonStyle(.plain)
                    .accessibilityAddTraits(selectedMode == mode ? .isSelected : [])
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

            difficultyLayout {
                ForEach(Difficulty.allCases) { diff in
                    Button {
                        withAnimation(reduceMotion ? nil : Theme.quickSpring) {
                            selectedDifficulty = diff
                        }
                    } label: {
                        VStack(spacing: 6) {
                            Text(diff.label)
                                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                            Text("\(diff.letterCount) letters")
                                .font(Theme.captionFont)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 14)
                        .foregroundStyle(diff.color)
                        .background(
                            RoundedRectangle(cornerRadius: 12, style: .continuous)
                                .fill(diff.color.opacity(0.1))
                        )
                        .overlay(
                            RoundedRectangle(cornerRadius: 12, style: .continuous)
                                .stroke(selectedDifficulty == diff ? diff.color : Color.clear, lineWidth: 2)
                        )
                    }
                    .buttonStyle(.plain)
                    .accessibilityAddTraits(selectedDifficulty == diff ? .isSelected : [])
                }
            }
        }
        .cardStyle()
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 40)
    }

    private var difficultyLayout: AnyLayout {
        if dynamicTypeSize.isAccessibilitySize {
            AnyLayout(VStackLayout(spacing: 10))
        } else {
            AnyLayout(HStackLayout(spacing: 10))
        }
    }

    // MARK: - Play Button

    // Racing others on the games hub; needs the network, unlike the
    // single-player modes above.
    private var raceButton: some View {
        NavigationLink {
            RaceLobbyView()
        } label: {
            HStack(spacing: 10) {
                Image(systemName: "person.3.fill")
                    .font(.system(size: 16))
                Text("Race friends")
                    .font(.system(size: 18, weight: .semibold, design: .rounded))
            }
            .foregroundStyle(Theme.accent)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 16)
            .background(
                RoundedRectangle(cornerRadius: 16, style: .continuous)
                    .stroke(Theme.accent.opacity(0.5), lineWidth: 1.5)
            )
        }
        .buttonStyle(.plain)
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 50)
    }

    private var playButton: some View {
        Button {
            game.startNewGame(mode: selectedMode, difficulty: selectedDifficulty)
            showGame = true
        } label: {
            Label("Play", systemImage: "play.fill")
        }
        .buttonStyle(PrimaryActionStyle())
        .disabled(game.isLoading || game.graph == nil)
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
