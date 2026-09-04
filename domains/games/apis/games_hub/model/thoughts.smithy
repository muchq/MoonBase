$version: "2.0"

namespace moonbase.thoughts

use moonbase.games#CommandRejected
use moonbase.games#SeatConflict
use moonbase.games#SessionReady
use moonbase.games#Unauthenticated

// Thoughts (#79): the chill 3D vibe at muchq.com/thoughts, on the games
// hub. A world per room (#1490): each joined player is a position on the
// ground plane, a color, and a shape, and every change reaches everyone
// else in the same world. The hub relays; it simulates nothing and
// remembers nothing past the connection.

/// The one WebSocket session per player, on the same ticket GetSession
/// mints for golf. Invalid commands come back as commandRejected events
/// and change nothing; the modeled errors are terminal. A closed socket
/// is a player gone: presence is the whole game, so there is no reconnect
/// grace and nothing to resume.
@http(method: "POST", uri: "/games/v2/thoughts/play")
operation Think {
    input := {
        @required
        @httpQuery("ticket")
        ticket: String

        @httpPayload
        commands: ThoughtsCommands
    }
    output := {
        @httpPayload
        events: ThoughtsEvents
    }
    errors: [Unauthenticated, SeatConflict]
}

@streaming
union ThoughtsCommands {
    join: JoinWorld
    move: MoveTo
    shape: ChangeShape
    leave: LeaveWorld
}

/// Enter a world. Refused while already in one: leave first to respawn,
/// which is also how a color or a room changes.
structure JoinWorld {
    /// The room whose world to enter; absent, the public plaza — a
    /// well-known room, "plaza", so an unroomed join and a join naming it
    /// land together. Any other id names a world of its own; the room
    /// layer is not consulted (that is #1490's phase 3).
    roomId: String

    @required
    position: Vec3

    @required
    color: Vec3

    @required
    shape: Integer
}

/// A new position for a player in the world.
structure MoveTo {
    @required
    position: Vec3
}

/// A new shape for a player in the world.
structure ChangeShape {
    @required
    shape: Integer
}

/// Leave the world but keep the session; join again to respawn.
structure LeaveWorld {}

@streaming
union ThoughtsEvents {
    sessionReady: SessionReady
    worldState: WorldState
    playerJoined: PlayerJoined
    playerMoved: PlayerMoved
    shapeChanged: ShapeChanged
    playerLeft: PlayerLeft
    commandRejected: CommandRejected
}

/// Everyone already in the joined world, sent once to a joiner — before
/// anyone else hears their playerJoined, and never listing the joiner.
/// Empty when the world is.
structure WorldState {
    @required
    players: WorldPlayers
}

list WorldPlayers {
    member: WorldPlayer
}

/// Position is [x, 0, z] with x and z within ±50; color is [r, g, b] in
/// 0..1; shape is 0 (sphere), 1 (cube) or 2 (pyramid). The hub refuses
/// anything else, so a value here is always inside these bounds.
structure WorldPlayer {
    @required
    playerId: String

    @required
    position: Vec3

    @required
    color: Vec3

    @required
    shape: Integer
}

/// Fan-out to everyone else in the actor's world, and nobody outside it:
/// a session that has not joined hears nothing, and the actor never hears
/// its own echo.
structure PlayerJoined {
    @required
    player: WorldPlayer
}

structure PlayerMoved {
    @required
    playerId: String

    @required
    position: Vec3
}

structure ShapeChanged {
    @required
    playerId: String

    @required
    shape: Integer
}

/// A deliberate leave or a closed socket, alike.
structure PlayerLeft {
    @required
    playerId: String
}

/// [x, y, z], as three.js vectors serialize; doubles as an RGB triple.
/// Always three members — the hub refuses any other length.
list Vec3 {
    member: Double
}
