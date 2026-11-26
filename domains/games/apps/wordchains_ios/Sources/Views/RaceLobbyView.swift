import SwiftUI

/// Racing other people on the games hub: a room, created here or joined
/// by the code the web lobby shows, and its race tables. No chat. Seated
/// at a table, the table takes the screen. Single-player stays on the
/// menu, whatever the network does.
struct RaceLobbyView: View {
    @State private var session = RaceSession(transport: HubSocket())
    @State private var code = ""
    /// The quick race's letters; nil for any.
    @State private var quickLetters: Int?
    @State private var backgrounded = false
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        Group {
            if session.state.inQuickRace {
                quickRace
            } else if let table = session.state.table {
                RaceTableView(session: session, table: table)
            } else {
                lobby
            }
        }
        .background(Theme.secondaryBackground.ignoresSafeArea())
        .navigationTitle("Race")
        .inlineTitle()
        .onAppear { session.connect() }
        .onDisappear { session.disconnect() }
        // A backgrounded app's socket may be dead without having said so;
        // back in the foreground, dial afresh and the resume token takes the
        // seat again. A glance at Control Center only goes inactive.
        .onChange(of: scenePhase) { _, phase in
            if phase == .background { backgrounded = true }
            if phase == .active, backgrounded {
                backgrounded = false
                session.resume()
            }
        }
        .onChange(of: session.state.notice) { _, notice in
            #if os(iOS)
            if let notice, session.state.table == nil { AccessibilityNotification.Announcement(notice).post() }
            #endif
        }
    }

    /// In the pool: the waiting screen until the race's first word, then
    /// the race table.
    private var quickRace: some View {
        TimelineView(.periodic(from: .now, by: 0.25)) { context in
            let now = Int64(context.date.timeIntervalSince1970 * 1000)
            if let table = session.state.table {
                if QuickRace(view: table, viewer: session.state.playerId ?? "").waits(nowMs: now) {
                    QuickRaceView(session: session, table: table, nowMs: now)
                } else {
                    RaceTableView(session: session, table: table)
                }
            } else {
                // Between tables: a seek on its way, or the race's end.
                VStack(spacing: 16) {
                    ProgressView("Finding a player…")
                    Button("Stop", role: .cancel) { session.leaveRoom() }
                }
            }
        }
    }

    private var lobby: some View {
        ScrollView {
            VStack(spacing: Theme.sectionSpacing) {
                status
                if let room = session.state.room {
                    roomCard(room)
                } else {
                    noRoomCard
                }
                if let notice = session.state.notice {
                    Text(notice)
                        .font(Theme.captionFont)
                        .foregroundStyle(Theme.errorRed)
                        .accessibilityAddTraits(.updatesFrequently)
                }
            }
            .padding(.horizontal, Theme.cardPadding)
            .padding(.vertical, 20)
        }
    }

    @ViewBuilder
    private var status: some View {
        switch session.connection {
        case .idle, .connecting:
            HStack(spacing: 8) {
                ProgressView()
                Text("Connecting to the games hub…").font(Theme.captionFont).foregroundStyle(.secondary)
            }
        case .connected:
            if let playerId = session.state.playerId {
                Text("Playing as \(playerId)").font(Theme.captionFont).foregroundStyle(.secondary)
            }
        case let .lost(reason):
            VStack(spacing: 8) {
                Text(reason).font(Theme.captionFont).foregroundStyle(Theme.errorRed)
                Button("Try again") { session.connect() }
            }
        }
    }

    private var connected: Bool { session.connection == .connected }

    private var noRoomCard: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Race someone new, heads-up: the hub finds you a player.")
                .font(Theme.bodyFont)
            Picker("Letters", selection: $quickLetters) {
                Text("Any").tag(Int?.none)
                Text("3").tag(Int?.some(3))
                Text("4").tag(Int?.some(4))
                Text("5").tag(Int?.some(5))
            }
            .pickerStyle(.segmented)
            .accessibilityLabel("Letters")
            Button {
                session.seek(letters: quickLetters)
            } label: {
                Text("Quick race").frame(maxWidth: .infinity)
            }
            .buttonStyle(PrimaryActionStyle())
            .disabled(!connected || session.state.opening)

            Divider()
            Text("Or race friends up the same ladder, on one clock. Fewest steps wins. Alone at a table, practise.")
                .font(Theme.bodyFont)
            Button {
                session.createRoom()
            } label: {
                Text("Create a room").frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(!connected)

            HStack {
                TextField("Room code", text: $code)
                    .typedAs(.characters)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { if connected { session.joinRoom(code) } }
                Button("Join") { session.joinRoom(code) }
                    .buttonStyle(.bordered)
                    .disabled(!connected)
            }
        }
        .cardStyle()
    }

    private func roomCard(_ room: HubRoom) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            HStack {
                Text("Room \(room.roomId)").font(Theme.headlineFont)
                Spacer()
                if let link = URL(string: "https://muchq.com/games/room/\(room.roomId)") {
                    ShareLink(item: link) { Image(systemName: "square.and.arrow.up") }
                        .accessibilityLabel("Share room \(room.roomId)")
                }
            }
            Text("\(room.players.count) in the room").font(Theme.captionFont).foregroundStyle(.secondary)

            let tables = session.state.raceTables
            if tables.isEmpty {
                Text("No race tables yet.").font(Theme.captionFont).foregroundStyle(.secondary)
            }
            ForEach(tables, id: \.gameId) { table in
                HStack {
                    VStack(alignment: .leading) {
                        Text("Table \(table.gameId)").font(.system(size: 16, weight: .semibold, design: .rounded))
                        Text("\(table.status) · \(table.playerCount)/4 seated").font(Theme.captionFont).foregroundStyle(.secondary)
                    }
                    Spacer()
                    if table.status == "waiting" && table.playerCount < 4 {
                        Button("Join") { session.joinTable(table.gameId) }
                            .buttonStyle(.bordered)
                            .disabled(!connected || session.state.opening)
                    }
                }
            }

            Button {
                session.openTable()
            } label: {
                Text("Open a race table").frame(maxWidth: .infinity)
            }
            .buttonStyle(PrimaryActionStyle())
            .disabled(!connected || session.state.opening)

            Button("Leave the room", role: .destructive) { session.leaveRoom() }
                .disabled(!connected)
        }
        .cardStyle()
    }
}

// iOS-only input and title modifiers, so `swift test` still builds the
// views for macOS.
/// How a race screen's text field types: room codes in capitals, words in lowercase.
enum RaceTyping { case characters, lowercase }

extension View {
    func typedAs(_ typing: RaceTyping) -> some View {
        #if os(iOS)
        return self
            .textInputAutocapitalization(typing == .characters ? .characters : .never)
            .autocorrectionDisabled()
        #else
        return self.autocorrectionDisabled()
        #endif
    }

    func inlineTitle() -> some View {
        #if os(iOS)
        return self.navigationBarTitleDisplayMode(.inline)
        #else
        return self
        #endif
    }
}
