import SwiftUI

enum MenuPage: String, CaseIterable, Identifiable {
    case about = "About"
    case settings = "Settings"
    case help = "Help"

    var id: Self { self }

    var icon: String {
        switch self {
        case .about: "info.circle"
        case .settings: "slider.horizontal.3"
        case .help: "questionmark.circle"
        }
    }
}

struct MenuPageSheet<Content: View>: View {
    let page: MenuPage
    @ViewBuilder let content: () -> Content
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            content()
                .navigationTitle(page.rawValue)
                #if os(iOS)
                .navigationBarTitleDisplayMode(.inline)
                #endif
                .toolbar {
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Done") { dismiss() }
                            .accessibilityIdentifier("menu-sheet-done")
                    }
                }
        }
    }
}

struct HelpView: View {
    private let words = ["CAT", "COT", "DOT", "DOG"]

    var body: some View {
        ScrollView {
            VStack(spacing: 16) {
                VStack(alignment: .leading, spacing: 12) {
                    Text("How to play")
                        .font(Theme.headlineFont)
                        .accessibilityAddTraits(.isHeader)
                    Text("Reach the target by changing exactly one letter at a time. Every step must be a real word of the same length that you haven’t used in this chain.")
                        .font(Theme.bodyFont)
                        .foregroundStyle(.secondary)
                    ViewThatFits(in: .horizontal) {
                        HStack(spacing: 8) { exampleWords(arrow: "arrow.forward") }
                        VStack(spacing: 8) { exampleWords(arrow: "arrow.down") }
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 4)
                    .accessibilityElement(children: .ignore)
                    .accessibilityLabel("Example chain: cat, cot, dot, dog.")
                    .accessibilityIdentifier("help-example")
                }
                .cardStyle()

                VStack(alignment: .leading, spacing: 16) {
                    Text("Controls")
                        .font(Theme.headlineFont)
                        .accessibilityAddTraits(.isHeader)
                    control("Submit", icon: "arrow.up", description: "Type the next word, then tap Submit or press Return.")
                    control("Undo", icon: "arrow.uturn.backward", description: "Take back your last word and try a different route.")
                    control("Hint", icon: "lightbulb.fill", description: "In Quick Play, fill in a suggested next word. Submit it when you’re ready.")
                }
                .cardStyle()
            }
            .padding(Theme.cardPadding)
            .frame(maxWidth: 600)
            .frame(maxWidth: .infinity)
        }
        .background(Theme.secondaryBackground.ignoresSafeArea())
    }

    private func exampleWords(arrow: String) -> some View {
        ForEach(words, id: \.self) { word in
            if word != words.first {
                Image(systemName: arrow)
                    .foregroundStyle(.secondary)
                    .accessibilityHidden(true)
            }
            Text(word)
                .font(.system(.headline, design: .monospaced))
                .foregroundStyle(word == words.last ? Theme.chainEnd : .primary)
                .padding(.horizontal, 8)
                .padding(.vertical, 6)
                .background(Theme.secondaryBackground, in: RoundedRectangle(cornerRadius: 8))
        }
    }

    private func control(_ title: String, icon: String, description: String) -> some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: icon)
                .foregroundStyle(Theme.accent)
                .frame(width: 24)
                .accessibilityHidden(true)
            VStack(alignment: .leading, spacing: 4) {
                Text(title)
                    .font(.system(.headline, design: .rounded))
                Text(description)
                    .font(Theme.bodyFont)
                    .foregroundStyle(.secondary)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .accessibilityElement(children: .combine)
    }
}

struct AboutView: View {
    private var version: String? { Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Word Chains")
                        .font(Theme.titleFont)
                        .accessibilityAddTraits(.isHeader)
                    if let version {
                        Text("Version \(version)")
                            .font(Theme.captionFont)
                            .foregroundStyle(.secondary)
                    }
                }
                Text("Play a quick puzzle, solve the Daily Challenge, race the clock in Time Attack, or race friends online.")
                    .font(Theme.bodyFont)
                    .foregroundStyle(.secondary)
                Link(destination: URL(string: "https://github.com/muchq/MoonBase/tree/main/domains/games/apps/wordchains_ios")!) {
                    Label("Source code", systemImage: "arrow.up.right")
                        .font(.system(.headline, design: .rounded))
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .cardStyle()
            .padding(Theme.cardPadding)
            .frame(maxWidth: 600)
            .frame(maxWidth: .infinity)
        }
        .background(Theme.secondaryBackground.ignoresSafeArea())
    }
}
