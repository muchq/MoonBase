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

    func testSettingsCanBeChangedAndSurviveRelaunch() {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launch()

        assertMenuSharesTitleRow(in: app)
        openMenuPage("Settings", in: app)

        let sound = app.switches["settings-sound-effects"]
        let haptics = app.switches["settings-haptics"]
        let motion = app.switches["settings-reduce-motion"]
        XCTAssertTrue(haptics.waitForExistence(timeout: 5))
        XCTAssertTrue(motion.exists)
        XCTAssertEqual(sound.value as? String, "1")
        XCTAssertEqual(haptics.value as? String, "1")
        XCTAssertEqual(motion.value as? String, "0")
        capture("06-settings")

        setSwitch(sound, to: "0")
        setSwitch(haptics, to: "0")
        setSwitch(motion, to: "1")
        returnFromMenuPage("Settings", in: app)
        XCTAssertTrue(app.buttons["Play"].waitForExistence(timeout: 5))

        app.terminate()
        app.launch()
        openMenuPage("Settings", in: app)
        XCTAssertTrue(haptics.waitForExistence(timeout: 5))
        XCTAssertEqual(sound.value as? String, "0")
        XCTAssertEqual(haptics.value as? String, "0")
        XCTAssertEqual(motion.value as? String, "1")

        setSwitch(sound, to: "1")
        setSwitch(haptics, to: "1")
        setSwitch(motion, to: "0")
        returnFromMenuPage("Settings", in: app)
        XCTAssertTrue(app.buttons["Play"].isHittable)
    }

    private func setSwitch(_ element: XCUIElement, to value: String) {
        // In a Form, XCTest includes the label in the switch's frame.
        let control = element.coordinate(withNormalizedOffset: CGVector(dx: 0.9, dy: 0.5))
        for _ in 0..<2 {
            if element.value as? String == value { return }
            control.tap()
            let changed = XCTNSPredicateExpectation(
                predicate: NSPredicate(format: "value == %@", value), object: element
            )
            if XCTWaiter.wait(for: [changed], timeout: 3) == .completed { return }
        }
        XCTAssertEqual(element.value as? String, value, element.identifier)
    }

    func testMenuOpensHelpAndAbout() {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launch()

        let menu = app.buttons["menu-open"]
        XCTAssertTrue(menu.waitForExistence(timeout: 30))
        XCTAssertFalse(app.buttons["menu-settings"].exists)
        menu.tap()
        let help = app.buttons["Help"]
        XCTAssertTrue(help.waitForExistence(timeout: 5))
        XCTAssertTrue(app.buttons["About"].exists)
        XCTAssertTrue(app.buttons["Settings"].exists)
        capture("09-open-menu")
        help.tap()

        XCTAssertTrue(app.staticTexts["How to play"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.descendants(matching: .any)["help-example"].exists)
        XCTAssertTrue(app.staticTexts["Controls"].exists)
        capture("10-help")
        returnFromMenuPage("Help", in: app)

        openMenuPage("About", in: app)
        XCTAssertTrue(app.navigationBars["About"].waitForExistence(timeout: 5))
        XCTAssertTrue(app.staticTexts.containing(NSPredicate(format: "label BEGINSWITH %@", "Version ")).firstMatch.exists)
        XCTAssertFalse(app.descendants(matching: .any)["Source code"].exists)
        capture("11-about")
        returnFromMenuPage("About", in: app)
        XCTAssertTrue(menu.waitForExistence(timeout: 5))
    }

    private func openMenuPage(_ page: String, in app: XCUIApplication) {
        let menu = app.buttons["menu-open"]
        XCTAssertTrue(menu.waitForExistence(timeout: 30))
        menu.tap()
        let item = app.buttons[page]
        XCTAssertTrue(item.waitForExistence(timeout: 5))
        item.tap()
    }

    private func returnFromMenuPage(_ page: String, in app: XCUIApplication) {
        let navigationBar = app.navigationBars[page]
        XCTAssertTrue(navigationBar.waitForExistence(timeout: 5))
        XCTAssertFalse(navigationBar.buttons["Done"].exists)
        let back = navigationBar.buttons.firstMatch
        XCTAssertTrue(back.isHittable)
        back.tap()
        XCTAssertTrue(app.buttons["menu-open"].waitForExistence(timeout: 5))
    }

    func testRaceRoomCreationAndJoiningLeadDirectlyToTheLobby() {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launchArguments = ["-raceRoomUITest"]
        app.launch()

        let race = app.buttons["Race friends"]
        XCTAssertTrue(race.waitForExistence(timeout: 30))
        for _ in 0..<4 where !race.isHittable { app.swipeUp() }
        race.tap()
        let create = app.buttons["Create a room"]
        XCTAssertTrue(create.waitForExistence(timeout: 5))
        create.tap()

        let code = app.staticTexts["race-room-code"]
        XCTAssertTrue(code.waitForExistence(timeout: 5))
        XCTAssertEqual(code.label, "AB12")
        XCTAssertTrue(app.buttons["Invite friends"].exists)
        XCTAssertTrue(app.buttons["Practice"].exists)
        XCTAssertFalse(app.buttons["Open a race table"].exists)
        capture("12-race-room-created")

        app.navigationBars.buttons["Leave"].tap()
        XCTAssertTrue(create.waitForExistence(timeout: 5))

        let field = app.textFields["Room code"]
        field.tap()
        field.typeText("AB12")
        app.buttons["Join"].tap()

        XCTAssertTrue(code.waitForExistence(timeout: 5))
        XCTAssertEqual(code.label, "AB12")
        XCTAssertTrue(app.buttons["Start the race"].exists)
        let friend = app.descendants(matching: .any)
            .matching(NSPredicate(format: "label CONTAINS %@", "alex")).firstMatch
        XCTAssertTrue(friend.exists)
        capture("13-race-room-joined")
    }

    func testModeSelectionKeepsMenuLayoutStable() {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launchArguments = ["-UIPreferredContentSizeCategoryName", "UICTContentSizeCategoryXXXL"]
        app.launch()

        let labels = ["Quick Play", "Daily Challenge", "Time Attack"]
        let buttons = labels.map { label in
            app.buttons.containing(NSPredicate(format: "label CONTAINS %@", label)).firstMatch
        }
        XCTAssertTrue(buttons[0].waitForExistence(timeout: 30))
        assertMenuSharesTitleRow(in: app)
        XCTAssertEqual(buttons.map { $0.label }, labels)
        let initialFrames = buttons.map { $0.frame }

        for selected in [1, 2, 0] {
            buttons[selected].tap()
            settle()
            XCTAssertTrue(buttons[selected].isSelected)
            for (index, button) in buttons.enumerated() {
                XCTAssertEqual(button.frame.minY, initialFrames[index].minY, accuracy: 1)
                XCTAssertEqual(button.frame.height, initialFrames[index].height, accuracy: 1)
            }
        }
    }

    private func assertMenuSharesTitleRow(in app: XCUIApplication) {
        let title = app.staticTexts["Word Chains"]
        let menu = app.buttons["menu-open"]
        XCTAssertTrue(title.waitForExistence(timeout: 30))
        XCTAssertTrue(menu.exists)
        settle()
        XCTAssertEqual(menu.frame.midY, title.frame.midY, accuracy: 3)
        XCTAssertEqual(title.frame.midX, app.frame.midX, accuracy: 3)
        XCTAssertLessThan(title.frame.height, 70, "The home title should fit on one line.")
        XCTAssertLessThanOrEqual(menu.frame.maxX, title.frame.minX)
        XCTAssertGreaterThanOrEqual(menu.frame.width.rounded(), 44)
        XCTAssertGreaterThanOrEqual(menu.frame.height.rounded(), 44)
    }

    private func startPuzzle(mode: String? = nil, difficulty: String? = nil, arguments: [String] = []) -> XCUIApplication {
        let app = XCUIApplication(bundleIdentifier: "com.muchq.wordchains")
        app.launchArguments = arguments
        let launchStarted = ContinuousClock.now
        app.launch()
        print("Word Chains timing: app launch took \(launchStarted.duration(to: .now))")
        if let mode {
            let button = app.buttons.containing(NSPredicate(format: "label CONTAINS %@", mode)).firstMatch
            XCTAssertTrue(button.waitForExistence(timeout: 30))
            button.tap()
        }
        if let difficulty {
            let button = app.buttons.containing(NSPredicate(format: "label BEGINSWITH %@", difficulty)).firstMatch
            XCTAssertTrue(button.waitForExistence(timeout: 5))
            for _ in 0..<4 where !button.isHittable { app.swipeUp() }
            XCTAssertTrue(button.isHittable)
            button.tap()
        }
        let play = app.buttons["Play"]
        XCTAssertTrue(play.waitForExistence(timeout: 30))
        for _ in 0..<4 where !play.isHittable { app.swipeUp() }
        XCTAssertTrue(play.isHittable)
        let playStarted = ContinuousClock.now
        play.tap()
        XCTAssertTrue(app.descendants(matching: .any)["word-input"].waitForExistence(timeout: 5))
        print("Word Chains timing: \(mode ?? "Quick Play") Play to word input took \(playStarted.duration(to: .now))")
        return app
    }

    private func capture(_ screen: String) {
        settle()
        let attachment = XCTAttachment(data: XCUIScreen.main.screenshot().pngRepresentation, uniformTypeIdentifier: "public.png")
        attachment.name = "WordChains-\(screen)"
        attachment.lifetime = .keepAlways
        add(attachment)
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
        XCTAssertFalse(app.staticTexts["word-feedback"].exists, "Typing after a rejected Return clears the error")
    }

    // An accepted word leaves the field on screen, not just in the model:
    // a stale word would ride along with the next keystroke.
    func testReturnOnAValidWordClearsTheFieldWithNoError() {
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

    func testUndoClearsTheFieldOnScreen() {
        let app = startPuzzle()
        let field = wordInput(app)
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        app.buttons["word-hint"].tap()
        field.typeText("\n")
        settle()
        field.typeText("b")
        app.buttons["word-undo"].tap()
        field.typeText("c")
        XCTAssertEqual(field.value as? String, "c", "Undo left nothing behind in the field")
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
    func testTimeAttackHasNoHintButton() {
        let app = startPuzzle(mode: "Time Attack")
        XCTAssertTrue(app.descendants(matching: .any)["time-attack-bar"].waitForExistence(timeout: 5))
        XCTAssertFalse(app.buttons["word-hint"].exists)
    }

    func testDailyChallengeHasNoHintsAndCanReturnToMenu() {
        let app = startPuzzle(mode: "Daily Challenge", arguments: ["-sprintHints"])
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))
        let field = wordInput(app)
        XCTAssertTrue(field.isHittable)
        XCTAssertFalse(app.buttons["word-hint"].exists)
        capture("02-daily-challenge")

        field.typeText("a")
        XCTAssertTrue(app.buttons["word-submit"].isHittable)
        XCTAssertTrue(app.buttons["word-submit"].isEnabled)

        let menu = app.buttons["game-menu"]
        XCTAssertTrue(menu.waitForExistence(timeout: 5))
        XCTAssertEqual(menu.label, "Back to menu")
        XCTAssertTrue(menu.isHittable)
        menu.tap()

        let quickPlay = app.buttons.containing(NSPredicate(format: "label CONTAINS %@", "Quick Play")).firstMatch
        XCTAssertTrue(quickPlay.waitForExistence(timeout: 5))
        XCTAssertFalse(app.descendants(matching: .any)["word-input"].exists)
        let title = app.staticTexts["Word Chains"]
        for _ in 0..<4 where !title.isHittable { app.swipeDown() }
        XCTAssertTrue(title.isHittable)
        capture("01-menu")
    }

    func testFinishedDailyShowsItsSavedChainAndShareSheetAfterRelaunch() throws {
        let app = startPuzzle(mode: "Daily Challenge", difficulty: "Easy")
        let field = wordInput(app)
        XCTAssertTrue(app.keyboards.firstMatch.waitForExistence(timeout: 5))

        let start = app.staticTexts["puzzle-start"].label.lowercased()
        let target = app.staticTexts["puzzle-target"].label.lowercased()
        let graphURL = try XCTUnwrap(Bundle(for: WordEditorUITests.self).url(forResource: "word_graph", withExtension: "json"))
        let graph = try JSONDecoder().decode(TestWordGraph.self, from: Data(contentsOf: graphURL))
        let words = try graph.path(from: start, to: target)
        for word in words.dropFirst() {
            field.typeText(word + "\n")
        }

        let chain = app.staticTexts[words.joined(separator: " → ")]
        let share = app.buttons["Share Result"]
        XCTAssertTrue(share.waitForExistence(timeout: 5))
        XCTAssertTrue(chain.exists)
        XCTAssertFalse(app.buttons["Play Again"].exists)

        app.terminate()
        app.launch()
        let daily = app.buttons["Daily Challenge"]
        XCTAssertTrue(daily.waitForExistence(timeout: 30))
        daily.tap()
        let easy = app.buttons.containing(NSPredicate(format: "label BEGINSWITH %@", "Easy")).firstMatch
        XCTAssertTrue(easy.waitForExistence(timeout: 5))
        for _ in 0..<4 where !easy.isHittable { app.swipeUp() }
        easy.tap()
        XCTAssertEqual(daily.value as? String, "Solved today")
        let viewResult = app.buttons["View Result"]
        for _ in 0..<4 where !viewResult.isHittable { app.swipeUp() }
        XCTAssertTrue(viewResult.isHittable)
        capture("07-daily-solved-menu")
        viewResult.tap()

        XCTAssertTrue(chain.waitForExistence(timeout: 5))
        XCTAssertFalse(app.keyboards.firstMatch.exists)
        capture("08-saved-daily-result")
        for _ in 0..<4 where !share.isHittable { app.swipeUp() }
        XCTAssertTrue(share.isHittable)
        share.tap()
        let copy = app.descendants(matching: .any)["Copy"].firstMatch
        XCTAssertTrue(copy.waitForExistence(timeout: 15), "Share sheet did not show Copy.\n\(app.debugDescription)")
    }

    func testQuickPlayResultsCanReturnToTheMenu() {
        let app = startPuzzle()
        let hint = app.buttons["word-hint"]
        let submit = app.buttons["word-submit"]
        XCTAssertTrue(hint.waitForExistence(timeout: 5))
        capture("03-quick-play")

        for _ in 0..<8 where hint.exists {
            hint.tap()
            submit.tap()
        }
        let newPuzzle = app.buttons["New Puzzle"]
        XCTAssertTrue(newPuzzle.waitForExistence(timeout: 5))
        capture("04-puzzle-results")

        let back = app.buttons["Back to Menu"]
        for _ in 0..<4 where !back.isHittable { app.swipeUp() }
        XCTAssertTrue(back.isHittable)
        back.tap()
        XCTAssertTrue(app.buttons["Play"].waitForExistence(timeout: 5))
    }

    func testTimeAttackSummaryOffersReplayAndMenu() {
        let app = startPuzzle(mode: "Time Attack", difficulty: "Easy", arguments: ["-expireSprintInBackground"])
        XCTAssertTrue(app.descendants(matching: .any)["time-attack-bar"].waitForExistence(timeout: 5))

        XCUIDevice.shared.press(.home)
        XCTAssertTrue(app.wait(for: .runningBackground, timeout: 5) || app.state == .runningBackgroundSuspended)
        app.activate()

        let replay = app.buttons["time-attack-replay"]
        XCTAssertTrue(replay.waitForExistence(timeout: 15))
        XCTAssertTrue(app.staticTexts["Easy · 120 seconds"].exists)
        for _ in 0..<4 where !replay.isHittable { app.swipeUp() }
        XCTAssertTrue(replay.isHittable)
        capture("05-time-attack-results")

        let back = app.buttons["Back to Menu"]
        for _ in 0..<4 where !back.isHittable { app.swipeUp() }
        XCTAssertTrue(back.isHittable)
        back.tap()
        XCTAssertTrue(app.buttons["Play"].waitForExistence(timeout: 5))
    }

    // Players get no hints in a sprint; this test turns them on to solve one.
    func testTimeAttackSolveKeepsTheKeyboardUp() {
        let app = startPuzzle(mode: "Time Attack", arguments: ["-sprintHints"])
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

/// Find a solution in the same bundled dictionary without enabling in-app hints.
private struct TestWordGraph: Decodable {
    let nodes: [String]
    let edges: [[Int]]

    func path(from start: String, to target: String) throws -> [String] {
        let first = try XCTUnwrap(nodes.firstIndex(of: start))
        let last = try XCTUnwrap(nodes.firstIndex(of: target))
        var parents = [first: first]
        var queue = [first]
        var head = 0
        while head < queue.count && parents[last] == nil {
            let current = queue[head]
            head += 1
            for neighbor in edges[current] where parents[neighbor] == nil {
                parents[neighbor] = current
                queue.append(neighbor)
            }
        }

        var chain = [target]
        var current = last
        while current != first {
            current = try XCTUnwrap(parents[current], "No route from \(start) to \(target)")
            chain.append(nodes[current])
        }
        return chain.reversed()
    }
}
