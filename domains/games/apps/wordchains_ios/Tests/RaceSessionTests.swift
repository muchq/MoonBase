import XCTest
#if canImport(FoundationNetworking)
import FoundationNetworking
#endif
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
        result: nil
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
        state.apply(.sessionReady(SessionReady(playerId: "alice", roomId: "AB12")))
        XCTAssertEqual(state.playerId, "alice")
        XCTAssertEqual(state.room?.roomId, "AB12")
        // A seat that survived comes back as its own gameJoined; one that
        // didn't sends nothing, so the old table must not linger.
        XCTAssertNil(state.table)

        state.apply(.sessionReady(SessionReady(playerId: "alice", roomId: nil)))
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

    func testRepeatedWordRefusalsCountEvenWithOtherRunnerUpdates() {
        var state = RaceState()
        state.apply(.sessionReady(SessionReady(playerId: "alice", roomId: nil)))
        state.apply(.wordchain(.view(view())))

        state.sending = true
        state.apply(.wordchain(.view(view(runners: [runner("alice", words: 1), runner("bob", words: 4)]))))
        XCTAssertTrue(state.sending)

        state.apply(.commandRejected("That word is not allowed"))
        XCTAssertEqual(state.wordRefusals, 1)
        XCTAssertFalse(state.sending)

        state.sending = true
        state.apply(.commandRejected("That word is not allowed"))
        XCTAssertEqual(state.wordRefusals, 2)

        state.apply(.commandRejected("Cannot start a race"))
        XCTAssertEqual(state.wordRefusals, 2)
    }

    func testOwnWordChangeSettlesThePendingCommand() {
        var state = RaceState()
        state.apply(.sessionReady(SessionReady(playerId: "alice", roomId: nil)))
        state.apply(.wordchain(.view(view())))
        state.sending = true

        state.apply(.wordchain(.view(view(runners: [runner("alice", words: 2), runner("bob", words: 3)]))))
        XCTAssertFalse(state.sending)
        XCTAssertEqual(state.wordRefusals, 0)
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
        var resumes = 0
        var events: HubTransportEvents?
        func connect() { connects += 1 }
        func disconnect() { disconnects += 1 }
        func resume() { resumes += 1 }
        func send(_ command: HubCommand) { sent.append(command) }
    }

    @MainActor
    private func mount() -> (RaceSession, FakeTransport) {
        let transport = FakeTransport()
        let session = RaceSession(transport: transport)
        transport.events?.status(.connected)
        transport.events?.frame(.sessionReady(SessionReady(playerId: "alice", roomId: nil)))
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

    func testCreatingARoomPreparesItsRaceExactlyOnce() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.createRoom()
            session.createRoom()
            XCTAssertEqual(transport.sent, [.createRoom])

            transport.events?.frame(.roomState(room()))
            transport.events?.frame(.roomState(room()))
            XCTAssertEqual(transport.sent, [.createRoom, .wordchain(.createGame)])
            XCTAssertTrue(session.state.opening)

            transport.events?.frame(.wordchain(.view(view(.waiting))))
            XCTAssertEqual(session.state.table?.phase, .waiting)
            XCTAssertFalse(session.state.opening)
            session.start(letters: 3)
            session.retract()
            XCTAssertEqual(Array(transport.sent.suffix(2)), [.wordchain(.startGame(letters: 3)), .wordchain(.retract)])
        }
    }

    func testJoiningARoomUsesTheFirstRaceWithAnOpenSeat() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            let games = [
                HubGameSummary(gameId: "K1", game: "chess", status: "waiting", playerCount: 1),
                HubGameSummary(gameId: "W1", game: "wordchain", status: "racing", playerCount: 2),
                HubGameSummary(gameId: "W2", game: "wordchain", status: "waiting", playerCount: 4),
                HubGameSummary(gameId: "W3", game: "wordchain", status: "over", playerCount: 2),
                HubGameSummary(gameId: "W4", game: "wordchain", status: "waiting", playerCount: 1),
            ]
            session.joinRoom("AB12")
            transport.events?.frame(.roomState(room(games: Array(games.dropLast()))))
            XCTAssertTrue(session.state.raceEntryBlocked)
            XCTAssertEqual(transport.sent, [.joinRoom("AB12")])
            transport.events?.frame(.roomState(room(games: games)))
            transport.events?.frame(.roomState(room(games: games)))
            XCTAssertFalse(session.state.raceEntryBlocked)
            XCTAssertEqual(transport.sent, [.joinRoom("AB12"), .wordchain(.joinGame("W4"))])
        }
    }

    func testEmptyExistingRoomCanBeSetUpAndARefusalAllowsRetry() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.joinRoom("AB12")
            transport.events?.frame(.roomState(room()))
            XCTAssertEqual(transport.sent, [.joinRoom("AB12")])
            session.prepareRace()
            transport.events?.frame(.commandRejected("Try again"))
            transport.events?.frame(.roomState(room()))
            XCTAssertEqual(transport.sent, [.joinRoom("AB12"), .wordchain(.createGame)])
            XCTAssertFalse(session.state.opening)
            session.prepareRace()
            XCTAssertEqual(transport.sent.last, .wordchain(.createGame))
            XCTAssertTrue(session.state.opening)

            transport.events?.frame(.commandRejected("Try again"))
            let race = HubGameSummary(gameId: "W1", game: "wordchain", status: "waiting", playerCount: 1)
            transport.events?.frame(.roomState(room(games: [race])))
            XCTAssertEqual(transport.sent, [.joinRoom("AB12"), .wordchain(.createGame), .wordchain(.createGame)])
            session.prepareRace()
            XCTAssertEqual(transport.sent.last, .wordchain(.joinGame("W1")))
            XCTAssertNil(session.state.notice)
        }
    }

    func testResumingWithASeatWaitsForTheHubRaceView() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            let seat = HubTable(game: "wordchain", gameId: "W1")
            let race = HubGameSummary(gameId: "W1", game: "wordchain", status: "waiting", playerCount: 1)
            transport.events?.frame(.sessionReady(SessionReady(playerId: "alice", roomId: "AB12")))
            transport.events?.frame(.roomState(room(games: [race], aliceAt: seat)))
            XCTAssertTrue(transport.sent.isEmpty)
            transport.events?.frame(.wordchain(.view(view(.waiting))))
            XCTAssertEqual(session.state.table?.gameId, "W1")
        }
    }

    func testLeavingTheRoomAlsoLeavesItsRaceWithoutJoiningAgain() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            let race = HubGameSummary(gameId: "W1", game: "wordchain", status: "waiting", playerCount: 1)
            transport.events?.frame(.roomState(room()))
            transport.events?.frame(.wordchain(.view(view(.waiting))))
            session.leaveRoom()
            transport.events?.frame(.wordchain(.left))
            transport.events?.frame(.roomState(room(games: [race])))
            XCTAssertEqual(transport.sent, [.leaveRoom])
            transport.events?.frame(.roomLeft("AB12"))
            XCTAssertNil(session.state.room)
            XCTAssertNil(session.state.table)
        }
    }

    func testPlayAgainSetsUpARaceInTheSameRoom() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            transport.events?.frame(.roomState(room()))
            transport.events?.frame(.wordchain(.view(view(.closed))))
            session.prepareRace()
            XCTAssertEqual(transport.sent, [.wordchain(.createGame)])
            XCTAssertTrue(session.state.opening)
            XCTAssertEqual(session.state.room?.roomId, "AB12")
        }
    }

    func testADropAnswersWhateverWasWaiting() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.createRoom()
            XCTAssertEqual(session.state.roomRequest, .create)
            transport.events?.status(.connecting)
            XCTAssertNil(session.state.roomRequest)

            transport.events?.status(.connected)
            session.createRoom()
            transport.events?.frame(.roomState(room()))
            XCTAssertTrue(session.state.opening)
            transport.events?.status(.connecting)
            XCTAssertFalse(session.state.opening)

            transport.events?.status(.connected)
            transport.events?.frame(.wordchain(.view(view())))
            session.play("card")
            XCTAssertTrue(session.state.sending)
            transport.events?.status(.connecting)
            XCTAssertFalse(session.state.sending)
        }
    }

    // MARK: - Quick race

    func testAQuickRaceSeeksOnTheChosenLettersAndWaitsForItsTable() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.seek(letters: 5)
            XCTAssertEqual(transport.sent, [.wordchain(.seek(letters: 5))])
            XCTAssertTrue(session.state.opening)
            transport.events?.frame(.roomState(room(HubEndpoints.quickRoomId)))
            transport.events?.frame(.wordchain(.view(view(.closed))))
            session.seek(letters: nil)
            XCTAssertEqual(transport.sent.last, .wordchain(.seek(letters: nil)))
            XCTAssertNil(session.state.seekLetters)
        }
    }

    func testInThePoolPlayAgainSeeksAgainAndLeavingLeavesThePool() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.seek(letters: 3)
            transport.events?.frame(.roomState(room(HubEndpoints.quickRoomId)))
            XCTAssertTrue(session.state.inQuickRace)
            transport.events?.frame(.wordchain(.view(view(.closed))))
            transport.sent = []
            session.prepareRace()
            XCTAssertEqual(transport.sent, [.wordchain(.seek(letters: 3))])
            transport.events?.frame(.wordchain(.view(view())))
            transport.sent = []
            session.leaveRoom()
            XCTAssertEqual(transport.sent, [.leaveRoom])
        }
    }

    func testTransportStatusIsTheSessionsConnection() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            XCTAssertEqual(session.connection, .connected)
            transport.events?.status(.lost("slow down"))
            XCTAssertEqual(session.connection, .lost("slow down"))
        }
    }

    func testResumeReachesTheTransport() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.resume()
            XCTAssertEqual(transport.resumes, 1)
        }
    }

    // A word or table asked for and never sent waits on nothing: the
    // screen lets it go and says why.
    func testAFailedSendLetsGoOfWhatWasWaitingAndSaysWhy() {
        MainActor.assumeIsolated {
            let (session, transport) = mount()
            session.createRoom()
            XCTAssertEqual(session.state.roomRequest, .create)
            transport.events?.sendFailed()
            XCTAssertNil(session.state.roomRequest)
            XCTAssertEqual(session.state.notice, "Not sent: reconnecting")

            session.createRoom()
            transport.events?.frame(.roomState(room()))
            XCTAssertTrue(session.state.opening)
            transport.events?.sendFailed()
            XCTAssertFalse(session.state.opening)
            XCTAssertEqual(session.state.notice, "Not sent: reconnecting")

            transport.events?.frame(.wordchain(.view(view())))
            session.dismissNotice()
            session.retract()
            XCTAssertTrue(session.state.sending)
            transport.events?.sendFailed()
            XCTAssertFalse(session.state.sending)
            XCTAssertEqual(session.state.notice, "Not sent: reconnecting")
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

// The real socket's half of a failed send: with no socket to write to, a
// command is reported failed at once, and nothing else is said.
final class HubSocketSendTests: XCTestCase {
    func testASendWithNoSocketIsReportedFailed() {
        MainActor.assumeIsolated {
            final class NoTokens: ResumeTokenStore {
                func load() -> String? { nil }
                func save(_ token: String) -> Bool { true }
                func clear() {}
            }
            let socket = HubSocket(tokens: NoTokens())
            var failed = 0, said: [String] = []
            socket.events = HubTransportEvents(
                status: { said.append("status \($0)") },
                frame: { said.append("frame \($0)") },
                sendFailed: { failed += 1 }
            )
            socket.send(.wordchain(.play("card")))
            XCTAssertEqual(failed, 1)
            XCTAssertEqual(said, [])
        }
    }
}

// The real socket's mint names this build: what the hub counts it under
// (games_hub client_tag.h).
final class HubSocketMintTests: XCTestCase {
    /// Answers every request with a 503, keeping what was asked.
    final class Recording: URLProtocol {
        static var asked: [URL] = []
        static var seen: XCTestExpectation?
        override class func canInit(with request: URLRequest) -> Bool { true }
        override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
        override func startLoading() {
            if let url = request.url {
                Self.asked.append(url)
                Self.seen?.fulfill()
                Self.seen = nil
            }
            let response = HTTPURLResponse(url: request.url!, statusCode: 503, httpVersion: nil, headerFields: nil)!
            client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
            client?.urlProtocolDidFinishLoading(self)
        }
        override func stopLoading() {}
    }

    func testTheMintAsksAsThisBuildOfTheApp() {
        final class NoTokens: ResumeTokenStore {
            func load() -> String? { nil }
            func save(_ token: String) -> Bool { true }
            func clear() {}
        }
        let config = URLSessionConfiguration.ephemeral
        config.protocolClasses = [Recording.self]
        Recording.asked = []
        Recording.seen = expectation(description: "a mint")
        let socket = MainActor.assumeIsolated {
            HubSocket(playURL: URL(string: "wss://hub.test/games/v2/play")!, urlSession: URLSession(configuration: config), tokens: NoTokens())
        }
        MainActor.assumeIsolated { socket.connect() }
        wait(for: [Recording.seen ?? expectation(description: "already seen")], timeout: 5)
        MainActor.assumeIsolated { socket.disconnect() }
        XCTAssertEqual(Recording.asked.first?.absoluteString,
                       "https://hub.test/games/v2/session?client=\(HubEndpoints.appClient)")
        XCTAssertTrue(HubEndpoints.appClient.hasPrefix("ios"), HubEndpoints.appClient)
    }
}

final class ReconnectPolicyTests: XCTestCase {
    func testCeilingsDoubleFromHalfASecondToThirtyThenGiveUp() {
        var policy = ReconnectPolicy()
        var ceilings: [TimeInterval] = []
        while let ceiling = policy.next() { ceilings.append(ceiling) }
        XCTAssertEqual(ceilings, [0.5, 1, 2, 4, 8, 16, 30, 30, 30, 30])
    }

    func testResetStartsTheCountOver() {
        var policy = ReconnectPolicy()
        for _ in 0..<10 { _ = policy.next() }
        policy.reset()
        XCTAssertEqual(policy.next(), 0.5)
    }
}

final class RetryAfterTests: XCTestCase {
    func testSecondsAreRead() {
        XCTAssertEqual(HubEndpoints.retryAfter("60"), 60)
        XCTAssertEqual(HubEndpoints.retryAfter(" 5 "), 5)
    }

    // An HTTP-date, a negative or garbage is no answer: the backoff stands.
    func testAnythingElseIsNoAnswer() {
        for value in [nil, "", "-1", "soon", "Wed, 21 Oct 2026 07:28:00 GMT", "1.5"] {
            XCTAssertNil(HubEndpoints.retryAfter(value), "\(String(describing: value))")
        }
    }
}
