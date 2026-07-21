$version: "2.0"

namespace moonbase.golf

use alloy#simpleRestJson

/// The Golf hub, phase 1 (session + room lifecycle): the smithy-cpp
/// event-stream rebuild of games_ws_backend's golf hub. Game rules and
/// their wire vocabulary land in phase 2, after the rules-variant decision
/// (Go hub semantics vs libs/cards/golf) — this model deliberately stops
/// at rooms so no game shape freezes before that call.
@simpleRestJson
@title("Golf Hub")
service GolfHub {
    version: "2026-07-21"
    operations: [GetSession, Play]
}

/// Mints identity plus the two credentials of smithy-cpp ADR-0018's
/// blessed browser auth: a single-use short-lived ticket spent on the Play
/// upgrade, and a multi-use resume token a reconnect exchanges for a fresh
/// ticket (same playerId back). No resumeToken, or an expired one, mints a
/// fresh player.
@http(method: "POST", uri: "/golf/v1/session")
operation GetSession {
    input := {
        resumeToken: String
    }
    output := {
        @required
        playerId: String

        @required
        ticket: String

        @required
        resumeToken: String
    }
}

/// The one WebSocket session per player: commands up, events down. The
/// ticket rides the upgrade GET as a query member (browsers cannot set
/// upgrade headers); the gate checks it pre-101 and the handler spends it
/// (single use). Invalid moves never end the stream — they come back as
/// commandRejected events; the modeled errors below are terminal.
@http(method: "POST", uri: "/golf/v1/play")
operation Play {
    input := {
        @required
        @httpQuery("ticket")
        ticket: String

        @httpPayload
        commands: GolfCommands
    }
    output := {
        @httpPayload
        events: GolfEvents
    }
    errors: [Unauthenticated, SeatConflict]
}

@streaming
union GolfCommands {
    createRoom: CreateRoom
    joinRoom: JoinRoom
    leaveRoom: LeaveRoom
    getRoomState: GetRoomState
}

structure CreateRoom {}

structure JoinRoom {
    @required
    roomId: String
}

structure LeaveRoom {}

structure GetRoomState {}

@streaming
union GolfEvents {
    sessionReady: SessionReady
    roomState: RoomState
    roomLeft: RoomLeft
    commandRejected: CommandRejected
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
}

list PlayerInfos {
    member: PlayerInfo
}

structure PlayerInfo {
    @required
    playerId: String

    @required
    connected: Boolean
}

/// Ack for a deliberate leaveRoom; the remaining members see roomState.
structure RoomLeft {
    @required
    roomId: String
}

/// A command the hub declined — wrong state, unknown room, and later
/// illegal moves. In-band and non-fatal, matching the Go hub's error
/// messages; the stream continues.
structure CommandRejected {
    @required
    reason: String
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
