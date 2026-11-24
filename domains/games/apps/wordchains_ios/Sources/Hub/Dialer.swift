import Foundation

/// HubSocket's decisions, kept apart from URLSession so they can be tested:
/// when to dial, when to wait and dial again, when to give up. Every dial
/// and every wait has a generation; news carrying any other generation is
/// from a dial a disconnect, reconnect or retry already replaced, and
/// changes nothing.
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
        case idle, dialing, waiting, lost
    }

    private var phase = Phase.idle
    private var generation = 0
    private var policy = ReconnectPolicy()

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
        guard phase == .dialing || phase == .waiting else { return [] }
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
        policy.admitted()
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
    /// dialing back would take it back, and the two would trade it.
    mutating func dropped(_ gen: Int, superseded: Bool = false) -> [Effect] {
        guard isCurrent(gen), phase == .dialing else { return [] }
        if superseded {
            phase = .lost
            return [.hangUp, .status(.lost("Playing on another device"))]
        }
        guard let delay = policy.next() else {
            phase = .lost
            return [.hangUp, .status(.lost("Lost connection to the games hub"))]
        }
        phase = .waiting
        return [.status(.connecting), .wait(next(), delay)]
    }

    mutating func retryDue(_ gen: Int) -> [Effect] {
        guard isCurrent(gen), phase == .waiting else { return [] }
        phase = .dialing
        return [.dial(gen)]
    }
}
