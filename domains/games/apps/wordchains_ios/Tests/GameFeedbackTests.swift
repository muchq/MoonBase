import XCTest
@testable import WordChains

final class GameFeedbackTests: XCTestCase {
    func testReduceMotionHonorsEitherPreference() {
        XCTAssertFalse(GameFeedback().reduceMotion)
        XCTAssertTrue(GameFeedback(prefersReducedMotion: true).reduceMotion)
        XCTAssertTrue(GameFeedback(systemReduceMotion: true).reduceMotion)
        XCTAssertTrue(GameFeedback(prefersReducedMotion: true, systemReduceMotion: true).reduceMotion)
    }

    func testHapticsSettingControlsFeedbackIndependentlyOfMotion() {
        var pulses = 0
        GameFeedback(hapticsEnabled: false).performHaptic { pulses += 1 }
        XCTAssertEqual(pulses, 0)

        GameFeedback(prefersReducedMotion: true).performHaptic { pulses += 1 }
        XCTAssertEqual(pulses, 1)
    }
}
