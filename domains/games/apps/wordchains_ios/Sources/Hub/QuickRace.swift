import Foundation

struct QuickRace {
    let view: WordchainView
    let viewer: String

    enum Stage: Equatable {
        case looking(msLeft: Int64)
        case expired
        case starting(opponent: String?, count: Int)
        case racing
    }

    func stage(nowMs: Int64) -> Stage { .racing }
    func ringLeft(nowMs: Int64) -> Double { -1 }
    func status(nowMs: Int64) -> String { "" }
    func spoken(nowMs: Int64) -> String { "" }
    func waits(nowMs: Int64) -> Bool { false }
    var ladderLetters: Int { 0 }
}
