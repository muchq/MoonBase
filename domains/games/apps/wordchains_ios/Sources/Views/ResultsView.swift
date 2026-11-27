import SwiftUI

struct ResultsView: View {
    let result: PuzzleResult
    var streak = 0
    var celebrate = true
    var onNewGame: (() -> Void)? = nil
    let onMenu: () -> Void

    @Environment(\.gameFeedback) private var feedback
    @State private var animateIn = false
    @State private var showConfetti = false

    private var reduceMotion: Bool { feedback.reduceMotion }
    private var revealed: Bool { animateIn || !celebrate }

    var body: some View {
        ScrollView {
            VStack(spacing: Theme.sectionSpacing) {
                celebrationHeader
                scoreCard
                chainSummary
                actionButtons
            }
            .frame(maxWidth: .infinity)
            .padding(Theme.cardPadding)
        }
        .background(Theme.secondaryBackground.ignoresSafeArea())
        .overlay(alignment: .top) {
            if showConfetti && !reduceMotion {
                ConfettiView()
                    .allowsHitTesting(false)
                    .accessibilityHidden(true)
            }
        }
        .onAppear {
            guard celebrate else { return }
            withAnimation(reduceMotion ? nil : Theme.springAnimation.delay(0.2)) {
                animateIn = true
            }
            if !reduceMotion {
                withAnimation(.easeOut(duration: 0.5).delay(0.3)) {
                    showConfetti = true
                }
            }
            #if canImport(UIKit)
            feedback.performHaptic { UINotificationFeedbackGenerator().notificationOccurred(.success) }
            #endif
        }
    }

    // MARK: - Header

    private var celebrationHeader: some View {
        VStack(spacing: 8) {
            Text(headerEmoji)
                .font(.system(size: 64))
                .scaleEffect(revealed || reduceMotion ? 1 : 0.3)
                .accessibilityHidden(true)

            Text(headerTitle)
                .font(Theme.titleFont)
                .multilineTextAlignment(.center)
                .accessibilityAddTraits(.isHeader)

            if let start = result.chain.first, let target = result.chain.last {
                Text("\(start) → \(target)")
                    .font(Theme.bodyFont)
                    .foregroundStyle(.secondary)
            }

            if let daily = result.dailyChallenge {
                Text("Daily Challenge · \(daily.difficulty.label) · \(daily.day)")
                    .font(Theme.captionFont)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
            }
        }
        .opacity(revealed ? 1 : 0)
    }

    private var headerEmoji: String {
        if result.isOptimal { return "\u{1F31F}" }
        if result.stepsTaken <= result.optimalSteps + 1 { return "\u{1F389}" }
        return "\u{2705}"
    }

    private var headerTitle: String {
        if result.isOptimal { return "Optimal!" }
        if result.stepsTaken <= result.optimalSteps + 1 { return "Great Job!" }
        return "Complete!"
    }

    // MARK: - Score

    private var scoreCard: some View {
        VStack(spacing: 16) {
            HStack(spacing: 0) {
                scoreItem(
                    value: "\(result.stepsTaken)",
                    label: "Steps",
                    icon: "figure.walk"
                )
                Divider().frame(height: 44)
                scoreItem(
                    value: "\(result.optimalSteps)",
                    label: "Optimal",
                    icon: "star"
                )
                Divider().frame(height: 44)
                scoreItem(
                    value: "+\(result.score)",
                    label: "Points",
                    icon: "flame"
                )
            }

            if streak > 1 {
                HStack(spacing: 6) {
                    Image(systemName: "bolt.fill")
                        .foregroundStyle(Theme.warningOrange)
                    Text("\(streak) streak!")
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
        .opacity(revealed ? 1 : 0)
        .offset(y: revealed ? 0 : 30)
    }

    private func scoreItem(value: String, label: String, icon: String) -> some View {
        VStack(spacing: 6) {
            Image(systemName: icon)
                .font(.system(size: 16))
                .foregroundStyle(Theme.accent)
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

            Text(result.chain.joined(separator: " → "))
                .font(.system(size: 15, weight: .medium, design: .monospaced))
                .foregroundStyle(.primary)
                .lineLimit(nil)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .cardStyle()
        .opacity(revealed ? 1 : 0)
        .offset(y: revealed ? 0 : 40)
    }

    // MARK: - Actions

    private var actionButtons: some View {
        VStack(spacing: 12) {
            if let shareText = result.shareText {
                ShareLink(item: shareText) {
                    Label("Share Result", systemImage: "square.and.arrow.up")
                }
                .buttonStyle(PrimaryActionStyle())
            } else if let onNewGame {
                Button(action: onNewGame) {
                    Label("New Puzzle", systemImage: "arrow.clockwise")
                }
                .buttonStyle(PrimaryActionStyle())
            }

            Button("Back to Menu", action: onMenu)
                .buttonStyle(.bordered)
                .controlSize(.large)
        }
        .opacity(revealed ? 1 : 0)
        .offset(y: revealed ? 0 : 50)
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
        Theme.accent, Theme.gradientEnd, Theme.chainStart,
        Theme.warningOrange, Theme.hintBlue, .pink, .yellow,
    ].randomElement()!
}
