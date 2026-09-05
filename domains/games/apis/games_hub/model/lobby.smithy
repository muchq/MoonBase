$version: "2.0"

namespace moonbase.lobby

// The lobby's world (#79, #1490): the chill 3D vibe at muchq.com/games
// (and /thoughts), on the games hub. A world per room: each joined player
// is a position on the ground plane, a color, and a shape, and every
// change reaches everyone else in the same world. The hub relays; it
// simulates nothing and remembers nothing past the connection.
//
// The way in is the `lobby` member of the room's Play stream
// (games.smithy): the world is the session's room's, or the plaza's while
// unroomed, on the same socket as chat and the tables.

/// The lobby envelope on the room stream: exactly one action.
structure LobbyCommand {
    @required
    action: LobbyAction
}

union LobbyAction {
    join: JoinWorld
    move: MoveTo
    shape: ChangeShape
    leave: LeaveWorld
}

/// The lobby envelope on the event stream: exactly one update. The
/// session's own sessionReady and commandRejected are the stream's.
structure LobbyEvent {
    @required
    update: LobbyUpdate
}

union LobbyUpdate {
    worldState: WorldState
    playerJoined: PlayerJoined
    playerMoved: PlayerMoved
    shapeChanged: ShapeChanged
    playerLeft: PlayerLeft
}

/// Enter a world. Refused while already in one: leave first to respawn,
/// which is also how a color or a room changes.
structure JoinWorld {
    /// The world is the session's: its room's, or the plaza's — the
    /// well-known room "plaza" — while unroomed; a roomId here must name
    /// that or is refused.
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

/// Everyone already in the joined world, sent once to a joiner — before
/// anyone else hears their playerJoined, and never listing the joiner.
/// Empty when the world is. A full replacement: a client that respawns
/// draws this world and nothing it drew before.
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
