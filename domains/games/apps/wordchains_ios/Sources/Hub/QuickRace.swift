import Foundation

/// What the quick race's waiting screen shows, from the hub's view and the
/// clock: still looking and for how long, expired, or matched and counting
/// down to the race's first word. QuickRaceView only draws it.
struct QuickRace {
    /// How long the hub offers a seek (games_hub kQuickSeekMs): the ring's
    /// full turn. The deadline itself is always the hub's.
    static let seekMs: Int64 = 90_000

    let view: WordchainView
    let viewer: String

    enum Stage: Equatable {
        case looking(msLeft: Int64)
        case expired
        /// Matched: the opponent, and 3, 2, 1 to the start.
        case starting(opponent: String?, count: Int)
        /// Past the start, or over: the race table's.
        case racing
    }

    var opponent: String? { view.runners.first { $0.playerId != viewer }?.playerId }

    func stage(nowMs: Int64) -> Stage {
        switch view.phase {
        case .waiting where view.runners.count >= 2:
            // Matched; the hub is about to start it.
            return .starting(opponent: opponent, count: 3)
        case .waiting:
            guard let seek = view.seek else { return .expired }
            if seek.expired || nowMs >= seek.deadlineMs { return .expired }
            return .looking(msLeft: seek.deadlineMs - nowMs)
        case .racing:
            guard let start = view.startedAtMs, nowMs < start else { return .racing }
            return .starting(opponent: opponent, count: Int((start - nowMs + 999) / 1000))
        case .over, .closed:
            return .racing
        }
    }

    /// The waiting screen holds until the race's first word.
    func waits(nowMs: Int64) -> Bool { stage(nowMs: nowMs) != .racing }

    /// How much of the ring is left: full at the seek, empty at its end.
    func ringLeft(nowMs: Int64) -> Double {
        guard case let .looking(msLeft) = stage(nowMs: nowMs) else { return 0 }
        return min(1, max(0, Double(msLeft) / Double(Self.seekMs)))
    }

    func status(nowMs: Int64) -> String {
        switch stage(nowMs: nowMs) {
        case let .looking(msLeft): return "Looking for a player… \(RaceText.clock(msLeft: msLeft))"
        case .expired: return "Nobody came. Keep looking?"
        case let .starting(opponent, _): return "Playing \(opponent ?? "someone")"
        case .racing: return ""
        }
    }

    /// The status as VoiceOver says it.
    func spoken(nowMs: Int64) -> String {
        switch stage(nowMs: nowMs) {
        case let .looking(msLeft):
            let seconds = Int(max(0, (msLeft + 999) / 1000))
            let parts = [(seconds / 60, "minute"), (seconds % 60, "second")]
                .filter { $0.0 > 0 }
                .map { "\($0.0) \($0.1)\($0.0 == 1 ? "" : "s")" }
            return "Looking for a player, \(parts.joined(separator: " ")) left"
        case .expired: return "Nobody came"
        case let .starting(opponent, _): return "Playing \(opponent ?? "someone"), starting"
        case .racing: return ""
        }
    }

    /// The ladder to play while waiting: the seek's letters, or 4 for any.
    var ladderLetters: Int { view.seek?.letters ?? view.start?.count ?? 4 }
}
