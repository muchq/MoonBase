import XCTest

/// Only what needs a real keyboard and screen. The input logic behind these
/// (Return submits, stale errors clear, hints, Undo) is in WordEntryTests.
final class WordEditorUITests: XCTestCase {
    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    /// Lets a keyboard dismissal finish, so a check can't pass mid-animation.
    private func settle() {
        _ = XCTWaiter.wait(for: [XCTestExpectation(description: "settle")], timeout: 1)
    }

    private func startPuzzle() -> XCUIApplication {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launch()
        let play = app.buttons["Play"]
        XCTAssertTrue(play.waitForExistence(timeout: 30))
        for _ in 0..<4 where !play.isHittable { app.swipeUp() }
        XCTAssertTrue(play.isHittable)
        play.tap()
        XCTAssertTrue(app.descendants(matching: .any)["word-input"].waitForExistence(timeout: 5))
        return app
    }

    /// The one-line, vertical-axis field; which element type iOS reports for
    /// it is an implementation detail, so accept either.
    private func wordInput(_ app: XCUIApplication) -> XCUIElement {
        let field = app.textFields["word-input"]
        return field.exists ? field : app.textViews["word-input"]
    }

    func testReturnKeepsTheKeyboardWhetherEmptyOrRejected() {
        let app = startPuzzle()
        let field = wordInput(app)
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))

        field.typeText("\n")
        settle()
        XCTAssertTrue(field.hasFocus, "Return on an empty field")
        XCTAssertTrue(app.keyboards.firstMatch.exists)

        field.typeText("zzzzz\n")
        XCTAssertTrue(app.staticTexts["word-feedback"].waitForExistence(timeout: 5))
        XCTAssertEqual(field.value as? String, "zzzzz", "One line, no newline")
        settle()
        XCTAssertTrue(field.hasFocus, "Return on a rejected word")
        XCTAssertTrue(app.keyboards.firstMatch.exists)
    }

    func testErrorDoesNotMoveTheInputRowOrPushItUnderTheKeyboard() {
        let app = startPuzzle()
        let field = wordInput(app)
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
            // Frames come back as 43.99999…; compare whole points.
            XCTAssertGreaterThanOrEqual(frame.width.rounded(), 44, id)
            XCTAssertGreaterThanOrEqual(frame.height.rounded(), 44, id)
        }
    }
}
