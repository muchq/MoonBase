$version: "2.0"

namespace moonbase.games

use alloy#simpleRestJson
use moonbase.castle#CastleCommand
use moonbase.castle#CastleEvent
use moonbase.golf#GolfCommand
use moonbase.golf#GolfEvent
use moonbase.lobby#LobbyCommand
use moonbase.lobby#LobbyEvent

/// The games hub (#79): one service, one session identity, one room layer,
/// and one stream, Play, on which the lobby (#1490), golf, and castle
/// (#77) each ride as one envelope member per direction.
@simpleRestJson
@title("Games Hub")
service GamesHub {
    version: "2026-07-21"
    operations: [GetSession, Play]
}

/// The one WebSocket session per player: commands up, events down. The
/// ticket rides the upgrade GET as a query member (browsers cannot set
/// upgrade headers); the gate checks it pre-101 and the handler spends it
/// (single use). Invalid moves never end the stream — they come back as
/// commandRejected events; the modeled errors below are terminal.
@http(method: "POST", uri: "/games/v2/play")
operation Play {
    input := {
        @required
        @httpQuery("ticket")
        ticket: String

        @httpPayload
        commands: GameCommands
    }
    output := {
        @httpPayload
        events: GameEvents
    }
    errors: [Unauthenticated, SeatConflict]
}

/// The room layer's own commands, then one envelope per tenant: the
/// lobby's world, golf's table, castle's table.
@streaming
union GameCommands {
    createRoom: CreateRoom
    joinRoom: JoinRoom
    leaveRoom: LeaveRoom
    getRoomState: GetRoomState
    chat: Chat
    lobby: LobbyCommand
    golf: GolfCommand
    castle: CastleCommand
}

@streaming
union GameEvents {
    sessionReady: SessionReady
    roomState: RoomState
    roomLeft: RoomLeft
    roomChat: ChatMessage
    roomChatHistory: ChatHistory
    commandRejected: CommandRejected
    lobby: LobbyEvent
    golf: GolfEvent
    castle: CastleEvent
}

/// Session identity is game-agnostic: the route carries no game segment,
/// and the minted credentials open any stream on this hub.
@http(method: "POST", uri: "/games/v2/session")
operation GetSession {
    input: SessionRequest
    output: SessionCredentials
}

/// The game-agnostic room layer (MoonBase#79 by way of #1187): session
/// identity, room lifecycle, and chat. Apart from GameSummary.game — the
/// one word that names a table's game for the lobby — nothing in this
/// namespace knows which game a table plays; a game contributes only its
/// own vocabulary, the way moonbase.golf and moonbase.castle do.

/// GetSession's input: a resume token exchanges for a fresh ticket and
/// the same playerId; absent or expired mints a fresh player.
structure SessionRequest {
    resumeToken: String
}

/// The two credentials of the blessed browser auth (smithy-cpp ADR-0018):
/// a single-use short-lived ticket spent on the play upgrade, and a
/// multi-use resume token. playerId is whimsical and doubles as the
/// display name.
structure SessionCredentials {
    @required
    playerId: String

    @required
    ticket: String

    @required
    resumeToken: String
}

structure CreateRoom {}

structure JoinRoom {
    @required
    roomId: String
}

structure LeaveRoom {}

structure GetRoomState {}

/// Room-scoped chat; fan-out is the roomChat event.
structure Chat {
    @required
    text: String
}

/// First event on every stream: who you are, whether this seat resumed a
/// parked session (ADR-0020 grace), and the room you are still in if so.
structure SessionReady {
    @required
    playerId: String

    @required
    resumed: Boolean

    roomId: String
}

structure RoomState {
    @required
    roomId: String

    @required
    players: PlayerInfos

    @required
    games: GameSummaries
}

list PlayerInfos {
    member: PlayerInfo
}

/// A room member with their room-scoped running stats, and the table
/// they are at, if any.
structure PlayerInfo {
    @required
    playerId: String

    @required
    connected: Boolean

    /// The table this member is at — pending or in play — absent while
    /// idle. A member at a table is still in the room (chat, presence),
    /// so the lobby (#1490) reads this to tell who is free. A finished
    /// table leaves the room's list, and this with it.
    table: Table

    @required
    gamesPlayed: Integer

    @required
    gamesWon: Integer

    @required
    totalScore: Integer
}

/// A room member's table: which game, which table.
structure Table {
    /// golf | castle, as GameSummary.game spells it.
    @required
    game: String

    @required
    gameId: String
}

list GameSummaries {
    member: GameSummary
}

/// Enough of a game for the lobby: join it or see why you cannot.
structure GameSummary {
    @required
    gameId: String

    /// Which game the table plays: golf | castle.
    @required
    game: String

    @required
    status: String

    @required
    playerCount: Integer
}

/// Ack for a deliberate leaveRoom; the remaining members see roomState.
structure RoomLeft {
    @required
    roomId: String
}

/// One committed chat message. messageId is assigned by the server and
/// rises with commit order within a room; it is the only ordering key,
/// and sentAtUnixMillis is display time that may not agree with it.
///
/// Delivery is at-least-once, so the same message can arrive more than
/// once — through a redelivery, or through roomChatHistory overlapping
/// live events on join. Consumers deduplicate by messageId rather than
/// treating an overlap as an error.
structure ChatMessage {
    @required
    messageId: Long

    @required
    playerId: String

    @required
    text: String

    @required
    sentAtUnixMillis: Long
}

list ChatMessages {
    member: ChatMessage
}

/// The bounded replay a joining or resuming stream receives, ascending
/// by messageId and capped at the room's retained history. It goes only
/// to the stream that just arrived, after that stream's roomState, and
/// never to members already in the room.
///
/// It is sent exactly once per join or resume, even when the room has no
/// messages — an empty list means "history loaded, and it is empty", so
/// clients need not infer emptiness from silence. Two exceptions: a
/// freshly created room sends nothing (no history exists, and creating
/// is not joining), and a failed history load sends nothing (delivery is
/// best-effort; live messages catch the client up). A client must
/// therefore tolerate absence, but may treat arrival as authoritative.
structure ChatHistory {
    @required
    messages: ChatMessages
}

/// A command the hub declined — wrong state, unknown room, illegal move.
/// In-band and non-fatal; the stream continues.
structure CommandRejected {
    @required
    reason: String
}

/// Game lifecycle within a room — create/join/start/leave and their
/// announcements carry no game-specific content, so any game reuses them,
/// as do the card and player-list shapes below.
/// Creates a game in the current room and seats the creator.
structure CreateGame {}

structure JoinGame {
    @required
    gameId: String
}

structure StartGame {}

structure LeaveGame {}

/// A game was created and is open to join. Distinct from gameStarted,
/// which fires when play actually begins. createdBy lets the creator's
/// client tell its own echo apart from other players' creations.
structure GameCreated {
    @required
    gameId: String

    @required
    createdBy: String
}

/// Play has begun: seats are locked and the game's opening is dealt.
structure GameStarted {}

structure TurnChanged {
    @required
    playerId: String
}

/// Ack for a deliberate leaveGame; remaining players see a state update.
structure GameLeft {
    @required
    gameId: String
}

/// The ticket did not spend: expired, already used, or never minted. The
/// gate catches most of these pre-101; this is the race's terminal shape.
@error("client")
@httpError(401)
structure Unauthenticated {
    message: String
}

/// The player already holds a live seat (ADR-0022 admission refused).
/// Reconnect after an abrupt loss resumes instead — this fires only while
/// the old wire is still healthy.
@error("client")
@httpError(409)
structure SeatConflict {
    message: String
}

/// Ranks A 2..10 J Q K, suits ♠ ♥ ♦ ♣ — the v1 wire's card language,
/// which the UI already renders.
structure Card {
    @required
    rank: String

    @required
    suit: String
}

list CardIndexes {
    member: Integer
}

list PlayerIds {
    member: String
}
