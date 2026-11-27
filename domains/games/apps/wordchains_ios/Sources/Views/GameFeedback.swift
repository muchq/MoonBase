import SwiftUI

struct GameFeedback: Sendable {
    static let hapticsKey = "wordChains.hapticsEnabled"
    static let reduceMotionKey = "wordChains.reduceMotion"
    static let soundEffectsKey = "wordChains.soundEffectsEnabled"

    let soundEffectsEnabled: Bool
    let hapticsEnabled: Bool
    let reduceMotion: Bool

    init(
        hapticsEnabled: Bool = true,
        prefersReducedMotion: Bool = false,
        systemReduceMotion: Bool = false,
        soundEffectsEnabled: Bool = true
    ) {
        self.soundEffectsEnabled = soundEffectsEnabled
        self.hapticsEnabled = hapticsEnabled
        self.reduceMotion = prefersReducedMotion || systemReduceMotion
    }

    func performHaptic(_ emit: () -> Void) {
        if hapticsEnabled { emit() }
    }

    func performSound(_ emit: () -> Void) {
        if soundEffectsEnabled { emit() }
    }

    @MainActor
    func playSounds(_ sounds: [GameSound]) {
        #if os(iOS)
        guard !sounds.isEmpty else { return }
        performSound { GameSoundPlayer.shared.play(sounds) }
        #endif
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
