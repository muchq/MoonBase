import Foundation
import Observation

/// Everything the race screens show, as the hub's frames leave it. Pure:
/// each frame in, the next state out.
struct RaceState: Equatable {
    var playerId: String?
    var room: HubRoom?
    var table: WordchainView?
    /// A word or a retract sent and not yet answered.
    var sending = false
    /// A table asked for and not yet arrived.
    var opening = false
    /// The last thing to tell the player: the hub's reason for a refusal.
    var notice: String?

    /// The room's race tables, in the hub's order.
    var raceTables: [HubGameSummary] { room?.games.filter { $0.game == "wordchain" } ?? [] }

    mutating func apply(_ frame: HubFrame) {
        switch frame {
        case let .sessionReady(ready):
            playerId = ready.playerId
            // A seat that survived the drop comes back as its own
            // gameJoined; one that didn't sends nothing.
            table = nil
            settle()
            if ready.roomId != room?.roomId { room = nil }
        case let .roomState(next):
            room = next
        case .roomLeft:
            room = nil
            table = nil
            settle()
        case let .commandRejected(reason):
            settle()
            notice = reason
        case let .wordchain(.view(view)):
            table = view
            settle()
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
                if status != .connected { self.state.settle() }
            },
            frame: { [weak self] frame in self?.receive(frame) }
        )
    }

    // A refusal that ends the stream reaches the player as the transport's
    // status; the state has nothing to do with it.
    private func receive(_ frame: HubFrame) {
        state.apply(frame)
    }

    func connect() { transport.connect() }
    func disconnect() { transport.disconnect() }
    /// Back in the foreground; see Dialer.resume.
    func resume() { transport.resume() }
    func dismissNotice() { state.notice = nil }

    // MARK: - The room

    func createRoom() { transport.send(.createRoom) }

    /// Room codes are typed as the web lobby shows them.
    func joinRoom(_ code: String) {
        let roomId = code.trimmingCharacters(in: .whitespacesAndNewlines).uppercased()
        guard !roomId.isEmpty else {
            state.notice = "Enter a room code"
            return
        }
        transport.send(.joinRoom(roomId))
    }

    func leaveRoom() { transport.send(.leaveRoom) }

    // MARK: - The table

    /// A new table: from the room, or once this one has closed.
    func openTable() {
        state.opening = true
        transport.send(.wordchain(.createGame))
    }

    func joinTable(_ gameId: String) {
        state.opening = true
        transport.send(.wordchain(.joinGame(gameId)))
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

    /// A closed table is already gone from the hub: only the view lingers.
    /// Once Play again has asked for a new table, though, that one is on its
    /// way and would seat the player after they left: Back waits for it.
    func leaveTable() {
        guard let table = state.table, table.phase != .closed else {
            if !state.opening { state.table = nil }
            return
        }
        transport.send(.wordchain(.leaveGame))
    }

}

/// The web's: 2 s apart, ten times, inside the hub's 5-minute grace;
/// admission starts the count over.
struct ReconnectPolicy {
    static let delay: TimeInterval = 2
    static let attempts = 10
    private var tried = 0

    /// The wait before the next dial, or nil once it is time to give up.
    mutating func next() -> TimeInterval? {
        guard tried < Self.attempts else { return nil }
        tried += 1
        return Self.delay
    }

    mutating func admitted() { tried = 0 }
}
