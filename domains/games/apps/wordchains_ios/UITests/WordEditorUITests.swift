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
        XCTAssertTrue(app.textFields["word-input"].waitForExistence(timeout: 5))
        return app
    }

    func testAWholeWordIsTypedIntoOneField() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        field.typeText("zzzz")
        XCTAssertEqual(field.value as? String, "zzzz")
    }

    func testHintedWordCanBeSubmittedFromTheKeyboardAndUndone() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        let undo = app.buttons["word-undo"]
        XCTAssertFalse(undo.isEnabled)

        app.buttons["word-hint"].tap()
        let hinted = field.value as? String ?? ""
        XCTAssertFalse(hinted.isEmpty, "Hint fills the next word")
        field.typeText("\n")
        XCTAssertTrue(undo.isEnabled)
        XCTAssertNotEqual(field.value as? String, hinted, "A played word clears the field")
        XCTAssertTrue(app.keyboards.firstMatch.exists, "Return keeps the keyboard up")

        undo.tap()
        XCTAssertFalse(undo.isEnabled)
    }

    func testReturnOnAnEmptyFieldKeepsTheKeyboard() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        field.typeText("\n")
        XCTAssertTrue(app.keyboards.firstMatch.exists)
        XCTAssertTrue(field.hasFocus)
    }

    func testEditingAfterARejectionClearsTheStaleError() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        let feedback = app.staticTexts["word-feedback"]
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        field.typeText("zzzzz\n")
        XCTAssertTrue(feedback.waitForExistence(timeout: 5))
        field.typeText("a")
        XCTAssertFalse(feedback.exists, "The error describes the old text, not this one")
    }

    func testHintFillsTheFieldWithoutSubmitting() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        app.buttons["word-hint"].tap()
        XCTAssertFalse((field.value as? String ?? "").isEmpty)
        XCTAssertFalse(app.buttons["word-undo"].isEnabled, "The chain has not advanced")
    }

    func testTypedInvalidWordShowsFeedbackAndKeepsKeyboard() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        field.typeText("zzzzz")
        app.buttons["word-submit"].tap()
        XCTAssertTrue(app.staticTexts["word-feedback"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["word-undo"].isEnabled)
        XCTAssertEqual(field.value as? String, "zzzzz", "A rejected word stays for editing")
        XCTAssertTrue(app.keyboards.firstMatch.exists)
    }

    func testErrorDoesNotMoveTheInputRowOrPushItUnderTheKeyboard() {
        let app = startPuzzle()
        let field = app.textFields["word-input"]
        let submit = app.buttons["word-submit"]
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        field.typeText("zzzzz")
        let before = field.frame
        submit.tap()
        XCTAssertTrue(app.staticTexts["word-feedback"].waitForExistence(timeout: 5))
        XCTAssertEqual(field.frame.minY, before.minY, accuracy: 1)
        XCTAssertTrue(field.isHittable)
        XCTAssertTrue(submit.isHittable)
        XCTAssertLessThanOrEqual(submit.frame.maxY, app.keyboards.firstMatch.frame.minY)
    }

    func testControlsMeetTheMinimumTapTarget() {
        let app = startPuzzle()
        for id in ["word-undo", "word-hint", "word-submit"] {
            let frame = app.buttons[id].frame
            XCTAssertGreaterThanOrEqual(frame.width, 44, id)
            XCTAssertGreaterThanOrEqual(frame.height, 44, id)
        }
    }
}
