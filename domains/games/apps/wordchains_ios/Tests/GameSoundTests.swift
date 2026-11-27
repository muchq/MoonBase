import XCTest
#if os(iOS)
import AVFoundation
#endif
@testable import WordChains

final class GameSoundTests: XCTestCase {
    func testPitchClimbsGentlyAndStaysBounded() {
        let pitches = (0...24).map { GameSound.pitch(for: $0) }
        for (lower, higher) in zip(pitches, pitches.dropFirst()) {
            XCTAssertGreaterThan(higher, lower)
            XCTAssertLessThan(higher / lower, 1.05)
        }
        XCTAssertLessThanOrEqual(GameSound.pitch(for: Int.max), 260)
    }

    func testCueWaveformsStartAndEndQuietly() {
        let sounds: [GameSound] = [
            .step(1), .takeback(steps: 0), .refusal, .target(4),
            .win, .sharedWin, .raceFinished, .timeout,
        ]
        for sound in sounds {
            let wav = GameSoundWaveform.wav(for: [sound])
            XCTAssertGreaterThan(wav.count, 44)
            XCTAssertEqual(Array(wav.prefix(4)), Array("RIFF".utf8))
            XCTAssertEqual(Array(wav.dropFirst(44).prefix(2)), [0, 0])
            XCTAssertEqual(Array(wav.suffix(2)), [0, 0])
            XCTAssertTrue(wav.dropFirst(44).contains { $0 != 0 })
        }
    }

    #if os(iOS)
    func testAVAudioPlayerDecodesASequenceOfCues() throws {
        let player = try AVAudioPlayer(data: GameSoundWaveform.wav(for: [.target(4), .win]))
        XCTAssertEqual(player.numberOfChannels, 1)
        XCTAssertEqual(player.duration, 0.44, accuracy: 0.001)
    }
    #endif
}
