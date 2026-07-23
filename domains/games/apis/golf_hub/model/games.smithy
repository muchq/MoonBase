$version: "2.0"

namespace moonbase.games

/// The game-agnostic room layer (MoonBase#79 by way of #1187): session
/// identity, room lifecycle, and chat. Nothing in this namespace knows
/// which game a room is hosting — a future game reuses these shapes
/// verbatim and contributes only its own vocabulary, the way
/// moonbase.golf does.

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

/// A room member with their room-scoped running stats.
structure PlayerInfo {
    @required
    playerId: String

    @required
    connected: Boolean

    @required
    gamesPlayed: Integer

    @required
    gamesWon: Integer

    @required
    totalScore: Integer
}

list GameSummaries {
    member: GameSummary
}

/// Enough of a game for the lobby: join it or see why you cannot.
structure GameSummary {
    @required
    gameId: String

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

structure ChatMessage {
    @required
    playerId: String

    @required
    text: String
}

/// A command the hub declined — wrong state, unknown room, illegal move.
/// In-band and non-fatal; the stream continues.
structure CommandRejected {
    @required
    reason: String
}
