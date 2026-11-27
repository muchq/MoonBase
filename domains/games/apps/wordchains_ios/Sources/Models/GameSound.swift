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

    fileprivate var thumps: [Thump] {
        switch self {
        case let .step(steps):
            return [Thump(pitch: Self.pitch(for: steps))]
        case let .takeback(steps):
            let pitch = Self.pitch(for: steps)
            return [Thump(pitch: pitch, fall: pitch * 0.08, volume: 0.15)]
        case .refusal:
            return [Thump(pitch: 128, fall: 23, volume: 0.13, duration: 0.09)]
        case let .target(steps):
            return [Thump(pitch: Self.pitch(for: steps), volume: 0.24, duration: 0.15)]
        case .win:
            return [Thump(pitch: 205), Thump(pitch: 273, duration: 0.14)]
        case .sharedWin:
            return [Thump(pitch: 205), Thump(pitch: 245), Thump(pitch: 273, duration: 0.14)]
        case .raceFinished:
            return [Thump(pitch: 190, volume: 0.15, duration: 0.13)]
        case .timeout:
            return [Thump(pitch: 158, volume: 0.15), Thump(pitch: 125, fall: 16, volume: 0.13)]
        }
    }
}

private struct Thump {
    let pitch: Double
    var fall = 8.0
    var volume = 0.20
    var duration = 0.10
}

enum GameSoundWaveform {
    private static let sampleRate: UInt32 = 24_000

    /// Mono 16-bit PCM, wrapped in a WAV so the whole cue plays as one sound.
    static func wav(for sounds: [GameSound]) -> Data {
        let thumps = sounds.flatMap(\.thumps)
        let rate = Double(sampleRate)
        let gapFrames = Int(rate * 0.025)
        let frames = thumps.map { Int(rate * $0.duration) }
        let pcmSize = UInt32(2 * (frames.reduce(0, +) + max(0, thumps.count - 1) * gapFrames))
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

        for (index, thump) in thumps.enumerated() {
            if index > 0 { wav.append(contentsOf: repeatElement(UInt8(0), count: gapFrames * 2)) }
            var phase = 0.0
            for frame in 0..<frames[index] {
                let time = Double(frame) / rate
                let progress = time / thump.duration
                let frequency = thump.pitch + thump.fall * (1 - progress)
                phase += 2 * .pi * frequency / rate
                // Smooth ends avoid a click. A little harmonic at the attack
                // keeps the low thump audible on a phone speaker.
                let attack = min(1, time / 0.008)
                let envelope = attack * attack * (1 - progress) * (1 - progress)
                let tone = sin(phase) + 0.25 * (1 - progress) * sin(2 * phase)
                let sample = Int16(tone * envelope * thump.volume * Double(Int16.max))
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
