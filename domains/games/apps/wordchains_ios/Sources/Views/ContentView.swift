import SwiftUI

struct ContentView: View {
    @State private var game = GameState()

    var body: some View {
        Group {
            if game.isLoading {
                loadingView
            } else if game.graph == nil {
                errorView
            } else {
                MenuView(game: game)
            }
        }
        .onAppear {
            game.loadGraph()
        }
    }

    private var loadingView: some View {
        VStack(spacing: 20) {
            ProgressView()
                .scaleEffect(1.5)
                .tint(Theme.gradientStart)

            Text("Loading dictionary...")
                .font(Theme.bodyFont)
                .foregroundStyle(.secondary)
        }
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
            .foregroundStyle(Theme.gradientStart)
        }
        .padding(Theme.cardPadding)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Theme.secondaryBackground.ignoresSafeArea())
    }
}
