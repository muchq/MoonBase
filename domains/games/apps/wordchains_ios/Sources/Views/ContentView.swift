import SwiftUI

struct ContentView: View {
    @State private var game = GameState(sprintHints: ProcessInfo.processInfo.arguments.contains("-sprintHints"))
    @State private var showsSettings = false
    @AppStorage(GameFeedback.hapticsKey) private var hapticsEnabled = true
    @AppStorage(GameFeedback.reduceMotionKey) private var prefersReducedMotion = false
    @Environment(\.accessibilityReduceMotion) private var systemReduceMotion

    var body: some View {
        Group {
            if game.isLoading {
                loadingView
            } else if game.graph == nil {
                errorView
            } else {
                MenuView(game: game, openSettings: { showsSettings = true })
                    .sheet(isPresented: $showsSettings) {
                        SettingsView(hapticsEnabled: $hapticsEnabled, prefersReducedMotion: $prefersReducedMotion)
                    }
            }
        }
        .environment(\.gameFeedback, GameFeedback(
            hapticsEnabled: hapticsEnabled,
            prefersReducedMotion: prefersReducedMotion,
            systemReduceMotion: systemReduceMotion
        ))
        .tint(Theme.accent)
        .onAppear {
            game.loadGraph()
        }
    }

    private var loadingView: some View {
        LoadingFractal()
            .frame(width: 52, height: 52)
            .accessibilityHidden(true)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Theme.secondaryBackground.ignoresSafeArea())
    }

    private var errorView: some View {
        VStack(spacing: 16) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 48))
                .foregroundStyle(Theme.warningOrange)

            Text("Could not load word graph")
                .font(Theme.headlineFont)

            Text("Make sure word_graph.json is bundled with the app.")
                .font(Theme.bodyFont)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)

            Button("Retry") {
                game.loadGraph()
            }
            .font(Theme.bodyFont)
            .foregroundStyle(Theme.accent)
        }
        .padding(Theme.cardPadding)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Theme.secondaryBackground.ignoresSafeArea())
    }
}

private struct LoadingFractal: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30, paused: reduceMotion)) { timeline in
            // Ease between a shallow outline and a deeper snowflake every six seconds.
            let phase = timeline.date.timeIntervalSinceReferenceDate * (2 * Double.pi / 6)
            let depth = reduceMotion ? 0.65 : 0.65 + 0.35 * sin(phase)

            Canvas { context, size in
                let center = CGPoint(x: size.width / 2, y: size.height / 2)
                let radius = min(size.width, size.height) * 0.43
                let sideOffset = radius * sqrt(3.0) / 2
                let top = CGPoint(x: center.x, y: center.y - radius)
                let right = CGPoint(x: center.x + sideOffset, y: center.y + radius / 2)
                let left = CGPoint(x: center.x - sideOffset, y: right.y)

                var path = Path()
                path.move(to: top)
                for (start, end) in [(top, right), (right, left), (left, top)] {
                    appendEdge(from: start, to: end, depth: CGFloat(depth), levels: 2, to: &path)
                }
                path.closeSubpath()
                context.stroke(path, with: .color(Theme.accent), style: StrokeStyle(lineWidth: 1.5, lineJoin: .round))
            }
        }
    }

    // Replace each edge with four smaller edges, using the same bend at each level.
    private func appendEdge(from start: CGPoint, to end: CGPoint, depth: CGFloat, levels: Int, to path: inout Path) {
        guard levels > 0 else {
            path.addLine(to: end)
            return
        }

        let dx = end.x - start.x
        let dy = end.y - start.y
        let first = CGPoint(x: start.x + dx / 3, y: start.y + dy / 3)
        let last = CGPoint(x: end.x - dx / 3, y: end.y - dy / 3)
        let height = depth * sqrt(3.0) / 6
        let peak = CGPoint(x: start.x + dx / 2 + dy * height, y: start.y + dy / 2 - dx * height)

        for (a, b) in [(start, first), (first, peak), (peak, last), (last, end)] {
            appendEdge(from: a, to: b, depth: depth, levels: levels - 1, to: &path)
        }
    }
}
