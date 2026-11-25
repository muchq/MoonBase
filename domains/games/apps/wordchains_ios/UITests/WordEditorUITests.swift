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

    private func startPuzzle(mode: String? = nil) -> XCUIApplication {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launch()
        if let mode {
            let button = app.buttons.containing(NSPredicate(format: "label CONTAINS %@", mode)).firstMatch
            XCTAssertTrue(button.waitForExistence(timeout: 30))
            button.tap()
        }
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

    // `hasFocus` follows the focus engine, not the first responder, so it is
    // false for a text field with the keyboard up. What the player sees is
    // the keyboard, and keystrokes still landing in the field.
    func testReturnKeepsTheKeyboardWhetherEmptyOrRejected() {
        let app = startPuzzle()
        let field = wordInput(app)
        let keyboard = app.keyboards.firstMatch
        XCTAssertTrue(keyboard.waitForExistence(timeout: 5))

        field.typeText("\n")
        settle()
        XCTAssertTrue(keyboard.exists, "Return on an empty field")

        field.typeText("zzzzz\n")
        XCTAssertTrue(app.staticTexts["word-feedback"].waitForExistence(timeout: 5))
        XCTAssertEqual(field.value as? String, "zzzzz", "One line, no newline")
        settle()
        XCTAssertTrue(keyboard.exists, "Return on a rejected word")

        field.typeText("a")
        XCTAssertEqual(field.value as? String, "zzzzza", "Typing still lands in the field")
    }

    // The field once handed one Return over twice, and the second copy
    // reported the accepted word as already in the chain.
    func testReturnOnAValidWordSubmitsOnceAndClearsTheField() {
        let app = startPuzzle()
        let field = wordInput(app)
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        app.buttons["word-hint"].tap()
        let hinted = field.value as? String
        field.typeText("\n")
        settle()
        XCTAssertFalse(app.staticTexts["word-feedback"].exists, "No error after an accepted word")
        XCTAssertNotEqual(field.value as? String, hinted, "The accepted word left the field")
        field.typeText("a")
        XCTAssertEqual(field.value as? String, "a", "The field holds only what was typed since")
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

    // A Time Attack solve advances without leaving the game screen: the
    // keyboard and the input row stay put through the celebration.
    func testTimeAttackSolveKeepsTheKeyboardUp() {
        let app = startPuzzle(mode: "Time Attack")
        let field = wordInput(app)
        let keyboard = app.keyboards.firstMatch
        let bar = app.descendants(matching: .any)["time-attack-bar"]
        XCTAssertTrue(keyboard.waitForExistence(timeout: 5))
        XCTAssertTrue(bar.waitForExistence(timeout: 5))
        let row = field.frame.minY

        // Hints walk a shortest route; the longest puzzle is 7 moves.
        for _ in 0..<8 where !bar.label.contains("1 solved") {
            app.buttons["word-hint"].tap()
            app.buttons["word-submit"].tap()
        }
        XCTAssertTrue(bar.label.contains("1 solved"), bar.label)
        XCTAssertTrue(keyboard.exists, "Through the celebration")
        XCTAssertEqual(field.frame.minY, row, accuracy: 1)

        settle()
        XCTAssertTrue(keyboard.exists, "On the next puzzle")
        XCTAssertEqual(field.frame.minY, row, accuracy: 1)
        field.typeText("a")
        XCTAssertEqual(field.value as? String, "a", "Typing lands in the next puzzle's field")
    }
}
