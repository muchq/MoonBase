import SwiftUI

/// The one screen a Time Attack sprint ends on.
struct TimeAttackSummaryView: View {
    let summary: TimeAttackSummary
    let difficulty: Difficulty
    let onReplay: () -> Void
    let onMenu: () -> Void

    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @State private var animateIn = false

    var body: some View {
        VStack(spacing: Theme.sectionSpacing) {
            Spacer()

            VStack(spacing: 8) {
                Image(systemName: summary.isNewBest ? "trophy.fill" : "timer")
                    .font(.system(size: 56))
                    .foregroundStyle(summary.isNewBest ? Theme.warningOrange : Theme.gradientStart)
                    .accessibilityHidden(true)
                Text("Time's up")
                    .font(Theme.titleFont)
                    .foregroundStyle(Theme.headerGradient)
                    .accessibilityAddTraits(.isHeader)
                Text("\(difficulty.label) · \(TimeAttack.seconds(TimeAttack.length(for: difficulty))) seconds")
                    .font(Theme.bodyFont)
                    .foregroundStyle(.secondary)
            }

            VStack(spacing: 16) {
                HStack(spacing: 0) {
                    stat(value: "\(summary.solved)", label: "Solved")
                    Divider().frame(height: 44)
                    stat(value: "\(summary.score)", label: "Points")
                    Divider().frame(height: 44)
                    stat(value: "\(summary.best)", label: "Best")
                }
                if summary.isNewBest {
                    Text("New personal best!")
                        .font(.system(size: 15, weight: .semibold, design: .rounded))
                        .foregroundStyle(Theme.warningOrange)
                        .padding(.horizontal, 16)
                        .padding(.vertical, 8)
                        .background(Capsule().fill(Theme.warningOrange.opacity(0.12)))
                }
            }
            .cardStyle()
            .accessibilityElement(children: .combine)
            .accessibilityIdentifier("time-attack-summary")

            VStack(spacing: 12) {
                Button(action: onReplay) {
                    HStack(spacing: 8) {
                        Image(systemName: "arrow.clockwise")
                        Text("Beat your best")
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
                .accessibilityIdentifier("time-attack-replay")

                Button(action: onMenu) {
                    Text("Back to Menu")
                        .font(.system(size: 16, weight: .medium, design: .rounded))
                        .foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }

            Spacer()
        }
        .padding(.horizontal, Theme.cardPadding)
        .background(Theme.secondaryBackground.ignoresSafeArea())
        .opacity(animateIn ? 1 : 0)
        .scaleEffect(animateIn || reduceMotion ? 1 : 0.95)
        .onAppear {
            withAnimation(reduceMotion ? nil : Theme.springAnimation) { animateIn = true }
            #if os(iOS)
            AccessibilityNotification.Announcement(spoken).post()
            #endif
            #if canImport(UIKit)
            UINotificationFeedbackGenerator().notificationOccurred(summary.isNewBest ? .success : .warning)
            #endif
        }
    }

    private var spoken: String {
        "Time's up. \(summary.solved) solved, \(summary.score) points"
            + (summary.isNewBest ? ". New personal best." : ", best \(summary.best).")
    }

    private func stat(value: String, label: String) -> some View {
        VStack(spacing: 6) {
            Text(value)
                .font(.system(size: 28, weight: .bold, design: .rounded))
                .monospacedDigit()
            Text(label)
                .font(Theme.captionFont)
                .foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
    }
}
