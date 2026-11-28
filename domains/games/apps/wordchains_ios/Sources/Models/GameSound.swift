import Foundation

enum GameSound: Equatable {
    case step(Int)
    case takeback(steps: Int)
    case refusal
    case target(Int)
    case win
    case sharedWin
    case raceFinished
    case timeout

    static func cue(for result: InputResult?, steps: Int) -> GameSound? {
        switch result {
        case .valid: return .step(steps)
        case .reachedTarget: return .target(steps)
        case .notInDictionary, .notOneLetterAway, .alreadyUsed, .wrongLength: return .refusal
        case nil, .gameComplete, .noRoute, .timeUp: return nil
        }
    }

    static func takeback(from previousSteps: Int, to steps: Int) -> GameSound? {
        guard steps < previousSteps else { return nil }
        return .takeback(steps: steps)
    }

    /// Small steps in pitch, with a ceiling for unusually long ladders.
    static func pitch(for steps: Int) -> Double {
        150 + 110 * (1 - exp(-Double(max(0, steps)) / 18))
    }

    fileprivate var notes: [Note] {
        switch self {
        case let .step(steps):
            let pitch = Self.pitch(for: steps)
            return [Note(pitch: pitch, bend: -pitch * 0.04)]
        case let .takeback(steps):
            let pitch = Self.pitch(for: steps)
            return [Note(pitch: pitch, bend: pitch * 0.12, volume: 0.15, brightness: 0.25)]
        case .refusal:
            return [
                Note(pitch: 137, bend: 14, volume: 0.13, duration: 0.06, brightness: 0.18),
                Note(pitch: 112, bend: 9, volume: 0.10, duration: 0.07, brightness: 0.14),
            ]
        case let .target(steps):
            let pitch = Self.pitch(for: steps)
            return [
                Note(pitch: pitch, volume: 0.19, duration: 0.075),
                Note(pitch: pitch * 1.5, volume: 0.17, duration: 0.085, brightness: 0.7),
                Note(pitch: pitch * 2, volume: 0.16, duration: 0.14, brightness: 0.85),
            ]
        case .win:
            return [
                Note(pitch: 220, volume: 0.19, duration: 0.08),
                Note(pitch: 275, volume: 0.19, duration: 0.085, brightness: 0.7),
                Note(pitch: 330, volume: 0.18, duration: 0.13, brightness: 0.85),
            ]
        case .sharedWin:
            return [
                Note(pitch: 220, volume: 0.19, duration: 0.08),
                Note(pitch: 275, volume: 0.18, duration: 0.08, brightness: 0.7),
                Note(pitch: 330, volume: 0.18, duration: 0.08, brightness: 0.75),
                Note(pitch: 440, volume: 0.16, duration: 0.14, brightness: 0.85),
            ]
        case .raceFinished:
            return [
                Note(pitch: 233, volume: 0.15, duration: 0.075),
                Note(pitch: 196, volume: 0.14, duration: 0.12, brightness: 0.28),
            ]
        case .timeout:
            return [
                Note(pitch: 165, volume: 0.15, duration: 0.10, brightness: 0.28),
                Note(pitch: 123, bend: 12, volume: 0.12, duration: 0.13, brightness: 0.16),
            ]
        }
    }
}

private struct Note {
    let pitch: Double
    var bend = 0.0
    var volume = 0.20
    var duration = 0.10
    var brightness = 0.48
}

enum GameSoundWaveform {
    private static let sampleRate: UInt32 = 24_000

    /// Mono 16-bit PCM, wrapped in a WAV so the whole cue plays as one sound.
    static func wav(for sounds: [GameSound]) -> Data {
        let notes = sounds.flatMap(\.notes)
        let rate = Double(sampleRate)
        let gapFrames = Int(rate * 0.025)
        let frames = notes.map { Int(rate * $0.duration) }
        let pcmSize = UInt32(2 * (frames.reduce(0, +) + max(0, notes.count - 1) * gapFrames))
        var wav = Data("RIFF".utf8)
        wav.appendLittleEndian(36 + pcmSize)
        wav.append(contentsOf: "WAVEfmt ".utf8)
        wav.appendLittleEndian(UInt32(16))
        wav.appendLittleEndian(UInt16(1)) // PCM
        wav.appendLittleEndian(UInt16(1)) // mono
        wav.appendLittleEndian(sampleRate)
        wav.appendLittleEndian(sampleRate * 2)
        wav.appendLittleEndian(UInt16(2)) // bytes per frame
        wav.appendLittleEndian(UInt16(16))
        wav.append(contentsOf: "data".utf8)
        wav.appendLittleEndian(pcmSize)
        wav.reserveCapacity(44 + Int(pcmSize))

        for (index, note) in notes.enumerated() {
            if index > 0 { wav.append(contentsOf: repeatElement(UInt8(0), count: gapFrames * 2)) }
            var phase = 0.0
            for frame in 0..<frames[index] {
                let time = Double(frame) / rate
                let release = 1 - time / note.duration
                let frequency = note.pitch + note.bend * release * release
                phase += 2 * .pi * frequency / rate

                // The fundamental rings longer than the upper partials of the strike.
                let attack = min(1, time / 0.005)
                let resonance = sin(phase) + 0.24 * sin(2 * phase)
                let strike = 0.52 * sin(2.76 * phase) + 0.28 * sin(4.08 * phase) + 0.20 * sin(5.41 * phase)
                let tone = resonance + note.brightness * exp(-time / 0.023) * strike
                let envelope = attack * attack * release * release
                let sample = Int16(tone * envelope * note.volume * Double(Int16.max))
                wav.appendLittleEndian(sample)
            }
        }

        return wav
    }
}

private extension Data {
    mutating func appendLittleEndian<T: FixedWidthInteger>(_ value: T) {
        Swift.withUnsafeBytes(of: value.littleEndian) { append(contentsOf: $0) }
    }
}
