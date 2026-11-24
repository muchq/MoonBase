import XCTest
@testable import WordChains

// What the app does with each frame the hub sends, and what it sends for
// each thing the player does. The hub is the rules: nothing here checks a
// word, only what the views and refusals say.

private func runner(_ id: String, words: Int = 0, ladder: [String]? = nil, finishedAtMs: Int64? = nil, left: Bool = false, wins: Int = 0) -> WordchainRunner {
    WordchainRunner(playerId: id, words: words, finishedAtMs: finishedAtMs, left: left, ladder: ladder, wins: wins)
}

private func view(_ phase: WordchainPhase = .racing, gameId: String = "W1", start: String? = "cold", runners: [WordchainRunner]? = nil) -> WordchainView {
    WordchainView(
        gameId: gameId, phase: phase, start: start, target: start == nil ? nil : "warm", par: start == nil ? nil : 5,
        startedAtMs: start == nil ? nil : 0, deadlineMs: start == nil ? nil : 180_000,
        runners: runners ?? [runner("alice", words: 1, ladder: ["cord"]), runner("bob", words: 3)],
        result: nil, racesPlayed: 0
    )
}

private func room(_ id: String = "AB12", games: [HubGameSummary] = [], aliceAt table: HubTable? = nil) -> HubRoom {
    HubRoom(roomId: id, players: [HubRoomPlayer(playerId: "alice", connected: true, table: table)], games: games)
}

final class RaceStateTests: XCTestCase {

    // MARK: - Session and room

    func testSessionReadyKeepsTheResumedRoomAndDropsAnyOther() {
        var state = RaceState()
        state.apply(.roomState(room("AB12")))
        state.apply(.wordchain(.view(view())))
        state.apply(.sessionReady(SessionReady(playerId: "alice", resumed: true, roomId: "AB12")))
        XCTAssertEqual(state.playerId, "alice")
        XCTAssertEqual(state.room?.roomId, "AB12")
        // A seat that survived comes back as its own gameJoined; one that
        // didn't sends nothing, so the old table must not linger.
        XCTAssertNil(state.table)

        state.apply(.sessionReady(SessionReady(playerId: "alice", resumed: false, roomId: nil)))
        XCTAssertNil(state.room)
    }

    func testTheRoomOffersOnlyItsRaceTables() {
        var state = RaceState()
        state.apply(.roomState(room(games: [
            HubGameSummary(gameId: "K1", game: "chess", status: "waiting", playerCount: 1),
            HubGameSummary(gameId: "W1", game: "wordchain", status: "waiting", playerCount: 1),
            HubGameSummary(gameId: "G1", game: nil, status: "waiting", playerCount: 1),
            HubGameSummary(gameId: "W2", game: "wordchain", status: "racing", playerCount: 3),
        ])))
        XCTAssertEqual(state.raceTables.map(\.gameId), ["W1", "W2"])
    }

    func testLeavingTheRoomLeavesItsTable() {
        var state = RaceState()
        state.apply(.roomState(room()))
        state.apply(.wordchain(.view(view())))
        state.apply(.roomLeft("AB12"))
        XCTAssertNil(state.room)
        XCTAssertNil(state.table)
    }

    // MARK: - The table

    func testAViewIsTheTableAndAnswersWhateverWasWaiting() {
        var state = RaceState()
        state.sending = true
        state.opening = true
        state.apply(.wordchain(.view(view())))
        XCTAssertEqual(state.table, view())
        XCTAssertFalse(state.sending)
        XCTAssertFalse(state.opening)
    }

    func testARefusalAnswersWhateverWasWaitingAndSaysWhy() {
        var state = RaceState()
        state.apply(.wordchain(.view(view())))
        state.sending = true
        state.apply(.commandRejected("\"cxrd\" is not a word here"))
        XCTAssertFalse(state.sending)
        XCTAssertEqual(state.notice, "\"cxrd\" is not a word here")
        XCTAssertEqual(state.table, view())
    }

    func testGameLeftClearsTheTable() {
        var state = RaceState()
        state.apply(.wordchain(.view(view())))
        state.apply(.wordchain(.left))
        XCTAssertNil(state.table)
    }

    func testOtherUpdatesChangeNothing() {
        var state = RaceState()
        state.apply(.wordchain(.view(view())))
        let before = state
        state.apply(.wordchain(.other))
        state.apply(.ignored("roomChat"))
        XCTAssertEqual(state, before)
    }
}

// The session lives on the main actor; XCTest runs tests on the main
// thread, and stepping onto it in each test (rather than isolating the
// class) runs under both Apple's XCTest and swift-corelibs-xctest.
final class RaceSessionTests: XCTestCase {

    @MainActor
    private final class FakeTransport: HubTransport {
        var sent: [HubCommand] = []
        var connects = 0
        var disconnects = 0
        var events: HubTransportEvents?
        func connect() { connects += 1 }
        func disconnect() { disconnects += 1 }
        func send(_ command: HubCommand) { sent.append(command) }
    }

    @MainActor
    private func mount() -> (RaceSession, FakeTransport) {
        let transport = FakeTransport()
        let session = RaceSession(transport: transport)
        transport.events?.status(.connected)
        transport.events?.frame(.sessionReady(SessionReady(playerId: "alice", resumed: false, roomId: nil)))
        return (session, transport)
    }

    func testAWordIsSentTrimmedAndLowercasedAndHeldUntilAnswered() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            transport.events?.frame(.wordchain(.view(view())))
            session.play("  Card ")
            XCTAssertEqual(transport.sent.last, .wordchain(.play("card")))
            XCTAssertTrue(session.state.sending)
            session.play("   ")
            XCTAssertEqual(transport.sent.count, 1)
        }
    }

    func testARoomCodeIsJoinedAsTheWebTypesIt() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.joinRoom(" ab12 ")
            XCTAssertEqual(transport.sent, [.joinRoom("AB12")])
            session.joinRoom("  ")
            XCTAssertEqual(transport.sent.count, 1)
            XCTAssertEqual(session.state.notice, "Enter a room code")
        }
    }

    func testTableMovesSpeakTheRaceVocabulary() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.openTable()
            session.joinTable("W1")
            XCTAssertTrue(session.state.opening)
            session.start(letters: 3)
            session.retract()
            XCTAssertEqual(transport.sent, [
                .wordchain(.createGame), .wordchain(.joinGame("W1")), .wordchain(.startGame(letters: 3)), .wordchain(.retract),
            ])
        }
    }

    func testLeavingAnOpenTableAsksTheHubAndAClosedOneIsJustLetGo() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            transport.events?.frame(.wordchain(.view(view())))
            session.leaveTable()
            XCTAssertEqual(transport.sent.last, .wordchain(.leaveGame))
            XCTAssertNotNil(session.state.table)

            transport.events?.frame(.wordchain(.view(view(.closed))))
            transport.sent = []
            session.leaveTable()
            XCTAssertEqual(transport.sent, [])
            XCTAssertNil(session.state.table)
        }
    }

    func testPlayAgainOpensANewTable() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            transport.events?.frame(.wordchain(.view(view(.closed))))
            session.playAgain()
            XCTAssertEqual(transport.sent.last, .wordchain(.createGame))
            XCTAssertTrue(session.state.opening)
        }
    }

    // A refusal that ends the stream is a lost connection, except a seat
    // conflict: the next dial takes the seat back.
    func testATerminalRefusalIsALossButASeatConflictIsNot() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            transport.events?.frame(.refused(exception: "SeatConflict", message: nil))
            XCTAssertEqual(session.connection, .connected)
            transport.events?.frame(.refused(exception: "TooManySessions", message: "slow down"))
            XCTAssertEqual(session.connection, .lost("slow down"))
        }
    }

    func testConnectAndDisconnectReachTheTransport() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.connect()
            session.disconnect()
            XCTAssertEqual(transport.connects, 1)
            XCTAssertEqual(transport.disconnects, 1)
        }
    }
}

final class ReconnectPolicyTests: XCTestCase {
    // 2s x 10 sits inside the hub's 5-minute reconnect grace, as on the web.
    func testTenRetriesTwoSecondsApartThenGivesUp() {
        var policy = ReconnectPolicy()
        for _ in 0..<10 { XCTAssertEqual(policy.next(), 2.0) }
        XCTAssertNil(policy.next())
    }

    func testAdmissionResetsTheCount() {
        var policy = ReconnectPolicy()
        for _ in 0..<10 { _ = policy.next() }
        policy.admitted()
        XCTAssertEqual(policy.next(), 2.0)
    }
}
