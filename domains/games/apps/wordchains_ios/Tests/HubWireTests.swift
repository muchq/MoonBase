import XCTest
@testable import WordChains

// The games hub's JSON-text frames (smithy-cpp ADR-0018), as
// model/games.smithy and model/wordchain.smithy spell them and the web
// lobby (muchq.github.io src/utils/hubStream.ts) sends them.
final class HubWireTests: XCTestCase {

    private func text(_ command: HubCommand) -> String {
        String(decoding: command.encoded(), as: UTF8.self)
    }

    private func frame(_ json: String) throws -> HubFrame {
        try HubFrame.decode(Data(json.utf8))
    }

    // MARK: - Commands

    func testRoomCommandsAreBare() {
        XCTAssertEqual(text(.createRoom), #"{"event":"createRoom","payload":{}}"#)
        XCTAssertEqual(text(.joinRoom("AB12")), #"{"event":"joinRoom","payload":{"roomId":"AB12"}}"#)
        XCTAssertEqual(text(.leaveRoom), #"{"event":"leaveRoom","payload":{}}"#)
    }

    func testWordchainMovesRideTheirEnvelope() {
        XCTAssertEqual(text(.wordchain(.createGame)), #"{"event":"wordchain","payload":{"move":{"createGame":{}}}}"#)
        XCTAssertEqual(text(.wordchain(.joinGame("W1"))), #"{"event":"wordchain","payload":{"move":{"joinGame":{"gameId":"W1"}}}}"#)
        XCTAssertEqual(text(.wordchain(.startGame(letters: 5))), #"{"event":"wordchain","payload":{"move":{"startGame":{"letters":5}}}}"#)
        XCTAssertEqual(text(.wordchain(.leaveGame)), #"{"event":"wordchain","payload":{"move":{"leaveGame":{}}}}"#)
        XCTAssertEqual(text(.wordchain(.play("cord"))), #"{"event":"wordchain","payload":{"move":{"play":{"word":"cord"}}}}"#)
        XCTAssertEqual(text(.wordchain(.retract)), #"{"event":"wordchain","payload":{"move":{"retract":{}}}}"#)
    }

    // MARK: - Frames

    func testSessionReadyNamesThePlayerAndAnyResumedRoom() throws {
        XCTAssertEqual(
            try frame(#"{"event":"sessionReady","payload":{"playerId":"bouncy-quokka","resumed":true,"roomId":"AB12"}}"#),
            .sessionReady(SessionReady(playerId: "bouncy-quokka", roomId: "AB12"))
        )
        XCTAssertEqual(
            try frame(#"{"event":"sessionReady","payload":{"playerId":"p","resumed":false}}"#),
            .sessionReady(SessionReady(playerId: "p", roomId: nil))
        )
    }

    // A room carries more than a race needs (geometry, stats, terms):
    // what it does not read, it ignores.
    func testRoomStateReadsPlayersTablesAndIgnoresTheRest() throws {
        let json = #"""
        {"event":"roomState","payload":{"roomId":"AB12","geometry":{"kind":"plane"},
          "players":[{"playerId":"alice","connected":true,"gamesPlayed":3,"gamesWon":1,"totalScore":0,
                      "table":{"game":"wordchain","gameId":"W1"}},
                     {"playerId":"bob","connected":false,"gamesPlayed":0,"gamesWon":0,"totalScore":0}],
          "games":[{"gameId":"W1","game":"wordchain","status":"waiting","playerCount":1},
                   {"gameId":"K1","game":"chess","status":"playing","playerCount":2,"terms":"3+2"}]}}
        """#
        XCTAssertEqual(try frame(json), .roomState(HubRoom(
            roomId: "AB12",
            players: [
                HubRoomPlayer(playerId: "alice", connected: true, table: HubTable(game: "wordchain", gameId: "W1")),
                HubRoomPlayer(playerId: "bob", connected: false, table: nil),
            ],
            games: [
                HubGameSummary(gameId: "W1", game: "wordchain", status: "waiting", playerCount: 1),
                HubGameSummary(gameId: "K1", game: "chess", status: "playing", playerCount: 2),
            ]
        )))
    }

    func testRefusalsLeavesAndTerminalExceptions() throws {
        XCTAssertEqual(try frame(#"{"event":"commandRejected","payload":{"reason":"\"cxrd\" is not a word here"}}"#),
                       .commandRejected("\"cxrd\" is not a word here"))
        XCTAssertEqual(try frame(#"{"event":"roomLeft","payload":{"roomId":"AB12"}}"#), .roomLeft("AB12"))
        XCTAssertEqual(try frame(#"{"exception":"TooManySessions","payload":{"message":"slow down"}}"#),
                       .refused(exception: "TooManySessions", message: "slow down"))
        XCTAssertEqual(try frame(#"{"exception":"SeatConflict","payload":{}}"#),
                       .refused(exception: "SeatConflict", message: nil))
    }

    // Chat, the world, voice and every other game are the web lobby's:
    // the app has no use for them and reads past them.
    func testEventsTheAppDoesNotPlayAreIgnored() throws {
        XCTAssertEqual(try frame(#"{"event":"roomChat","payload":{"messageId":"m","playerId":"p","text":"hi","sentAtUnixMillis":1}}"#),
                       .ignored("roomChat"))
        XCTAssertEqual(try frame(#"{"event":"chess","payload":{"update":{"gameStarted":{}}}}"#), .ignored("chess"))
        XCTAssertEqual(try frame(#"{"event":"lobby","payload":{"update":{"playerLeft":{"playerId":"p"}}}}"#), .ignored("lobby"))
    }

    func testARacingViewHidesOtherLaddersAndCarriesTheClock() throws {
        let json = #"""
        {"event":"wordchain","payload":{"update":{"gameState":{"view":{
          "gameId":"W1","phase":"racing","start":"cold","target":"warm","par":5,
          "startedAtMs":1700000000000,"deadlineMs":1700000180000,"lexicon":"enable",
          "runners":[{"playerId":"alice","words":1,"left":false,"ladder":["cord"],"wins":0},
                     {"playerId":"bob","words":3,"left":false,"wins":2}],
          "racesPlayed":0}}}}}
        """#
        XCTAssertEqual(try frame(json), .wordchain(.view(WordchainView(
            gameId: "W1", phase: .racing, start: "cold", target: "warm", par: 5,
            startedAtMs: 1_700_000_000_000, deadlineMs: 1_700_000_180_000,
            runners: [
                WordchainRunner(playerId: "alice", words: 1, finishedAtMs: nil, left: false, ladder: ["cord"], wins: 0),
                WordchainRunner(playerId: "bob", words: 3, finishedAtMs: nil, left: false, ladder: nil, wins: 2),
            ],
            result: nil
        ))))
    }

    // A hub from before practice (#1730) sends no flag: a real race.
    func testAViewSaysWhetherItIsPractice() throws {
        func practice(_ flag: String) throws -> Bool? {
            let json = #"{"event":"wordchain","payload":{"update":{"gameState":{"view":{"gameId":"W1","phase":"racing","runners":[],"racesPlayed":0"# + flag + "}}}}}"
            guard case let .wordchain(.view(view)) = try frame(json) else { return nil }
            return view.isPractice
        }
        XCTAssertEqual(try practice(#","practice":true"#), true)
        XCTAssertEqual(try practice(#","practice":false"#), false)
        XCTAssertEqual(try practice(""), false)
    }

    func testAnOverViewCarriesTheResult() throws {
        let json = #"""
        {"event":"wordchain","payload":{"update":{"gameJoined":{"view":{
          "gameId":"W1","phase":"over","start":"cold","target":"warm","par":5,
          "runners":[{"playerId":"bob","words":4,"finishedAtMs":1700000009000,"left":true,"ladder":["word","worm","warm"],"wins":1}],
          "result":{"ending":"timeout","winners":["bob"],"standings":["bob"]},"racesPlayed":1}}}}}
        """#
        guard case let .wordchain(.view(view)) = try frame(json) else { return XCTFail("not a view") }
        XCTAssertEqual(view.phase, .over)
        XCTAssertEqual(view.result, WordchainResult(ending: .timeout, winners: ["bob"]))
        XCTAssertEqual(view.runners.first?.finishedAtMs, 1_700_000_009_000)
        XCTAssertEqual(view.runners.first?.left, true)
    }

    func testTableLifecycleUpdates() throws {
        XCTAssertEqual(try frame(#"{"event":"wordchain","payload":{"update":{"gameLeft":{"gameId":"W1"}}}}"#), .wordchain(.left))
        XCTAssertEqual(try frame(#"{"event":"wordchain","payload":{"update":{"gameCreated":{"gameId":"W2","createdBy":"bob"}}}}"#),
                       .wordchain(.other))
        XCTAssertEqual(try frame(#"{"event":"wordchain","payload":{"update":{"gameEnded":{"result":{"ending":"finished","winners":[],"standings":[]}}}}}"#),
                       .wordchain(.other))
    }

    func testSessionMintIsReadFromItsJSON() throws {
        let session = try JSONDecoder().decode(HubSession.self, from: Data(#"{"playerId":"p","ticket":"t","resumeToken":"r"}"#.utf8))
        XCTAssertEqual(session, HubSession(playerId: "p", ticket: "t", resumeToken: "r"))
    }

    func testEndpointsFollowThePlayURL() {
        let play = URL(string: "wss://api.muchq.com/games/v2/play")!
        XCTAssertEqual(HubEndpoints.sessionURL(play: play).absoluteString, "https://api.muchq.com/games/v2/session")
        XCTAssertEqual(HubEndpoints.socketURL(play: play, ticket: "a b+c").absoluteString,
                       "wss://api.muchq.com/games/v2/play?ticket=a%20b%2Bc")
        let local = URL(string: "ws://localhost:2015/games/v2/play")!
        XCTAssertEqual(HubEndpoints.sessionURL(play: local).absoluteString, "http://localhost:2015/games/v2/session")
    }
}
