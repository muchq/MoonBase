import XCTest

final class WordEditorUITests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    private func startPuzzle() -> XCUIApplication {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launch()
        let play = app.buttons["Play"]
        XCTAssertTrue(play.waitForExistence(timeout: 30))
        for _ in 0..<4 where !play.isHittable { app.swipeUp() }
        XCTAssertTrue(play.isHittable)
        play.tap()
        XCTAssertTrue(app.buttons["word-letter-0"].waitForExistence(timeout: 5))
        return app
    }

    func testKeyboardFollowsSelectedTileAndHintsCanBePlayedAndUndone() {
        let app = startPuzzle()
        let undo = app.buttons["word-undo"]
        let play = app.buttons["word-play"]
        let hint = app.buttons["word-hint"]
        XCTAssertFalse(undo.isEnabled)
        XCTAssertFalse(play.isEnabled)

        app.buttons["word-letter-0"].tap()
        let first = app.textFields["word-letter-0"]
        XCTAssertTrue(first.waitForExistence(timeout: 5))
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        first.typeText("q")
        XCTAssertEqual(first.value as? String, "Q")
        first.typeText("x")
        XCTAssertEqual(first.value as? String, "X", "Typing again must replace the visible letter")
        first.typeText("1")
        XCTAssertEqual(first.value as? String, "X", "Rejected input must not remain in the native field")

        app.buttons["word-letter-1"].tap()
        let second = app.textFields["word-letter-1"]
        XCTAssertTrue(second.waitForExistence(timeout: 5))
        second.typeText("z")
        XCTAssertEqual(second.value as? String, "Z")
        XCTAssertTrue(app.buttons["word-letter-0"].exists)

        hint.tap()
        XCTAssertFalse(play.isEnabled, "The first hint only selects a letter")
        XCTAssertEqual(hint.label, "Reveal")
        hint.tap()
        XCTAssertTrue(play.isEnabled)
        play.tap()
        XCTAssertTrue(undo.waitForExistence(timeout: 5))
        XCTAssertTrue(undo.isEnabled)
        XCTAssertFalse(play.isEnabled, "A played candidate must be cleared")
        XCTAssertTrue(app.keyboards.firstMatch.exists, "Keep the keyboard available between moves")
        undo.tap()
        XCTAssertFalse(undo.isEnabled)
        XCTAssertFalse(play.isEnabled)
        XCTAssertEqual(hint.label, "Hint")
    }

    func testDictionaryErrorDoesNotMoveTheEditorOrCoverItsControls() {
        let app = startPuzzle()
        app.buttons["word-letter-0"].tap()
        let field = app.textFields["word-letter-0"]
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        let play = app.buttons["word-play"]
        let feedback = app.staticTexts["word-feedback"]
        let originalLabel = field.label
        let beforeField = field.frame
        let beforePlay = play.frame

        // Use the actual bundled dictionary and random puzzle, without a test
        // backdoor in the app. Retry letters only until a word is rejected.
        for letter in "qxzjvk" {
            field.typeText(String(letter))
            if play.isEnabled { play.tap() }
            if feedback.label == "Not in dictionary" { break }
            // A valid probe can advance the chain. Restore the original rung.
            let undo = app.buttons["word-undo"]
            if undo.isEnabled { undo.tap() }
        }
        XCTAssertEqual(feedback.label, "Not in dictionary")
        XCTAssertEqual(field.label, originalLabel)
        XCTAssertEqual(field.frame.minY, beforeField.minY, accuracy: 1)
        XCTAssertEqual(play.frame.minY, beforePlay.minY, accuracy: 1)
        XCTAssertTrue(field.isHittable)
        XCTAssertTrue(play.isHittable)
        XCTAssertLessThanOrEqual(play.frame.maxY, app.keyboards.firstMatch.frame.minY)
        let screenshot = XCTAttachment(screenshot: app.screenshot())
        screenshot.name = "Dictionary error with keyboard visible"
        screenshot.lifetime = .keepAlways
        add(screenshot)
    }
}
