import XCTest
@testable import WordChains

final class GameFeedbackTests: XCTestCase {
    func testReduceMotionHonorsEitherPreference() {
        XCTAssertFalse(GameFeedback().reduceMotion)
        XCTAssertTrue(GameFeedback(prefersReducedMotion: true).reduceMotion)
        XCTAssertTrue(GameFeedback(systemReduceMotion: true).reduceMotion)
        XCTAssertTrue(GameFeedback(prefersReducedMotion: true, systemReduceMotion: true).reduceMotion)
    }

    func testSoundEffectsSettingControlsPlaybackIndependentlyOfOtherFeedback() {
        var cues = 0
        GameFeedback(soundEffectsEnabled: false).performSound { cues += 1 }
        XCTAssertEqual(cues, 0)

        GameFeedback(hapticsEnabled: false, prefersReducedMotion: true).performSound { cues += 1 }
        XCTAssertEqual(cues, 1)
    }

    func testStoredSoundSettingControlsPlaybackAndStopsActiveCuesWhenMuted() throws {
        let suite = "WordChains.GameFeedbackTests.\(UUID())"
        let store = try XCTUnwrap(UserDefaults(suiteName: suite))
        defer { store.removePersistentDomain(forName: suite) }
        var plays = 0
        var stops = 0

        let enabled = GameFeedbackSettings(store: store).feedback(systemReduceMotion: false)
        enabled.performSound { plays += 1 }
        enabled.stopSoundIfDisabled { stops += 1 }
        XCTAssertEqual(plays, 1)
        XCTAssertEqual(stops, 0)

        store.set(false, forKey: GameFeedback.soundEffectsKey)
        let muted = GameFeedbackSettings(store: store).feedback(systemReduceMotion: false)
        muted.performSound { plays += 1 }
        muted.stopSoundIfDisabled { stops += 1 }
        XCTAssertEqual(plays, 1)
        XCTAssertEqual(stops, 1)
    }

    func testHapticsSettingControlsFeedbackIndependentlyOfMotion() {
        var pulses = 0
        GameFeedback(hapticsEnabled: false).performHaptic { pulses += 1 }
        XCTAssertEqual(pulses, 0)

        GameFeedback(prefersReducedMotion: true).performHaptic { pulses += 1 }
        XCTAssertEqual(pulses, 1)
    }
}
