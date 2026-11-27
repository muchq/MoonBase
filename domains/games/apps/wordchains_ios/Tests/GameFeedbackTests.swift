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

    func testHapticsSettingControlsFeedbackIndependentlyOfMotion() {
        var pulses = 0
        GameFeedback(hapticsEnabled: false).performHaptic { pulses += 1 }
        XCTAssertEqual(pulses, 0)

        GameFeedback(prefersReducedMotion: true).performHaptic { pulses += 1 }
        XCTAssertEqual(pulses, 1)
    }
}
