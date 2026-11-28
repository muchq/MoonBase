import Foundation
import Observation

/// Everything the race screens show, as the hub's frames leave it. Pure:
/// each frame in, the next state out.
struct RaceState: Equatable {
    enum RoomRequest: Equatable { case create, join, leave }
    var roomRequest: RoomRequest?
    var playerId: String?
    var room: HubRoom?
    var table: WordchainView?
    /// A word or a retract sent and not yet answered.
    var sending = false
    /// A table asked for and not yet arrived.
    var opening = false
    /// The last thing to tell the player: the hub's reason for a refusal.
    var notice: String?
    /// Counts refused word actions, even when the reason stays the same.
    private(set) var wordRefusals = 0
    /// The letters the last quick race sought; nil for any.
    var seekLetters: Int?

    /// In the hub's quick race pool, rather than a room of one's own.
    var inQuickRace: Bool { room?.roomId == HubEndpoints.quickRoomId }

    /// The room's race tables, in the hub's order.
    var raceTables: [HubGameSummary] { room?.games.filter { $0.game == "wordchain" } ?? [] }

    /// The public hub can host several games in a room. The iOS room
    /// takes the first race with an open seat instead of listing games.
    var availableRace: HubGameSummary? {
        raceTables.first { $0.status == "waiting" && $0.playerCount < 4 }
    }

    var raceEntryBlocked: Bool { !raceTables.isEmpty && availableRace == nil }

    var roomSeat: HubTable? {
        room?.players.first { $0.playerId == playerId }?.table
    }

    mutating func apply(_ frame: HubFrame) {
        switch frame {
        case let .sessionReady(ready):
            playerId = ready.playerId
            roomRequest = nil
            notice = nil
            // A seat that survived the drop comes back as its own
            // gameJoined; one that didn't sends nothing.
            table = nil
            settle()
            if ready.roomId != room?.roomId { room = nil }
        case let .roomState(next):
            room = next
            if roomRequest != .leave { roomRequest = nil }
        case .roomLeft:
            roomRequest = nil
            notice = nil
            room = nil
            table = nil
            settle()
        case let .commandRejected(reason):
            if sending { wordRefusals += 1 }
            roomRequest = nil
            settle()
            notice = reason
        case let .wordchain(.view(view)):
            // A broadcast about another runner can arrive before the hub
            // answers our word. Keep waiting while our own rung is unchanged.
            if sending, let previous = table, let playerId, view.phase == .racing,
               view.gameId == previous.gameId, view.startedAtMs == previous.startedAtMs,
               let ownWords = view.runners.first(where: { $0.playerId == playerId })?.words,
               ownWords == previous.runners.first(where: { $0.playerId == playerId })?.words {
                opening = false
            } else {
                settle()
            }
            table = view
        case .wordchain(.left):
            table = nil
            settle()
        case .wordchain(.other), .ignored, .refused:
            break
        }
    }

    mutating func settle() {
        sending = false
        opening = false
    }
}

enum HubConnection: Equatable {
    case idle
    case connecting
    case connected
    /// The hub refused the stream, or reconnecting gave up.
    case lost(String)
}

/// How a transport reports back to its session.
struct HubTransportEvents {
    let status: (HubConnection) -> Void
    let frame: (HubFrame) -> Void
    let sendFailed: () -> Void
}

/// The hub's stream as the session sees it: dial, hang up, send. The real
/// one is HubSocket; tests use a fake.
@MainActor
protocol HubTransport: AnyObject {
    var events: HubTransportEvents? { get set }
    func connect()
    func disconnect()
    func resume()
    func send(_ command: HubCommand)
}

/// The race client: what the player does, sent to the hub, and what the hub
/// sends back, held for the views.
@MainActor
@Observable
final class RaceSession {
    private(set) var state = RaceState()
    private(set) var connection: HubConnection = .idle
    private let transport: HubTransport

    init(transport: HubTransport) {
        self.transport = transport
        transport.events = HubTransportEvents(
            status: { [weak self] status in
                guard let self else { return }
                self.connection = status
                // What was asked for on a stream that dropped never reached
                // the hub: nothing waits on its answer.
                if status != .connected { self.resetRequests() }
            },
            frame: { [weak self] frame in self?.receive(frame) },
            sendFailed: { [weak self] in
                // Never reached the hub: nothing waits on its answer.
                self?.resetRequests()
                self?.state.notice = "Not sent: reconnecting"
            }
        )
    }

    // A refusal that ends the stream reaches the player as the transport's
    // status; the state has nothing to do with it.
    private func receive(_ frame: HubFrame) {
        let roomWasCreated = state.roomRequest == .create
        state.apply(frame)
        // After a failed command, leave retrying to the player. Room
        // broadcasts can arrive before the conditions for joining change.
        if case .roomState = frame, state.table == nil, state.notice == nil {
            enterFriendRace(createIfEmpty: roomWasCreated)
        }
    }

    private func resetRequests() {
        state.roomRequest = nil
        state.settle()
    }

    func connect() { transport.connect() }
    func disconnect() { transport.disconnect() }
    /// Back in the foreground; see Dialer.resume.
    func resume() { transport.resume() }
    func dismissNotice() { state.notice = nil }

    // MARK: - The room

    func createRoom() { requestRoom(.create, command: .createRoom) }

    /// Room codes are typed as the web lobby shows them.
    func joinRoom(_ code: String) {
        let roomId = code.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard !roomId.isEmpty else {
            state.notice = "Enter a room code"
            return
        }
        requestRoom(.join, command: .joinRoom(roomId))
    }

    func leaveRoom() { requestRoom(.leave, command: .leaveRoom) }

    private func requestRoom(_ request: RaceState.RoomRequest, command: HubCommand) {
        guard connection == .connected, state.roomRequest == nil else { return }
        state.roomRequest = request
        state.notice = nil
        transport.send(command)
    }

    // MARK: - The table

    func prepareRace() {
        if state.inQuickRace {
            seek(letters: state.seekLetters)
        } else {
            enterFriendRace(createIfEmpty: true)
        }
    }

    private func enterFriendRace(createIfEmpty: Bool) {
        guard connection == .connected, state.room != nil, !state.inQuickRace,
              state.roomRequest == nil, !state.opening, state.roomSeat == nil,
              state.table == nil || state.table?.phase == .closed else { return }

        let command: WordchainMove
        if let race = state.availableRace {
            command = .joinGame(race.gameId)
        } else if createIfEmpty, state.raceTables.isEmpty {
            command = .createGame
        } else {
            return
        }

        state.notice = nil
        state.opening = true
        transport.send(.wordchain(command))
    }

    /// A quick race on these letters, or any; the hub seats the player in
    /// its pool. From one's own expired seek, it looks again.
    func seek(letters: Int?) {
        guard connection == .connected, state.roomRequest == nil, !state.opening else { return }
        state.notice = nil
        state.seekLetters = letters
        state.opening = true
        transport.send(.wordchain(.seek(letters: letters)))
    }

    func start(letters: Int) { transport.send(.wordchain(.startGame(letters: letters))) }

    func play(_ typed: String) {
        let word = typed.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        guard !word.isEmpty else { return }
        state.sending = true
        transport.send(.wordchain(.play(word)))
    }

    func retract() {
        state.sending = true
        transport.send(.wordchain(.retract))
    }
}

/// The ceiling of each wait: doubling from half a second to thirty, ten
/// times, inside the hub's 5-minute reconnect grace.
struct ReconnectPolicy {
    static let base: TimeInterval = 0.5
    static let cap: TimeInterval = 30
    static let attempts = 10
    /// A connection admitted this long ago has held: its drop starts over.
    static let stable: TimeInterval = 30
    private var tried = 0

    /// The ceiling of the next wait, or nil once it is time to give up.
    mutating func next() -> TimeInterval? {
        guard tried < Self.attempts else { return nil }
        defer { tried += 1 }
        return min(Self.cap, Self.base * pow(2, Double(tried)))
    }

    mutating func reset() { tried = 0 }
}
