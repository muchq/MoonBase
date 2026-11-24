import SwiftUI

/// A word ladder race at a hub table: everyone climbs the same puzzle on
/// one clock. The hub checks every word and says why it refuses one, so
/// this only draws the view: the viewer's own ladder while racing, everyone
/// else's as a step count until the race is over. It reads the same as the
/// web table (muchq.github.io WordchainTable).
struct RaceTableView: View {
    let session: RaceSession
    let table: WordchainView

    @State private var draft = ""
    @State private var letters = 4
    @State private var confirmingLeave = false
    @FocusState private var typing: Bool

    private var viewer: String { session.state.playerId ?? "" }
    /// Every decision this screen makes; tested in RaceTableTests.
    private var model: RaceTable { RaceTable(view: table, viewer: viewer) }
    private var me: WordchainRunner? { model.me }
    private var ladder: [String] { model.ladder }
    private var racing: Bool { model.racing }
    private var climbing: Bool { model.climbing }
    private var able: Bool { session.connection == .connected && !session.state.sending }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                if let hint = model.hint { Text(hint).font(Theme.bodyFont).foregroundStyle(.secondary) }
                if let result = table.result {
                    Text(model.said(result))
                        .font(Theme.headlineFont)
                        .foregroundStyle(Theme.warningOrange)
                }
                puzzle
                if racing, me != nil { myLadder }
                if let notice = session.state.notice {
                    Text(notice).font(Theme.captionFont).foregroundStyle(Theme.errorRed)
                }
                runnersCard
                actions
            }
            .padding(.horizontal, Theme.cardPadding)
            .padding(.vertical, 16)
        }
        .navigationTitle(model.title)
        .toolbar {
            if table.phase != .closed {
                ToolbarItem(placement: .primaryAction) {
                    Button("Leave") {
                        if model.asksBeforeLeaving { confirmingLeave = true } else { session.leaveTable() }
                    }
                    .disabled(session.connection != .connected)
                }
            }
        }
        .confirmationDialog("Leave the race?", isPresented: $confirmingLeave, titleVisibility: .visible) {
            Button("Leave the race", role: .destructive) { session.leaveTable() }
            Button("Stay", role: .cancel) {}
        }
        .onChange(of: model.draftKey) { _, _ in
            draft = ""
            session.dismissNotice()
        }
        .onChange(of: model.lastLetters, initial: true) { _, last in
            if let last { letters = last }
        }
        .onChange(of: climbing, initial: true) { _, now in typing = now }
        .onChange(of: draft) { _, typed in
            let capped = model.capped(typed)
            if capped != typed { draft = capped }
        }
        // What a sighted player reads in passing, VoiceOver says: why a
        // word was refused, and how the race ended.
        .onChange(of: session.state.notice) { _, notice in announce(notice) }
        .onChange(of: table.result) { _, result in
            announce(result.map(model.said))
        }
    }

    @ViewBuilder
    private var puzzle: some View {
        if let start = table.start, let target = table.target {
            HStack(alignment: .firstTextBaseline, spacing: 10) {
                Text(start.uppercased()).font(.system(size: 26, weight: .bold, design: .monospaced))
                Image(systemName: "arrow.right").accessibilityLabel("to")
                Text(target.uppercased()).font(.system(size: 26, weight: .bold, design: .monospaced))
                Spacer()
                if let deadline = table.deadlineMs, racing {
                    TimelineView(.periodic(from: .now, by: 0.25)) { context in
                        let now = Int64(context.date.timeIntervalSince1970 * 1000)
                        Text("\(RaceText.clock(msLeft: deadline - now)) left")
                            .font(.system(size: 20, weight: .semibold, design: .rounded).monospacedDigit())
                            .foregroundStyle(Theme.warningOrange)
                    }
                }
            }
            if let par = table.par {
                Text("par \(RaceText.steps(RaceText.parSteps(par)))").font(Theme.captionFont).foregroundStyle(.secondary)
            }
        }
    }

    private var myLadder: some View {
        VStack(alignment: .leading, spacing: 12) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(Array(([table.start ?? ""] + ladder).enumerated()), id: \.offset) { _, word in
                        Text(word.uppercased())
                            .font(.system(size: 16, weight: .medium, design: .monospaced))
                            .padding(.horizontal, 10)
                            .padding(.vertical, 6)
                            .background(RoundedRectangle(cornerRadius: 8).stroke(Color.secondary.opacity(0.5)))
                    }
                }
            }
            .accessibilityElement(children: .combine)
            .accessibilityLabel("Your ladder: \(([table.start ?? ""] + ladder).joined(separator: ", "))")

            if climbing {
                HStack(spacing: 8) {
                    TextField("Next word", text: $draft)
                        .font(.system(size: 20, design: .monospaced))
                        .typedAs(.lowercase)
                        .submitLabel(.go)
                        .focused($typing)
                        .textFieldStyle(.roundedBorder)
                        .onSubmit { if able { session.play(draft) } }
                    Button("Play") { session.play(draft) }
                        .buttonStyle(.borderedProminent)
                        .disabled(!able || draft.trimmingCharacters(in: .whitespaces).isEmpty)
                }
                Button("Take back") { session.retract() }
                    .buttonStyle(.bordered)
                    .disabled(!able || ladder.isEmpty)
            }
        }
    }

    private var runnersCard: some View {
        VStack(alignment: .leading, spacing: 10) {
            ForEach(table.runners, id: \.playerId) { runner in
                HStack(alignment: .top) {
                    VStack(alignment: .leading, spacing: 2) {
                        Text(RaceText.you(runner.playerId, viewer: viewer))
                            .font(.system(size: 16, weight: .semibold, design: .rounded))
                            .foregroundStyle(runner.playerId == viewer ? Theme.gradientStart : .primary)
                        Text(line(runner)).font(Theme.captionFont).foregroundStyle(.secondary)
                        if !racing, let rungs = runner.ladder, !rungs.isEmpty {
                            Text(([table.start ?? ""] + rungs).joined(separator: " → "))
                                .font(.system(size: 13, design: .monospaced))
                                .foregroundStyle(.secondary)
                        }
                    }
                    Spacer()
                    if model.showsWins {
                        Text("\(runner.wins) \(runner.wins == 1 ? "win" : "wins")").font(Theme.captionFont)
                    }
                }
                .accessibilityElement(children: .combine)
            }
        }
        .cardStyle()
    }

    private func line(_ runner: WordchainRunner) -> String {
        let climbed = runner.finishedAtMs != nil ? "finished in \(RaceText.steps(runner.words))" : RaceText.steps(runner.words)
        return runner.left ? "\(climbed) · left" : climbed
    }

    @ViewBuilder
    private var actions: some View {
        switch table.phase {
        case .waiting, .over:
            VStack(spacing: 12) {
                Picker("Letters", selection: $letters) {
                    Text("3 letters").tag(3)
                    Text("4 letters").tag(4)
                    Text("5 letters").tag(5)
                }
                .pickerStyle(.segmented)
                Button {
                    session.start(letters: letters)
                } label: {
                    Text(model.startLabel).frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(session.connection != .connected)
            }
        case .closed:
            VStack(spacing: 12) {
                Button {
                    session.openTable()
                } label: {
                    Text(session.state.opening ? "Opening…" : "Play again").frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .disabled(session.connection != .connected || session.state.opening)
                Button("Back to the room") { session.leaveTable() }
            }
        case .racing:
            EmptyView()
        }
    }

    private func announce(_ text: String?) {
        #if os(iOS)
        if let text { AccessibilityNotification.Announcement(text).post() }
        #endif
    }
}
