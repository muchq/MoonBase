import SwiftUI

/// Racing other people on the games hub: a room, created here or joined
/// by the code the web lobby shows, and its race tables. No chat. Seated
/// at a table, the table takes the screen. Single-player stays on the
/// menu, whatever the network does.
struct RaceLobbyView: View {
    @State private var session = RaceSession(transport: HubSocket())
    @State private var code = ""
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        Group {
            if let table = session.state.table {
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
        // A backgrounded app's socket is gone by the time it is back; the
        // resume token takes the seat again.
        .onChange(of: scenePhase) { _, phase in
            if phase == .active { session.connect() }
        }
        .onChange(of: session.state.notice) { _, notice in
            #if os(iOS)
            if let notice, session.state.table == nil { AccessibilityNotification.Announcement(notice).post() }
            #endif
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
            Text("Race friends up the same ladder, on one clock. Fewest steps wins. Alone at a table, practise.")
                .font(Theme.bodyFont)
            Button {
                session.createRoom()
            } label: {
                Text("Create a room").frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
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
            .buttonStyle(.borderedProminent)
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
