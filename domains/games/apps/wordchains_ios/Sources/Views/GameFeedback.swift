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

    func stopSoundIfDisabled(_ stop: () -> Void) {
        if !soundEffectsEnabled { stop() }
    }

    @MainActor
    func playSounds(_ sounds: [GameSound]) {
        #if os(iOS)
        guard !sounds.isEmpty else { return }
        performSound { GameSoundPlayer.shared.play(sounds) }
        #endif
    }
}

struct GameFeedbackSettings: DynamicProperty {
    @AppStorage var soundEffectsEnabled: Bool
    @AppStorage var hapticsEnabled: Bool
    @AppStorage var prefersReducedMotion: Bool

    init(store: UserDefaults? = nil) {
        _soundEffectsEnabled = AppStorage(wrappedValue: true, GameFeedback.soundEffectsKey, store: store)
        _hapticsEnabled = AppStorage(wrappedValue: true, GameFeedback.hapticsKey, store: store)
        _prefersReducedMotion = AppStorage(wrappedValue: false, GameFeedback.reduceMotionKey, store: store)
    }

    func feedback(systemReduceMotion: Bool) -> GameFeedback {
        GameFeedback(
            hapticsEnabled: hapticsEnabled,
            prefersReducedMotion: prefersReducedMotion,
            systemReduceMotion: systemReduceMotion,
            soundEffectsEnabled: soundEffectsEnabled
        )
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
