import XCTest
#if os(iOS)
import AVFoundation
#endif
@testable import WordChains

final class GameSoundTests: XCTestCase {
    func testSoloSubmissionsChooseTheCueForTheirResult() {
        XCTAssertEqual(GameSound.cue(for: .valid, steps: 3), .step(3))
        XCTAssertEqual(GameSound.cue(for: .reachedTarget, steps: 4), .target(4))

        let refusals: [InputResult] = [.notInDictionary, .notOneLetterAway, .alreadyUsed, .wrongLength]
        for refusal in refusals {
            XCTAssertEqual(GameSound.cue(for: refusal, steps: 3), .refusal)
        }
    }

    func testOnlyAnActualSubmissionOrTakebackPlaysASoloCue() {
        let silentResults: [InputResult?] = [nil, .noRoute, .timeUp, .gameComplete]
        for result in silentResults {
            XCTAssertNil(GameSound.cue(for: result, steps: 3))
        }

        XCTAssertEqual(GameSound.takeback(from: 1, to: 0), .takeback(steps: 0))
        XCTAssertNil(GameSound.takeback(from: 0, to: 0))
        XCTAssertNil(GameSound.takeback(from: 1, to: 2))
    }

    func testPitchClimbsGentlyAndStaysBounded() {
        let pitches = (0...24).map { GameSound.pitch(for: $0) }
        for (lower, higher) in zip(pitches, pitches.dropFirst()) {
            XCTAssertGreaterThan(higher, lower)
            XCTAssertLessThan(higher / lower, 1.05)
        }
        XCTAssertLessThanOrEqual(GameSound.pitch(for: Int.max), 260)
    }

    func testRenderedCuesAreDistinctAndQuiet() {
        let sounds: [GameSound] = [
            .step(1), .takeback(steps: 0), .refusal, .target(4),
            .win, .sharedWin, .raceFinished, .timeout,
        ]
        let waveforms = sounds.map { GameSoundWaveform.wav(for: [$0]) }
        XCTAssertEqual(Set(waveforms).count, sounds.count)

        for wav in waveforms {
            XCTAssertGreaterThan(wav.count, 44)
            XCTAssertEqual(Array(wav.prefix(4)), Array("RIFF".utf8))
            XCTAssertEqual(Array(wav.dropFirst(44).prefix(2)), [0, 0])
            XCTAssertEqual(Array(wav.suffix(2)), [0, 0])
            let peak = samples(in: wav).map { abs(Int($0)) }.max() ?? 0
            XCTAssertGreaterThan(peak, 0)
            XCTAssertLessThan(peak, Int(Int16.max) / 3)
        }
    }

    func testRenderedWordCuesBendAndFollowTheRungPitch() throws {
        let lowStep = samples(in: GameSoundWaveform.wav(for: [.step(1)]))[...]
        let highStep = samples(in: GameSoundWaveform.wav(for: [.step(12)]))[...]
        let takeback = samples(in: GameSoundWaveform.wav(for: [.takeback(steps: 1)]))[...]

        let early = 0.012..<0.038
        let late = 0.064..<0.09
        let lowPitch = try pitch(in: lowStep, during: late)

        XCTAssertGreaterThan(lowPitch, try pitch(in: lowStep, during: early) + 2)
        XCTAssertGreaterThan(try pitch(in: highStep, during: late), lowPitch + 20)
        XCTAssertGreaterThan(try pitch(in: takeback, during: early), try pitch(in: takeback, during: late) + 4)
        XCTAssertEqual(lowPitch, GameSound.pitch(for: 1), accuracy: 3)
        XCTAssertEqual(try pitch(in: takeback, during: late), GameSound.pitch(for: 1), accuracy: 3)
    }

    func testRenderedRefusalHasTwoDescendingKnocks() throws {
        let pitches = try notePitches(for: .refusal)
        XCTAssertEqual(pitches.count, 2)
        XCTAssertGreaterThan(try XCTUnwrap(pitches.first), try XCTUnwrap(pitches.last) + 15)
    }

    func testRenderedSolveAndWinMelodiesRise() throws {
        let rungPitch = GameSound.pitch(for: 4)
        let melodies: [(GameSound, [Double])] = [
            (.target(4), [rungPitch, rungPitch * 1.5, rungPitch * 2]),
            (.win, [220, 275, 330]),
            (.sharedWin, [220, 275, 330, 440]),
        ]

        for (cue, expected) in melodies {
            let pitches = try notePitches(for: cue)
            XCTAssertEqual(pitches.count, expected.count)
            for (actual, expected) in zip(pitches, expected) {
                XCTAssertEqual(actual, expected, accuracy: 5)
            }
        }
    }

    private let sampleRate = 24_000.0

    private func samples(in wav: Data) -> [Int16] {
        let pcm = Array(wav.dropFirst(44))
        return stride(from: 0, to: pcm.count, by: 2).map { offset in
            let bits = UInt16(pcm[offset]) | UInt16(pcm[offset + 1]) << 8
            return Int16(bitPattern: bits)
        }
    }

    private func notePitches(for sound: GameSound) throws -> [Double] {
        let pcm = samples(in: GameSoundWaveform.wav(for: [sound]))
        let audible = pcm.indices.filter { pcm[$0] != 0 }
        guard var start = audible.first else { return [] }

        // A gap this long separates notes, rather than crossings within a note.
        let gap = Int(sampleRate * 0.012)
        var notes: [ArraySlice<Int16>] = []
        var previous = start
        for frame in audible.dropFirst() {
            if frame - previous >= gap {
                notes.append(pcm[start...previous])
                start = frame
            }
            previous = frame
        }
        notes.append(pcm[start...previous])
        return try notes.map { try pitch(in: $0, during: 0.017..<0.047) }
    }

    private func pitch(in note: ArraySlice<Int16>, during window: Range<Double>) throws -> Double {
        let start = note.startIndex + Int(window.lowerBound * sampleRate)
        let end = min(note.endIndex - 1, note.startIndex + Int(window.upperBound * sampleRate))
        let crossings = (start..<max(start, end)).compactMap { index -> Double? in
            let before = Double(note[index])
            let after = Double(note[index + 1])
            guard before <= 0, after > 0 else { return nil }
            return Double(index) - before / (after - before)
        }
        let first = try XCTUnwrap(crossings.first)
        let last = try XCTUnwrap(crossings.dropFirst().last)
        return sampleRate * Double(crossings.count - 1) / (last - first)
    }

    #if os(iOS)
    func testAVAudioPlayerDecodesASequenceOfCues() throws {
        let player = try AVAudioPlayer(data: GameSoundWaveform.wav(for: [.target(4), .win]))
        XCTAssertEqual(player.numberOfChannels, 1)
        XCTAssertEqual(player.duration, 0.72, accuracy: 0.001)
    }
    #endif
}
