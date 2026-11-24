import Foundation

// The games hub's one stream (domains/games/apis/games_hub), the part a
// word ladder race needs. JSON-text frames both ways (smithy-cpp
// ADR-0018): {"event": "<member>", "payload": {...}} up and down, and
// {"exception": "<shape>", "payload": {"message": ...}} for a refusal
// that ends the stream. Shapes follow model/games.smithy and
// model/wordchain.smithy; chat, the world, voice and the other games ride
// the same stream and are read past.

/// What `POST /games/v2/session` mints: a single-use ticket for the
/// socket, and a resume token that is the player.
struct HubSession: Decodable, Equatable {
    let playerId: String
    let ticket: String
    let resumeToken: String
}

enum HubEndpoints {
    /// The socket subprotocol that selects JSON-text frames.
    static let subprotocol = "smithy.eventstream.v1+json"
    static let production = URL(string: "wss://api.muchq.com/games/v2/play")!

    /// The mint lives beside the play socket: same host, http(s) for ws(s).
    static func sessionURL(play: URL) -> URL {
        var parts = URLComponents(url: play, resolvingAgainstBaseURL: false)!
        parts.scheme = play.scheme == "wss" ? "https" : "http"
        parts.path = "/games/v2/session"
        parts.query = nil
        return parts.url!
    }

    /// The mint's body: the resume token, to come back as the same player,
    /// or nothing, to be a new one.
    static func mintBody(resumeToken: String?) -> Data {
        try! JSONSerialization.data(withJSONObject: resumeToken.map { ["resumeToken": $0] } ?? [String: String]())
    }

    static func socketURL(play: URL, ticket: String) -> URL {
        var parts = URLComponents(url: play, resolvingAgainstBaseURL: false)!
        // Percent-encode as encodeURIComponent does, '+' included.
        var allowed = CharacterSet.urlQueryAllowed
        allowed.remove(charactersIn: "+&=")
        parts.percentEncodedQuery = "ticket=" + (ticket.addingPercentEncoding(withAllowedCharacters: allowed) ?? ticket)
        return parts.url!
    }
}

// MARK: - Commands

enum WordchainMove: Equatable {
    case createGame
    case joinGame(String)
    /// The first race once two are seated, and each next once one is over;
    /// the hub's default clock (180 s).
    case startGame(letters: Int)
    case leaveGame
    case play(String)
    case retract
}

enum HubCommand: Equatable {
    case createRoom
    case joinRoom(String)
    case leaveRoom
    case wordchain(WordchainMove)

    /// One frame, keys sorted so the same command is always the same bytes.
    func encoded() -> Data {
        let (event, payload) = parts
        let frame: [String: Any] = ["event": event, "payload": payload]
        return try! JSONSerialization.data(withJSONObject: frame, options: [.sortedKeys])
    }

    private var parts: (String, [String: Any]) {
        switch self {
        case .createRoom: return ("createRoom", [:])
        case let .joinRoom(roomId): return ("joinRoom", ["roomId": roomId])
        case .leaveRoom: return ("leaveRoom", [:])
        case let .wordchain(move): return ("wordchain", ["move": Self.move(move)])
        }
    }

    private static func move(_ move: WordchainMove) -> [String: Any] {
        switch move {
        case .createGame: return ["createGame": [String: Any]()]
        case let .joinGame(gameId): return ["joinGame": ["gameId": gameId]]
        case let .startGame(letters): return ["startGame": ["letters": letters]]
        case .leaveGame: return ["leaveGame": [String: Any]()]
        case let .play(word): return ["play": ["word": word]]
        case .retract: return ["retract": [String: Any]()]
        }
    }
}

// MARK: - The room

struct SessionReady: Decodable, Equatable {
    let playerId: String
    /// The room a resumed seat is back in.
    let roomId: String?
}

struct HubTable: Decodable, Equatable {
    let game: String
    let gameId: String
}

struct HubRoomPlayer: Decodable, Equatable {
    let playerId: String
    let connected: Bool
    /// The table they sit at, if any.
    let table: HubTable?
}

struct HubGameSummary: Decodable, Equatable {
    let gameId: String
    /// Absent on a row from before games were named, which is golf's.
    let game: String?
    let status: String
    let playerCount: Int
}

struct HubRoom: Decodable, Equatable {
    let roomId: String
    let players: [HubRoomPlayer]
    let games: [HubGameSummary]
}

// MARK: - The race

enum WordchainPhase: String, Decodable, Equatable {
    /// over: between races; closed: too few seats remain.
    case waiting, racing, over, closed
}

enum WordchainEnding: String, Decodable, Equatable {
    /// finished: everyone seated finished, or one did on par; timeout:
    /// the clock ran out; leave: too few seats stayed.
    case finished, timeout, leave
}

struct WordchainResult: Decodable, Equatable {
    let ending: WordchainEnding
    /// The fewest words, then the earliest finish; several share a win,
    /// none when nobody finished.
    let winners: [String]
}

struct WordchainRunner: Decodable, Equatable {
    let playerId: String
    /// Steps climbed: words played after the start, the target among them
    /// once finished.
    let words: Int
    let finishedAtMs: Int64?
    let left: Bool
    /// The viewer's own always; everyone's once the race is over.
    let ladder: [String]?
    let wins: Int
}

struct WordchainView: Decodable, Equatable {
    let gameId: String
    let phase: WordchainPhase
    /// The puzzle and its clock (epoch ms); absent while waiting. `par`
    /// counts a shortest ladder's words, both ends included.
    let start: String?
    let target: String?
    let par: Int?
    let startedAtMs: Int64?
    let deadlineMs: Int64?
    /// In the order they joined.
    let runners: [WordchainRunner]
    let result: WordchainResult?
    /// A lone seat racing for practice: nobody wins and nothing is
    /// recorded; a join ends it. Absent from a hub older than practice.
    var practice: Bool? = nil
    /// A shortest ladder, start and target included, once the race is
    /// over. Absent from a hub older than #1732, and for races stored
    /// before puzzles kept one.
    var solution: [String]? = nil

    var isPractice: Bool { practice == true }
}

/// A wordchain update, as much of it as the app acts on.
enum WordchainEvent: Equatable {
    /// gameJoined or gameState: the whole table, as the viewer may see it.
    case view(WordchainView)
    /// The viewer is no longer at the table.
    case left
    /// gameCreated, gameStarted, gameEnded: the next view says it all.
    case other
}

// MARK: - Frames

enum HubFrame: Equatable {
    case sessionReady(SessionReady)
    case roomState(HubRoom)
    case roomLeft(String)
    case commandRejected(String)
    case wordchain(WordchainEvent)
    /// A refusal that ends the stream.
    case refused(exception: String, message: String?)
    /// An event the app has no use for.
    case ignored(String)

    static func decode(_ data: Data) throws -> HubFrame {
        let decoder = JSONDecoder()
        let head = try decoder.decode(Head.self, from: data)
        if let exception = head.exception {
            let body = try decoder.decode(Body<Refusal>.self, from: data)
            return .refused(exception: exception, message: body.payload.message)
        }
        switch head.event {
        case "sessionReady":
            return .sessionReady(try decoder.decode(Body<SessionReady>.self, from: data).payload)
        case "roomState":
            return .roomState(try decoder.decode(Body<HubRoom>.self, from: data).payload)
        case "roomLeft":
            return .roomLeft(try decoder.decode(Body<RoomLeft>.self, from: data).payload.roomId)
        case "commandRejected":
            return .commandRejected(try decoder.decode(Body<Rejected>.self, from: data).payload.reason)
        case "wordchain":
            let update = try decoder.decode(Body<Envelope>.self, from: data).payload.update
            if let view = update.gameJoined?.view ?? update.gameState?.view { return .wordchain(.view(view)) }
            if update.gameLeft != nil { return .wordchain(.left) }
            return .wordchain(.other)
        default:
            return .ignored(head.event ?? "")
        }
    }

    private struct Head: Decodable {
        let event: String?
        let exception: String?
    }

    private struct Body<Payload: Decodable>: Decodable {
        let payload: Payload
    }

    private struct Refusal: Decodable { let message: String? }
    private struct RoomLeft: Decodable { let roomId: String }
    private struct Rejected: Decodable { let reason: String }
    private struct Viewed: Decodable { let view: WordchainView }
    private struct Left: Decodable { let gameId: String }

    private struct Update: Decodable {
        let gameJoined: Viewed?
        let gameState: Viewed?
        let gameLeft: Left?
    }

    private struct Envelope: Decodable { let update: Update }
}
