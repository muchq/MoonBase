#if os(iOS)
import AVFoundation

@MainActor
final class GameSoundPlayer {
    static let shared = GameSoundPlayer()

    private var player: AVAudioPlayer?
    private var configured = false

    private init() {}

    func play(_ sounds: [GameSound]) {
        do {
            if !configured {
                // Mix with the user's audio and honor the silent switch.
                try AVAudioSession.sharedInstance().setCategory(.ambient)
                configured = true
            }
            let next = try AVAudioPlayer(data: GameSoundWaveform.wav(for: sounds))
            player?.stop()
            player = next
            next.play()
        } catch {
            // Audio is optional feedback. The game continues without it.
        }
    }

    func stop() {
        player?.stop()
        player = nil
    }
}
#endif
