import SwiftUI

struct ChainView: View {
    let chain: [String]
    let target: String
    let difficulty: Difficulty
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        ScrollViewReader { proxy in
            ScrollView(.vertical, showsIndicators: false) {
                VStack(spacing: 0) {
                    ForEach(Array(chain.enumerated()), id: \.offset) { index, word in
                        VStack(spacing: 0) {
                            if index > 0 {
                                connector(at: index)
                            }
                            wordTile(word, at: index)
                                .id(index)
                        }
                        .transition(reduceMotion ? .opacity : .asymmetric(
                            insertion: .scale(scale: 0.5).combined(with: .opacity).combined(with: .offset(y: 20)),
                            removal: .opacity
                        ))
                    }

                    if !chain.contains(target) {
                        connector(at: chain.count)
                        targetTile
                    }
                }
                .padding(.vertical, 12)
            }
            .onChange(of: chain.count) {
                withAnimation(reduceMotion ? nil : Theme.springAnimation) {
                    proxy.scrollTo(chain.count - 1, anchor: .center)
                }
            }
        }
    }

    // MARK: - Word Tile

    private func wordTile(_ word: String, at index: Int) -> some View {
        let isStart = index == 0
        let isLatest = index == chain.count - 1 && !chain.contains(target)

        return HStack(spacing: Theme.tileSpacing) {
            ForEach(Array(word.enumerated()), id: \.offset) { charIdx, char in
                let changed = index > 0 ? isCharChanged(word: word, previous: chain[index - 1], at: charIdx) : false

                Text(String(char).uppercased())
                    .font(difficulty == .hard ? Theme.smallTileFont : Theme.tileFont)
                    .frame(width: tileWidth, height: Theme.tileSize)
                    .foregroundStyle(changed ? Theme.accent : .primary)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .fill(changed ? Theme.accent.opacity(0.12) : Theme.tertiaryBackground)
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .stroke(
                                isStart ? Theme.chainStart.opacity(0.5) :
                                    isLatest ? Theme.accent.opacity(0.5) : Color.clear,
                                lineWidth: 2
                            )
                    )
                    .modifier(ChainLetterArrival(animate: changed && !reduceMotion))
            }
        }
        .padding(.horizontal, 4)
    }

    private var targetTile: some View {
        HStack(spacing: Theme.tileSpacing) {
            ForEach(Array(target.enumerated()), id: \.offset) { _, char in
                Text(String(char).uppercased())
                    .font(difficulty == .hard ? Theme.smallTileFont : Theme.tileFont)
                    .frame(width: tileWidth, height: Theme.tileSize)
                    .foregroundStyle(.secondary)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .strokeBorder(style: StrokeStyle(lineWidth: 2, dash: [6, 4]))
                            .foregroundStyle(Theme.chainEnd.opacity(0.65))
                    )
            }
        }
        .padding(.horizontal, 4)
    }

    // MARK: - Connector

    private func connector(at index: Int) -> some View {
        VStack(spacing: 2) {
            ForEach(0..<3, id: \.self) { _ in
                Circle()
                    .fill(Theme.chainEnd.opacity(index < chain.count ? 0.8 : 0.45))
                    .frame(width: 4, height: 4)
            }
        }
        .padding(.vertical, 4)
    }

    // MARK: - Helpers

    private var tileWidth: CGFloat {
        difficulty == .hard ? 40 : Theme.tileSize
    }

    private func isCharChanged(word: String, previous: String, at index: Int) -> Bool {
        guard index < word.count && index < previous.count else { return false }
        let wordIdx = word.index(word.startIndex, offsetBy: index)
        let prevIdx = previous.index(previous.startIndex, offsetBy: index)
        return word[wordIdx] != previous[prevIdx]
    }
}

/// The replaced letter settles into its new rung as the chain grows.
private struct ChainLetterArrival: ViewModifier {
    let animate: Bool
    @State private var arrived = false

    func body(content: Content) -> some View {
        content
            .scaleEffect(animate && !arrived ? 0.65 : 1)
            .offset(y: animate && !arrived ? 12 : 0)
            .onAppear {
                withAnimation(animate ? Theme.bounceAnimation : nil) { arrived = true }
            }
    }
}
