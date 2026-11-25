import XCTest
@testable import WordChains

// HubSocket's decisions, without a network: when to dial, when to wait
// and dial again, when to give up, and which dial's news still counts.
// Every dial has a generation; news from any other is stale.
// Jitter is pinned to the top of the range so a wait reads as its ceiling.
final class DialerTests: XCTestCase {

    private var clock = Date(timeIntervalSince1970: 0)

    private func makeDialer(jitter: Double = 1) -> Dialer {
        Dialer(jitter: { jitter }, now: { [unowned self] in self.clock })
    }

    private func connected() -> (Dialer, Int) {
        var dialer = makeDialer()
        let effects = dialer.connect()
        guard case let .dial(gen) = effects.last else { XCTFail("no dial"); return (dialer, 0) }
        _ = dialer.admitted(gen)
        return (dialer, gen)
    }

    func testConnectDialsOnceWhileADialIsUnderWay() {
        var dialer = makeDialer()
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(1)])
        XCTAssertEqual(dialer.connect(), [])
    }

    func testAdmissionIsConnected() {
        var dialer = makeDialer()
        _ = dialer.connect()
        XCTAssertEqual(dialer.admitted(1), [.status(.connected)])
    }

    // MARK: - Drops and retries

    func testADropWaitsThenDialsAgainUnderANewGeneration() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting), .wait(gen + 1, 0.5)])
        XCTAssertEqual(dialer.retryDue(gen + 1), [.dial(gen + 1)])
    }

    // Full jitter: a wait is a uniform share of its ceiling, so clients a
    // deploy dropped together don't come back together.
    func testAWaitIsTheJitteredShareOfItsCeiling() {
        var dialer = makeDialer(jitter: 0.25)
        _ = dialer.connect()
        XCTAssertEqual(dialer.dropped(1), [.status(.connecting), .wait(2, 0.125)])
    }

    func testRealJitterStaysWithinTheCeilingAndSpreads() {
        var waits = Set<TimeInterval>()
        for _ in 0..<50 {
            var dialer = Dialer()
            _ = dialer.connect()
            guard case let .wait(_, delay) = dialer.dropped(1).last else { return XCTFail("no retry") }
            XCTAssert((0...0.5).contains(delay), "\(delay)")
            waits.insert(delay)
        }
        XCTAssertGreaterThan(waits.count, 1)
    }

    func testWaitsDoubleUpToTheCap() {
        var dialer = makeDialer()
        _ = dialer.connect()
        var gen = 1
        var ceilings: [TimeInterval] = []
        for _ in 0..<10 {
            guard case let .wait(next, delay) = dialer.dropped(gen).last else { return XCTFail("no retry") }
            ceilings.append(delay)
            _ = dialer.retryDue(next)
            gen = next
        }
        XCTAssertEqual(ceilings, [0.5, 1, 2, 4, 8, 16, 30, 30, 30, 30])
    }

    func testTenDropsWithoutAdmissionGiveUp() {
        var dialer = makeDialer()
        _ = dialer.connect()
        var gen = 1
        for _ in 0..<10 {
            guard case let .wait(next, _) = dialer.dropped(gen).last else { return XCTFail("no retry") }
            _ = dialer.retryDue(next)
            gen = next
        }
        XCTAssertEqual(dialer.dropped(gen), [.hangUp, .status(.lost("Lost connection to the games hub"))])
    }

    // A hub that admits and then drops is not a connection that worked:
    // the backoff keeps climbing, and gives up as any other run would.
    func testAnAdmissionThatDoesNotHoldKeepsBackingOff() {
        var dialer = makeDialer()
        _ = dialer.connect()
        var gen = 1
        for expected: TimeInterval in [0.5, 1, 2, 4, 8, 16, 30, 30, 30, 30] {
            _ = dialer.admitted(gen)
            clock += 5
            guard case let .wait(next, delay) = dialer.dropped(gen).last else { return XCTFail("no retry") }
            XCTAssertEqual(delay, expected)
            _ = dialer.retryDue(next)
            gen = next
        }
        _ = dialer.admitted(gen)
        XCTAssertEqual(dialer.dropped(gen), [.hangUp, .status(.lost("Lost connection to the games hub"))])
    }

    func testAConnectionThatHeldStartsTheCountOver() {
        var dialer = makeDialer()
        _ = dialer.connect()
        var gen = 1
        for _ in 0..<9 {
            guard case let .wait(next, _) = dialer.dropped(gen).last else { return XCTFail("no retry") }
            _ = dialer.retryDue(next)
            gen = next
        }
        _ = dialer.admitted(gen)
        clock += ReconnectPolicy.stable
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting), .wait(gen + 1, 0.5)])
    }

    func testAConnectionDroppedJustShortOfHoldingKeepsBackingOff() {
        var (dialer, gen) = connected()
        guard case let .wait(next, _) = dialer.dropped(gen).last else { return XCTFail("no retry") }
        _ = dialer.retryDue(next)
        _ = dialer.admitted(next)
        clock += ReconnectPolicy.stable - 1
        XCTAssertEqual(dialer.dropped(next), [.status(.connecting), .wait(next + 1, 1)])
    }

    // The seat is held for the hub's grace (games_hub_main.cc, five
    // minutes): every wait at its ceiling, every mint at its timeout, still
    // comes back inside it.
    func testTheWholeScheduleFitsInsideTheHubsGrace() {
        var policy = ReconnectPolicy()
        var total: TimeInterval = 0
        while let ceiling = policy.next() { total += ceiling + HubSocket.mintTimeout }
        XCTAssertLessThan(total, 5 * 60)
    }

    // MARK: - The mint's answer

    // The hub's 429 names how long its window takes to open: retrying
    // sooner only spends the budget it is waiting on.
    func testARateLimitedMintWaitsTheHubsRetryAfter() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen, .mintRefused(status: 429, retryAfter: 60)), [.status(.connecting), .wait(gen + 1, 60)])
    }

    func testARetryAfterShorterThanTheBackoffWaitsTheBackoff() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen, .mintRefused(status: 429, retryAfter: 0.1)), [.status(.connecting), .wait(gen + 1, 0.5)])
    }

    func testARateLimitedMintWithoutRetryAfterBacksOff() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen, .mintRefused(status: 429, retryAfter: nil)), [.status(.connecting), .wait(gen + 1, 0.5)])
    }

    func testAServerErrorOrTimeoutIsRetried() {
        for status in [408, 500, 502, 503] {
            var (dialer, gen) = connected()
            XCTAssertEqual(dialer.dropped(gen, .mintRefused(status: status, retryAfter: nil)),
                           [.status(.connecting), .wait(gen + 1, 0.5)], "\(status)")
        }
    }

    // Any other 4xx says the same thing however often it is asked.
    func testARefusedMintIsTheEnd() {
        for status in [400, 403, 404] {
            var (dialer, gen) = connected()
            XCTAssertEqual(dialer.dropped(gen, .mintRefused(status: status, retryAfter: nil)),
                           [.hangUp, .status(.lost("The games hub turned this app away (\(status))"))], "\(status)")
            XCTAssertEqual(dialer.retryDue(gen + 1), [])
        }
    }

    // MARK: - The network

    // Offline, a dial can only fail: a drop waits for the network, spending
    // no attempt, and the network's return dials at once.
    func testOfflineADropWaitsForTheNetworkWithoutSpendingAnAttempt() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.pathChanged(online: false), [])
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting)])
        XCTAssertEqual(dialer.pathChanged(online: true), [.dial(gen + 1)])
        XCTAssertEqual(dialer.dropped(gen + 1), [.status(.connecting), .wait(gen + 2, 0.5)])
    }

    func testGoingOfflineDuringAWaitCancelsIt() {
        var (dialer, gen) = connected()
        _ = dialer.dropped(gen)
        XCTAssertEqual(dialer.pathChanged(online: false), [])
        XCTAssertEqual(dialer.retryDue(gen + 1), [])
        XCTAssertEqual(dialer.pathChanged(online: true), [.dial(gen + 2)])
    }

    // Many offline drops in a row still leave every attempt for when the
    // network is back.
    func testOfflineDropsNeverGiveUp() {
        var (dialer, gen) = connected()
        _ = dialer.pathChanged(online: false)
        for _ in 0..<20 {
            XCTAssertEqual(dialer.dropped(gen), [.status(.connecting)])
            XCTAssertEqual(dialer.resume(), [.hangUp, .status(.connecting), .dial(gen + 1)])
            gen += 1
        }
    }

    // Try again is the player's: offline or not, it dials.
    func testConnectWhileOfflineStillDials() {
        var (dialer, gen) = connected()
        _ = dialer.pathChanged(online: false)
        _ = dialer.dropped(gen)
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(gen + 1)])
    }

    func testTheNetworkMovesNothingWhileIdleLostOrAlreadyOnline() {
        var idle = makeDialer()
        XCTAssertEqual(idle.pathChanged(online: false), [])
        XCTAssertEqual(idle.pathChanged(online: true), [])

        var (lost, gen) = connected()
        _ = lost.refused(gen, exception: "TooManySessions", message: nil)
        _ = lost.pathChanged(online: false)
        XCTAssertEqual(lost.pathChanged(online: true), [])

        var (live, _) = connected()
        XCTAssertEqual(live.pathChanged(online: true), [])
    }

    func testDisconnectWhileOfflineStaysDisconnected() {
        var (dialer, gen) = connected()
        _ = dialer.pathChanged(online: false)
        _ = dialer.dropped(gen)
        XCTAssertEqual(dialer.disconnect(), [.hangUp, .status(.idle)])
        XCTAssertEqual(dialer.pathChanged(online: true), [])
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
        XCTAssertEqual(dialer.dropped(gen), [.status(.connecting), .wait(gen + 1, 0.5)])
    }

    // The hub closes a wire whose seat another connection took with 4001.
    // Dialing back would take the seat back, and the two would trade it
    // on every retry; the web stops, and so does this.
    func testASupersededWireStopsDialing() {
        var (dialer, gen) = connected()
        XCTAssertEqual(dialer.dropped(gen, .superseded), [.hangUp, .status(.lost("Playing on another device"))])
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
        var dialer = makeDialer()
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
        var dialer = makeDialer()
        _ = dialer.connect()
        _ = dialer.disconnect()
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(3)])
        XCTAssertEqual(dialer.dropped(1), [])

        XCTAssertEqual(dialer.dropped(3), [.status(.connecting), .wait(4, 0.5)])
        _ = dialer.disconnect()
        XCTAssertEqual(dialer.connect(), [.status(.connecting), .dial(6)])
        XCTAssertEqual(dialer.retryDue(4), [])
    }
}
