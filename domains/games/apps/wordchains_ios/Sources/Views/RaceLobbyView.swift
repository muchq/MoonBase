import SwiftUI

/// The friends room and its race share one screen. Quick races use the
/// separate matchmaking flow.
struct RaceLobbyView: View {
    @State private var session = RaceSession(transport: Self.makeTransport())
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
    private var busy: Bool { session.state.roomRequest != nil || session.state.opening }

    private static func makeTransport() -> any HubTransport {
        #if targetEnvironment(simulator)
        if ProcessInfo.processInfo.arguments.contains("-raceRoomUITest") {
            return RaceRoomUITestTransport()
        }
        #endif
        return HubSocket()
    }

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
            .disabled(!connected || busy)

            Divider()
            Text("Create a room to race friends on the same ladder, or join with a room code.")
                .font(Theme.bodyFont)
            Button {
                session.createRoom()
            } label: {
                Text(session.state.roomRequest == .create ? "Creating room…" : "Create a room")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(!connected || busy)

            HStack {
                TextField("Room code", text: $code)
                    .typedAs(.characters)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { if connected && !busy { session.joinRoom(code) } }
                Button("Join") { session.joinRoom(code) }
                    .buttonStyle(.bordered)
                    .disabled(!connected || busy || code.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
        }
        .cardStyle()
    }

    private func roomCard(_ room: HubRoom) -> some View {
        VStack(alignment: .leading, spacing: 14) {
            if session.state.roomRequest == .leave {
                ProgressView("Leaving room…")
            } else if session.state.opening || session.state.roomSeat?.game == "wordchain" {
                ProgressView("Getting the race ready…")
            } else if session.state.roomSeat != nil {
                Text("Leave your current game before joining the race.")
                    .font(Theme.bodyFont)
                    .foregroundStyle(.secondary)
            } else {
                RaceRoomInvite(roomCode: room.roomId, canInvite: !session.state.raceTables.isEmpty)
                if session.state.raceTables.isEmpty || session.state.availableRace != nil {
                    Button(session.state.raceTables.isEmpty ? "Set up race" : "Join race") {
                        session.prepareRace()
                    }
                    .buttonStyle(PrimaryActionStyle())
                    .disabled(!connected)
                } else {
                    Text("The race has started or has no open spots.")
                        .font(Theme.bodyFont)
                        .foregroundStyle(.secondary)
                }
            }

            Button("Leave room", role: .destructive) { session.leaveRoom() }
                .disabled(!connected || session.state.roomRequest != nil)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .cardStyle()
    }
}

struct RaceRoomInvite: View {
    let roomCode: String
    var canInvite = true

    var body: some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text("Room code").font(Theme.captionFont).foregroundStyle(.secondary)
                Text(roomCode)
                    .font(.system(.title2, design: .monospaced).weight(.bold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.6)
                    .accessibilityIdentifier("race-room-code")
            }
            Spacer()
            if canInvite, let link = URL(string: "https://muchq.com/games/room/\(roomCode)") {
                ShareLink(item: link) { Label("Invite friends", systemImage: "square.and.arrow.up") }
                    .buttonStyle(.bordered)
            }
        }
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
