import SwiftUI

/// The quick race's waiting screen: a ring draining with the seek's
/// deadline around a word ladder that climbs itself, then the opponent and
/// a big 3, 2, 1. QuickRace decides what it says; this only draws it.
struct QuickRaceView: View {
    let session: RaceSession
    let table: WordchainView
    let nowMs: Int64

    @Environment(\.gameFeedback.reduceMotion) private var reduceMotion

    private var model: QuickRace { QuickRace(view: table, viewer: session.state.playerId ?? "") }
    private var stage: QuickRace.Stage { model.stage(nowMs: nowMs) }
    private var connected: Bool { session.connection == .connected }

    var body: some View {
        VStack(spacing: Theme.sectionSpacing) {
            Spacer(minLength: 0)
            ZStack {
                Circle().stroke(Color.secondary.opacity(0.15), lineWidth: 6)
                Circle()
                    .trim(from: 0, to: model.ringLeft(nowMs: nowMs))
                    .stroke(Theme.accent, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                    .rotationEffect(.degrees(-90))
                    .animation(reduceMotion ? nil : .linear(duration: 0.25), value: model.ringLeft(nowMs: nowMs))
                if case let .starting(_, count) = stage {
                    Text("\(count)")
                        .font(.system(size: 96, weight: .bold, design: .rounded).monospacedDigit())
                        // Under Reduce Motion, just the number.
                        .contentTransition(reduceMotion ? .identity : .numericText(countsDown: true))
                        .animation(reduceMotion ? nil : .spring(duration: 0.4), value: count)
                } else {
                    LadderTicker(letters: model.ladderLetters, running: stage != .expired)
                }
            }
            .frame(width: 260, height: 260)
            .accessibilityHidden(true)

            Text(model.status(nowMs: nowMs))
                .font(Theme.headlineFont)
                .multilineTextAlignment(.center)
                .accessibilityLabel(model.spoken(nowMs: nowMs))
                .accessibilityAddTraits(.updatesFrequently)

            actions
            if let notice = session.state.notice {
                Text(notice).font(Theme.captionFont).foregroundStyle(Theme.errorRed)
            }
            Spacer(minLength: 0)
        }
        .padding(.horizontal, Theme.cardPadding)
        .navigationTitle("Quick race")
        .onChange(of: announcement) { _, said in
            #if os(iOS)
            if let said { AccessibilityNotification.Announcement(said).post() }
            #endif
        }
    }

    /// A match or an expiry is said aloud once, as it happens.
    private var announcement: String? {
        switch stage {
        case .starting, .expired: return model.spoken(nowMs: nowMs)
        default: return nil
        }
    }

    @ViewBuilder
    private var actions: some View {
        switch stage {
        case .expired:
            VStack(spacing: 12) {
                Button {
                    session.seek(letters: session.state.seekLetters)
                } label: {
                    Text("Keep looking").frame(maxWidth: .infinity)
                }
                .buttonStyle(PrimaryActionStyle())
                .disabled(!connected || session.state.opening)
                Button("Stop", role: .cancel) { session.leaveRoom() }
                    .disabled(!connected)
            }
        case .looking:
            Button("Stop looking", role: .cancel) { session.leaveRoom() }
                .disabled(!connected)
        case .starting, .racing:
            EmptyView()
        }
    }
}

/// A word ladder that plays itself, a rung about every 1.5 s: the letter
/// that changes lights green and flips in. Under Reduce Motion the ladder
/// stands whole and its rungs fade in, slower, with no flips. It runs only
/// while it is on screen.
private struct LadderTicker: View {
    let letters: Int
    let running: Bool

    @Environment(\.gameFeedback.reduceMotion) private var reduceMotion
    @State private var tour: LadderTour?
    @State private var ladder: [String] = []
    @State private var rung = 0

    var body: some View {
        Group {
            if reduceMotion {
                VStack(spacing: 6) {
                    ForEach(Array(ladder.enumerated()), id: \.offset) { index, word in
                        Text(word.uppercased())
                            .font(Theme.smallTileFont)
                            .opacity(index <= rung ? 1 : 0)
                            .animation(.easeIn(duration: 0.6), value: rung)
                    }
                }
            } else if let word = ladder[safe: rung] {
                HStack(spacing: Theme.tileSpacing) {
                    ForEach(Array(word.enumerated()), id: \.offset) { index, letter in
                        let changed = rung > 0 && ladder[rung - 1][safe: index] != letter
                        Text(String(letter).uppercased())
                            .font(Theme.tileFont)
                            .frame(width: Theme.tileSize, height: Theme.tileSize)
                            .background(
                                RoundedRectangle(cornerRadius: 10)
                                    .fill(changed ? Theme.chainStart.opacity(0.25) : Theme.tertiaryBackground)
                            )
                            .foregroundStyle(changed ? Theme.chainStart : .primary)
                            .id("\(rung)-\(index)-\(letter)")
                            .transition(changed ? .asymmetric(insertion: .flip, removal: .opacity) : .identity)
                    }
                }
                .animation(.easeInOut(duration: 0.5), value: rung)
            }
        }
        .task(id: running) {
            guard running else { return }
            if tour == nil { tour = await Self.loadTour(letters: letters) }
            if ladder.isEmpty { advance() }
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: UInt64((reduceMotion ? 3.0 : 1.5) * 1_000_000_000))
                guard !Task.isCancelled else { return }
                step()
            }
        }
    }

    private func step() {
        if rung + 1 < ladder.count {
            rung += 1
        } else {
            advance()
        }
    }

    /// The next ladder, from where this one ended.
    private func advance() {
        guard let next = tour?.next() else { return }
        ladder = next
        rung = 0
    }

    /// The bundled graph and puzzle words, read off the main thread.
    private static func loadTour(letters: Int) async -> LadderTour? {
        await Task.detached(priority: .utility) {
            guard let graph = WordGraph.loadBundled() else { return nil }
            return LadderTour(graph: graph, puzzleWords: PuzzleWords.load(from: .main), letters: letters,
                              seed: UInt64.random(in: 0...UInt64.max))
        }.value
    }
}

private extension AnyTransition {
    /// A tile turning over to its new letter.
    static var flip: AnyTransition {
        .modifier(active: FlipModifier(angle: -90), identity: FlipModifier(angle: 0))
    }
}

private struct FlipModifier: ViewModifier {
    let angle: Double
    func body(content: Content) -> some View {
        content.rotation3DEffect(.degrees(angle), axis: (x: 1, y: 0, z: 0)).opacity(angle == 0 ? 1 : 0)
    }
}

private extension Array {
    subscript(safe index: Int) -> Element? { indices.contains(index) ? self[index] : nil }
}

private extension String {
    subscript(safe index: Int) -> Character? {
        guard index >= 0, index < count else { return nil }
        return self[self.index(startIndex, offsetBy: index)]
    }
}
