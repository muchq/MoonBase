$version: "2.0"

namespace moonbase.games

use alloy#simpleRestJson
use moonbase.castle#CastleCommand
use moonbase.castle#CastleEvent
use moonbase.chess#ChessCommand
use moonbase.chess#ChessEvent
use moonbase.golf#GolfCommand
use moonbase.golf#GolfEvent
use moonbase.lobby#Geometry
use moonbase.lobby#LobbyCommand
use moonbase.lobby#LobbyEvent
use moonbase.rummy#RummyCommand
use moonbase.rummy#RummyEvent
use moonbase.voice#VoiceCommand
use moonbase.voice#VoiceEvent

/// The games hub (#79): one service, one session identity, one room layer,
/// and one stream, Play, on which the lobby (#1490), voice (#1590), golf,
/// castle (#77), rummy (#245) and chess each ride as one envelope member
/// per direction.
@simpleRestJson
@title("Games Hub")
service GamesHub {
    version: "2026-07-21"
    operations: [GetSession, Play, ExportChessGames]
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
/// lobby's world, the room's voice, golf's, castle's, rummy's and chess's
/// tables.
@streaming
union GameCommands {
    createRoom: CreateRoom
    joinRoom: JoinRoom
    leaveRoom: LeaveRoom
    getRoomState: GetRoomState
    chat: Chat
    lobby: LobbyCommand
    voice: VoiceCommand
    golf: GolfCommand
    castle: CastleCommand
    rummy: RummyCommand
    chess: ChessCommand
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
    voice: VoiceEvent
    golf: GolfEvent
    castle: CastleEvent
    rummy: RummyEvent
    chess: ChessEvent
}

/// The public chess feed (moonbase.chess#ChessPublish): every game that
/// ended while its room was published, ended within the last 30 days, as
/// PGN in archive order, at most 100 past `after`, one blank line between
/// games. No ticket, and no room named anywhere: [Site] is the game's own
/// URL, unique per game. Read on from the last game's archive id; ids
/// grow in archive order, but a game can land behind one already read,
/// so a reader that rereads a little and dedupes on [Site] misses none.
@readonly
@http(method: "GET", uri: "/games/v2/chess.pgn", code: 200)
operation ExportChessGames {
    input := {
        /// An archive id, exclusive: only games archived after it.
        @httpQuery("after")
        after: Long
    }
    output := {
        @required
        @httpHeader("Content-Type")
        contentType: String

        @required
        @httpPayload
        pgn: Blob
    }
}

/// Session identity is game-agnostic: the route carries no game segment,
/// and the minted credentials open any stream on this hub.
@http(method: "POST", uri: "/games/v2/session")
operation GetSession {
    input: SessionRequest
    output: SessionCredentials
}

/// The game-agnostic room layer (#79): session identity, room lifecycle,
/// and chat. Apart from GameSummary.game and Table.game — the word that
/// names a table's game for the lobby — nothing in this
/// namespace knows which game a table plays; a game contributes only its
/// own vocabulary, the way moonbase.golf, moonbase.castle, moonbase.rummy
/// and moonbase.chess do.

/// GetSession's input: a resume token exchanges for a fresh ticket and
/// the same playerId; absent or expired mints a fresh player.
structure SessionRequest {
    resumeToken: String
}

/// The two credentials of the blessed browser auth (opal-cpp ADR-0018):
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

structure CreateRoom {
    /// The surface the room's world stands on (lobby.smithy's Geometry);
    /// absent is the plane. Refused for a sphere too small to stand in.
    geometry: Geometry
}

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

    /// The surface the room's world stands on, so a joiner knows what to
    /// draw and where to stand before its first lobby join.
    @required
    geometry: Geometry
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
    /// golf | castle | rummy | chess, as GameSummary.game spells it.
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

    /// Which game the table plays: golf | castle | rummy | chess.
    @required
    game: String

    @required
    status: String

    @required
    playerCount: Integer

    /// Text the table's game supplies for the lobby to show as is: what a
    /// waiting table plays on (chess's challenge: "Standard starting
    /// position · 3+2"). Absent when there is nothing to say.
    terms: String
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

    /// The author. A bot's replies carry its reserved id, which no player
    /// id can equal: "microgpt" answering "@bot" (#1591), "mithril"
    /// answering "/wordchain". Clients show it as the reply's source.
    @required
    playerId: String

    /// True on a bot's replies, absent on everyone else's, so a client
    /// never has to know the bots' names.
    bot: Boolean

    @required
    text: String

    @required
    sentAtUnixMillis: Long

    /// On mithril's replies, the ladder `text` spells out.
    wordchain: Wordchain
}

/// A word ladder from `start` to `end`, one letter changed per rung.
structure Wordchain {
    @required
    start: String

    @required
    end: String

    /// Every rung, both ends included; absent when no ladder joins them.
    path: WordchainRungs
}

list WordchainRungs {
    member: String
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

/// Ranks A 2..10 J Q K, suits ♠ ♥ ♦ ♣ — the glyphs the UI renders.
/// Castle's and rummy's moves name cards in it too, so
/// it is written as well as read: the ten is "10" and not "T", the suit
/// is the glyph and not a letter, and the ranks are upper case. A card
/// spelled any other way names nothing and is refused as malformed.
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
