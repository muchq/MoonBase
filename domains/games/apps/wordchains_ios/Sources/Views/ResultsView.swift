import SwiftUI

struct ResultsView: View {
    @Bindable var game: GameState
    let onNewGame: () -> Void
    let onMenu: () -> Void

    @State private var animateIn = false
    @State private var showConfetti = false

    var body: some View {
        VStack(spacing: Theme.sectionSpacing) {
            Spacer()

            celebrationHeader
            scoreCard
            chainSummary
            actionButtons

            Spacer()
        }
        .padding(.horizontal, Theme.cardPadding)
        .background(Theme.secondaryBackground.ignoresSafeArea())
        .overlay(alignment: .top) {
            if showConfetti {
                ConfettiView()
                    .allowsHitTesting(false)
            }
        }
        .onAppear {
            withAnimation(Theme.springAnimation.delay(0.2)) {
                animateIn = true
            }
            withAnimation(.easeOut(duration: 0.5).delay(0.3)) {
                showConfetti = true
            }
            hapticSuccess()
        }
    }

    // MARK: - Header

    private var celebrationHeader: some View {
        VStack(spacing: 8) {
            Text(headerEmoji)
                .font(.system(size: 64))
                .scaleEffect(animateIn ? 1 : 0.3)

            Text(headerTitle)
                .font(Theme.titleFont)
                .foregroundStyle(Theme.headerGradient)

            if let puzzle = game.currentPuzzle {
                Text("\(puzzle.start) \u{2192} \(puzzle.target)")
                    .font(Theme.bodyFont)
                    .foregroundStyle(.secondary)
            }
        }
        .opacity(animateIn ? 1 : 0)
    }

    private var headerEmoji: String {
        if isOptimal { return "\u{1F31F}" }
        if game.chain.count <= (game.currentPuzzle?.optimalLength ?? 0) + 1 { return "\u{1F389}" }
        return "\u{2705}"
    }

    private var headerTitle: String {
        if isOptimal { return "Optimal!" }
        if game.chain.count <= (game.currentPuzzle?.optimalLength ?? 0) + 1 { return "Great Job!" }
        return "Complete!"
    }

    private var isOptimal: Bool {
        guard let puzzle = game.currentPuzzle else { return false }
        return game.chain.count == puzzle.optimalLength
    }

    // MARK: - Score

    private var scoreCard: some View {
        VStack(spacing: 16) {
            HStack(spacing: 0) {
                scoreItem(
                    value: "\(game.chain.count - 1)",
                    label: "Steps",
                    icon: "figure.walk"
                )
                Divider().frame(height: 44)
                scoreItem(
                    value: "\(game.currentPuzzle?.optimalLength ?? 0 - 1)",
                    label: "Optimal",
                    icon: "star"
                )
                Divider().frame(height: 44)
                scoreItem(
                    value: "+\(game.score)",
                    label: "Points",
                    icon: "flame"
                )
            }

            if game.streak > 1 {
                HStack(spacing: 6) {
                    Image(systemName: "bolt.fill")
                        .foregroundStyle(Theme.warningOrange)
                    Text("\(game.streak) streak!")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                        .foregroundStyle(Theme.warningOrange)
                }
                .padding(.horizontal, 16)
                .padding(.vertical, 8)
                .background(
                    Capsule().fill(Theme.warningOrange.opacity(0.12))
                )
            }
        }
        .cardStyle()
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 30)
    }

    private func scoreItem(value: String, label: String, icon: String) -> some View {
        VStack(spacing: 6) {
            Image(systemName: icon)
                .font(.system(size: 16))
                .foregroundStyle(Theme.gradientStart)
            Text(value)
                .font(.system(size: 24, weight: .bold, design: .rounded))
            Text(label)
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }

    // MARK: - Chain Summary

    private var chainSummary: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Your Chain")
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
                .textCase(.uppercase)
                .tracking(1)

            Text(game.chain.joined(separator: " \u{2192} "))
                .font(.system(size: 15, weight: .medium, design: .monospaced))
                .foregroundStyle(.primary)
                .lineLimit(nil)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .cardStyle()
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 40)
    }

    // MARK: - Actions

    private var actionButtons: some View {
        VStack(spacing: 12) {
            Button(action: onNewGame) {
                HStack(spacing: 8) {
                    Image(systemName: "arrow.clockwise")
                    Text("New Puzzle")
                        .font(.system(size: 17, weight: .semibold, design: .rounded))
                }
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 16)
                .background(
                    RoundedRectangle(cornerRadius: 14, style: .continuous)
                        .fill(LinearGradient(
                            colors: [Theme.gradientStart, Theme.gradientEnd],
                            startPoint: .leading, endPoint: .trailing
                        ))
                )
            }
            .buttonStyle(.plain)

            Button(action: onMenu) {
                Text("Back to Menu")
                    .font(.system(size: 16, weight: .medium, design: .rounded))
                    .foregroundStyle(.secondary)
            }
            .buttonStyle(.plain)
        }
        .opacity(animateIn ? 1 : 0)
        .offset(y: animateIn ? 0 : 50)
    }

    private func hapticSuccess() {
        let generator = UINotificationFeedbackGenerator()
        generator.notificationOccurred(.success)
    }
}

// MARK: - Confetti

struct ConfettiView: View {
    @State private var particles: [ConfettiParticle] = (0..<40).map { _ in ConfettiParticle() }
    @State private var animate = false

    var body: some View {
        GeometryReader { geo in
            ZStack {
                ForEach(particles) { particle in
                    Circle()
                        .fill(particle.color)
                        .frame(width: particle.size, height: particle.size)
                        .position(
                            x: animate ? particle.endX * geo.size.width : particle.startX * geo.size.width,
                            y: animate ? geo.size.height + 20 : -20
                        )
                        .opacity(animate ? 0 : 1)
                }
            }
        }
        .onAppear {
            withAnimation(.easeOut(duration: 2.0)) {
                animate = true
            }
        }
    }
}

struct ConfettiParticle: Identifiable {
    let id = UUID()
    let startX = Double.random(in: 0.1...0.9)
    let endX = Double.random(in: 0...1)
    let size = CGFloat.random(in: 4...10)
    let color: Color = [
        Theme.gradientStart, Theme.gradientEnd, Theme.validGreen,
        Theme.warningOrange, Theme.hintBlue, .pink, .yellow,
    ].randomElement()!
}
