import XCTest
@testable import WordChains

// HubSocket's decisions, without a network: when to dial, when to wait
// and dial again, when to give up, and which dial's news still counts.
// Every dial has a generation; news from any other is stale.
final class DialerTests: XCTestCase {

    private func connected() -> (Dialer, Int) {
        var dialer = Dialer()
        let effects = dialer.connect()
        guard case let .dial(gen) = effects.last else { XCTFail("no dial"); return (dialer, 0) }
        _ = dialer.admitted(gen)
        return (dialer, gen)
    }

    func testConnectDialsOnceWhileADialIsUnderWay() {
        var dialer = Dialer()
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(1)])
        XCTAssertEqual(dialer.connect(), [])
    }

    func testAdmissionIsConnected() {
        var dialer = Dialer()
        _ = dialer.connect()
        XCTAssertEqual(dialer.admitted(1), [.status(.connected)])
    }

    // MARK: - Drops and retries

    func testADropWaitsTwoSecondsThenDialsAgainUnderANewGeneration() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting), .wait(gen + 1, 2)])
        XCTAssertEqual(dialer.retryDue(gen + 1), [.dial(gen + 1)])
    }

    func testTenDropsWithoutAdmissionGiveUp() {
        var dialer = Dialer()
        _ = dialer.connect()
        var gen = 1
        for _ in 0..<10 {
            guard case let .wait(next, _) = dialer.dropped(gen).last else { return XCTFail("no retry") }
            _ = dialer.retryDue(next)
            gen = next
        }
        XCTAssertEqual(dialer.dropped(gen), [.hangUp, .status(.lost("Lost connection to the games hub"))])
    }

    func testAdmissionStartsTheRetryCountOver() {
        var dialer = Dialer()
        _ = dialer.connect()
        var gen = 1
        for _ in 0..<9 {
            guard case let .wait(next, _) = dialer.dropped(gen).last else { return XCTFail("no retry") }
            _ = dialer.retryDue(next)
            gen = next
        }
        _ = dialer.admitted(gen)
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting), .wait(gen + 1, 2)])
    }

    func testConnectDuringTheWaitDialsNow() {
        var (dialer, gen) = connected()
        _ = dialer.dropped(gen)
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(gen + 2)])
        // The wait that was pending is stale.
        XCTAssertEqual(dialer.retryDue(gen + 1), [])
    }

    // MARK: - Refusals

    // A refusal that ends the stream is the end: no redial, and its
    // reason stands until the player asks again.
    func testATerminalRefusalHangsUpAndStaysLost() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.refused(gen, exception: "TooManySessions", message: "slow down"),
                       [.hangUp, .status(.lost("slow down"))])
        XCTAssertEqual(dialer.dropped(gen), [])
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(gen + 2)])
    }

    func testARefusalWithoutAMessageIsNamedByItsShape() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.refused(gen, exception: "Forbidden", message: nil), [.hangUp, .status(.lost("Forbidden"))])
    }

    // A seat conflict clears on the next dial: the drop that follows retries.
    func testASeatConflictIsRetried() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.refused(gen, exception: "SeatConflict", message: nil), [])
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting), .wait(gen + 1, 2)])
    }

    // The hub closes a wire whose seat another connection took with 4001.
    // Dialing back would take the seat back, and the two would trade it
    // every two seconds; the web stops, and so does this.
    func testASupersededWireStopsDialing() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen, superseded: true), [.hangUp, .status(.lost("Playing on another device"))])
        XCTAssertEqual(dialer.retryDue(gen + 1), [])
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(gen + 1)])
    }

    // MARK: - Coming back to the foreground

    // A backgrounded app's socket may be dead without having said so: back
    // in the foreground, an admitted connection dials afresh, and the
    // resume token takes the seat again.
    func testResumingRedialsEvenWhenAdmitted() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.resume(), [.hangUp, .status(.connecting), .dial(gen + 1)])
        XCTAssertEqual(dialer.dropped(gen), [])
    }

    // Resuming a connection the player hung up, or one that was lost,
    // leaves it as it is: Try again is theirs to press.
    func testResumingAnIdleOrLostConnectionDoesNothing() {
        var dialer = Dialer()
        XCTAssertEqual(dialer.resume(), [])
        var (lost, gen) = connected()
        _ = lost.refused(gen, exception: "TooManySessions", message: "slow down")
        XCTAssertEqual(lost.resume(), [])
    }

    // MARK: - Hanging up, and stale news

    func testDisconnectHangsUpAndSilencesEverythingInFlight() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.disconnect(), [.hangUp, .status(.idle)])
        XCTAssertEqual(dialer.dropped(gen), [])
        XCTAssertEqual(dialer.admitted(gen), [])
        XCTAssertEqual(dialer.refused(gen, exception: "TooManySessions", message: nil), [])
    }

    // Disconnect then connect while a dial or a wait is in flight: the old
    // one's failure, or its timer, must not start a second dial.
    func testAReconnectOutlivesTheDialAndTheWaitItReplaced() {
        var dialer = Dialer()
        _ = dialer.connect()
        _ = dialer.disconnect()
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(3)])
        XCTAssertEqual(dialer.dropped(1), [])

        XCTAssertEqual(dialer.dropped(3), [.status(.connecting), .wait(4, 2)])
        _ = dialer.disconnect()
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(6)])
        XCTAssertEqual(dialer.retryDue(4), [])
    }
}
