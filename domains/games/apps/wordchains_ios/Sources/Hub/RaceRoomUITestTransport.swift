#if targetEnvironment(simulator)
/// Local hub frames for UI screenshots and smoke tests.
@MainActor
final class RaceRoomUITestTransport: HubTransport {
    var events: HubTransportEvents?

    private let viewer = "rowan"
    private let friend = "alex"
    private let roomCode = "AB12"
    private let gameId = "W1"

    func connect() {
        events?.status(.connected)
        events?.frame(.sessionReady(SessionReady(playerId: viewer, roomId: nil)))
    }

    func disconnect() { events?.status(.idle) }
    func resume() {}

    func send(_ command: HubCommand) {
        switch command {
        case .createRoom:
            events?.frame(.roomState(room(seated: [])))
        case let .joinRoom(code) where code == roomCode:
            events?.frame(.roomState(room(seated: [friend])))
        case .wordchain(.createGame):
            seat([viewer])
        case let .wordchain(.joinGame(id)) where id == gameId:
            seat([friend, viewer])
        case .leaveRoom:
            events?.frame(.roomLeft(roomCode))
        default:
            break
        }
    }

    private func room(seated: [String]) -> HubRoom {
        let seat = HubTable(game: "wordchain", gameId: gameId)
        let players = (seated.contains(friend) ? [friend, viewer] : [viewer]).map { id in
            HubRoomPlayer(playerId: id, connected: true, table: seated.contains(id) ? seat : nil)
        }
        let games = seated.isEmpty ? [] : [
            HubGameSummary(gameId: gameId, game: "wordchain", status: "waiting", playerCount: seated.count)
        ]
        return HubRoom(roomId: roomCode, players: players, games: games)
    }

    private func seat(_ players: [String]) {
        let runners = players.map {
            WordchainRunner(playerId: $0, words: 0, finishedAtMs: nil, left: false, ladder: [], wins: 0)
        }
        let view = WordchainView(
            gameId: gameId, phase: .waiting, start: nil, target: nil, par: nil,
            startedAtMs: nil, deadlineMs: nil, runners: runners, result: nil
        )
        events?.frame(.wordchain(.view(view)))
        events?.frame(.roomState(room(seated: players)))
    }
}
#endif
