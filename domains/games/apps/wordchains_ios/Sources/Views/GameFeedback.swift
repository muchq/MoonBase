import SwiftUI

struct GameFeedback: Sendable {
    static let hapticsKey = "wordChains.hapticsEnabled"
    static let reduceMotionKey = "wordChains.reduceMotion"

    let hapticsEnabled: Bool
    let reduceMotion: Bool

    init(hapticsEnabled: Bool = true, prefersReducedMotion: Bool = false, systemReduceMotion: Bool = false) {
        self.hapticsEnabled = hapticsEnabled
        self.reduceMotion = prefersReducedMotion || systemReduceMotion
    }

    func performHaptic(_ emit: () -> Void) {
        if hapticsEnabled { emit() }
    }
}

private struct GameFeedbackKey: EnvironmentKey {
    static let defaultValue = GameFeedback()
}

extension EnvironmentValues {
    var gameFeedback: GameFeedback {
        get { self[GameFeedbackKey.self] }
        set { self[GameFeedbackKey.self] = newValue }
    }
}
