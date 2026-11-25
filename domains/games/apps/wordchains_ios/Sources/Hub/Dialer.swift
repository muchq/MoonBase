import Foundation

/// HubSocket's decisions, kept apart from URLSession so they can be tested:
/// when to dial, when to wait and dial again, when to give up. Every dial
/// and every wait has a generation; news carrying any other generation is
/// from a dial a disconnect, reconnect or retry already replaced, and
/// changes nothing. Waits are a random share of ReconnectPolicy's ceiling,
/// so clients one outage dropped together don't come back together.
struct Dialer {
    enum Effect: Equatable {
        /// Mint a session and open its socket, as this generation.
        case dial(Int)
        /// Wait this long, then report `retryDue` for this generation.
        case wait(Int, TimeInterval)
        /// Close whatever socket is open.
        case hangUp
        case status(HubConnection)
    }

    private enum Phase: Equatable {
        /// `offline`: a drop with no network, waiting for it rather than
        /// spending attempts on dials that can only fail.
        case idle, dialing, waiting, offline, lost
    }

    /// Why a dial ended without a stream.
    enum Drop: Equatable {
        /// The socket closed, or the mint never answered.
        case lost
        /// Closed 4001: another connection of this player took the seat.
        case superseded
        /// The mint answered with this HTTP status, and its Retry-After.
        case mintRefused(status: Int, retryAfter: TimeInterval?)
    }

    private var phase = Phase.idle
    private var generation = 0
    private var policy = ReconnectPolicy()
    private var online = true
    /// When the live connection was admitted; one that holds for
    /// ReconnectPolicy.stable has proven itself, and its drop starts over.
    private var admittedAt: Date?
    private let jitter: () -> Double
    private let now: () -> Date

    init(jitter: @escaping () -> Double = { Double.random(in: 0..<1) }, now: @escaping () -> Date = Date.init) {
        self.jitter = jitter
        self.now = now
    }

    func isCurrent(_ gen: Int) -> Bool { gen == generation && (phase == .dialing || phase == .waiting) }

    private mutating func next() -> Int {
        generation += 1
        return generation
    }

    /// Idempotent while a dial is under way; a wait is cut short.
    mutating func connect() -> [Effect] {
        if phase == .dialing { return [] }
        policy = ReconnectPolicy()
        phase = .dialing
        return [.status(.connecting), .dial(next())]
    }

    /// Back in the foreground: a backgrounded socket may be dead without
    /// having said so, so a live or waiting connection dials afresh and the
    /// resume token takes the seat again. An idle or lost one is the
    /// player's to restart.
    mutating func resume() -> [Effect] {
        guard phase == .dialing || phase == .waiting || phase == .offline else { return [] }
        policy = ReconnectPolicy()
        phase = .dialing
        return [.hangUp, .status(.connecting), .dial(next())]
    }

    mutating func disconnect() -> [Effect] {
        phase = .idle
        _ = next()
        return [.hangUp, .status(.idle)]
    }

    /// The hub's sessionReady: admission, not an open socket, is progress.
    mutating func admitted(_ gen: Int) -> [Effect] {
        guard isCurrent(gen), phase == .dialing else { return [] }
        admittedAt = now()
        return [.status(.connected)]
    }

    /// A refusal that ends the stream. A seat conflict clears on the next
    /// dial, so the drop that follows retries it; anything else is the end.
    mutating func refused(_ gen: Int, exception: String, message: String?) -> [Effect] {
        guard isCurrent(gen), exception != "SeatConflict" else { return [] }
        phase = .lost
        _ = next()
        return [.hangUp, .status(.lost(message ?? exception))]
    }

    /// The mint failed or the socket closed. A wire closed as superseded
    /// (4001) lost its seat to another connection of the same player:
    /// dialing back would take it back, and the two would trade it. A mint
    /// the hub refused with a 4xx says the same however often it is asked;
    /// a 429 says when to ask again.
    mutating func dropped(_ gen: Int, _ drop: Drop = .lost) -> [Effect] {
        guard isCurrent(gen), phase == .dialing else { return [] }
        if let at = admittedAt, now().timeIntervalSince(at) >= ReconnectPolicy.stable { policy.reset() }
        admittedAt = nil
        var floor: TimeInterval = 0
        switch drop {
        case .superseded:
            phase = .lost
            return [.hangUp, .status(.lost("Playing on another device"))]
        case let .mintRefused(status, retryAfter) where status == 429:
            floor = retryAfter ?? 0
        case let .mintRefused(status, _) where (400..<500).contains(status) && status != 408:
            phase = .lost
            _ = next()
            return [.hangUp, .status(.lost("The games hub turned this app away (\(status))"))]
        case .mintRefused, .lost:
            break
        }
        guard online else {
            phase = .offline
            return [.status(.connecting)]
        }
        guard let ceiling = policy.next() else {
            phase = .lost
            return [.hangUp, .status(.lost("Lost connection to the games hub"))]
        }
        phase = .waiting
        return [.status(.connecting), .wait(next(), max(floor, ceiling * jitter()))]
    }

    /// The network came or went. Going, a pending wait is dropped; coming
    /// back, a connection waiting on it dials at once.
    mutating func pathChanged(online: Bool) -> [Effect] {
        self.online = online
        switch (online, phase) {
        case (false, .waiting):
            phase = .offline
            return []
        case (true, .offline):
            phase = .dialing
            return [.dial(next())]
        default:
            return []
        }
    }

    mutating func retryDue(_ gen: Int) -> [Effect] {
        guard isCurrent(gen), phase == .waiting else { return [] }
        phase = .dialing
        return [.dial(gen)]
    }
}
