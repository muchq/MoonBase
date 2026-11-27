#if os(iOS)
import AVFoundation

@MainActor
final class GameSoundPlayer {
    static let shared = GameSoundPlayer()

    private var players: [AVAudioPlayer] = []
    private var configured = false

    private init() {}

    func play(_ sounds: [GameSound]) {
        players.removeAll { !$0.isPlaying }
        do {
            let session = AVAudioSession.sharedInstance()
            if !configured {
                // Mix with the user's audio and honor the silent switch.
                try session.setCategory(.ambient)
                configured = true
            }
            // Try activation on each cue so playback can recover after an interruption.
            try session.setActive(true)
            let next = try AVAudioPlayer(data: GameSoundWaveform.wav(for: sounds))
            guard next.play() else { return }

            // A new cue can start immediately while the earlier one fades out.
            for player in players { player.setVolume(0, fadeDuration: 0.02) }
            players.append(next)
        } catch {
            // Audio is optional feedback. The game continues without it.
        }
    }

    func stop() {
        for player in players { player.stop() }
        players.removeAll()
    }
}
#endif
