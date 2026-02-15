import SwiftUI

struct ChainView: View {
    let chain: [String]
    let target: String
    let difficulty: Difficulty

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
                        .transition(.asymmetric(
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
                withAnimation(Theme.springAnimation) {
                    proxy.scrollTo(chain.count - 1, anchor: .center)
                }
            }
        }
    }

    // MARK: - Word Tile

    private func wordTile(_ word: String, at index: Int) -> some View {
        let progress = chain.count > 1 ? Double(index) / Double(max(chain.count - 1, 1)) : 0
        let isStart = index == 0
        let isLatest = index == chain.count - 1 && !chain.contains(target)

        return HStack(spacing: Theme.tileSpacing) {
            ForEach(Array(word.enumerated()), id: \.offset) { charIdx, char in
                let changed = index > 0 ? isCharChanged(word: word, previous: chain[index - 1], at: charIdx) : false

                Text(String(char).uppercased())
                    .font(difficulty == .hard ? Theme.smallTileFont : Theme.tileFont)
                    .frame(width: tileWidth, height: Theme.tileSize)
                    .foregroundStyle(changed ? .white : .primary)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .fill(changed ? Theme.chainGradient(progress: progress) : Theme.tertiaryBackground)
                    )
                    .overlay(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .stroke(
                                isStart ? Theme.chainStart.opacity(0.5) :
                                    isLatest ? Theme.gradientStart.opacity(0.5) : Color.clear,
                                lineWidth: 2
                            )
                    )
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
                    .foregroundStyle(.secondary.opacity(0.5))
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .strokeBorder(style: StrokeStyle(lineWidth: 2, dash: [6, 4]))
                            .foregroundStyle(Theme.chainEnd.opacity(0.3))
                    )
            }
        }
        .padding(.horizontal, 4)
    }

    // MARK: - Connector

    private func connector(at index: Int) -> some View {
        let progress = chain.count > 1 ? Double(index) / Double(max(chain.count, 1)) : 0.5
        return VStack(spacing: 2) {
            ForEach(0..<3, id: \.self) { dotIdx in
                Circle()
                    .fill(Theme.chainGradient(progress: progress).opacity(index < chain.count ? 1 : 0.3))
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
