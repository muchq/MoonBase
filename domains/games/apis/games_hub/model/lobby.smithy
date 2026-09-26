$version: "2.0"

namespace moonbase.lobby

// The lobby's world (#79, #1490): the chill 3D vibe at muchq.com/games
// (and /thoughts), on the games hub. A world per room: each joined player
// is a position on the room's surface (#1554), a color, and a shape, and
// every change reaches everyone else in the same world. The hub relays
// and settles positions onto the surface; it simulates nothing, and past
// the connection it remembers only the surface (stored) and the last 32
// tape splats (in process).
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
    setGeometry: SetGeometry
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
    geometryChanged: GeometryChanged
    playerLeft: PlayerLeft
    tape: TapeSplat
}

/// Enter a world. Refused while already in one: leave first to respawn,
/// which is also how a color changes.
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

/// Reshape the session's room for everyone in it (#1554): the room's
/// world takes this surface, and everyone standing in it is placed at
/// the nearest point of the new one. Any member may, standing or not;
/// refused for a geometry the hub cannot host.
structure SetGeometry {
    @required
    geometry: Geometry
}

/// Everyone already in the joined world, sent once to a joiner — before
/// anyone else hears their playerJoined, and never listing the joiner.
/// Empty when the world is. A full replacement: a client that respawns
/// draws this world and nothing it drew before.
structure WorldState {
    @required
    players: WorldPlayers

    /// The surface this world stands on: the room's choice at creation,
    /// until a setGeometry changes it.
    @required
    geometry: Geometry

    /// What is already on the glass, oldest first, so a joiner walks into
    /// a wall with something on it rather than a blank one. Absent off a
    /// glasshouse, and until the first event lands.
    ///
    /// Thirty-two. deja's own ring holds 200 because that is a
    /// reconnecting page's backlog; a wall is a mood, not a log, and a
    /// joiner who splats two hundred events at once shows a crowd nobody
    /// was there to watch arrive.
    tape: TapeSplats
}

/// The shape of a room's world: where its players stand. A room starts
/// with the one createRoom names (absent: the plane) and any member can
/// change it with setGeometry; the plaza starts flat and changes the
/// same way, for everyone in it, until the hub restarts.
union Geometry {
    /// The ground plane: positions are [x, 0, z] with x and z within ±50.
    plane: PlaneGeometry

    /// The inside of a sphere centred on the origin: positions are points
    /// on its wall. The hub snaps a position within one unit of the wall
    /// onto it and refuses one farther off.
    sphere: SphereGeometry

    /// The plane's floor inside four glass walls standing at its edges
    /// (#1554). Players stand on the floor under exactly the plane's
    /// rules — the glass is the boundary it already had, not a place to
    /// stand — and deja's tape splats onto the walls (see TapeSplat).
    ///
    /// The walls are as tall as the client draws them; no height rides
    /// the wire, because a splat's `v` is a fraction of whatever that is.
    /// The four are numbered as TapeSplat.wall names them.
    glasshouse: GlasshouseGeometry
}

structure PlaneGeometry {}

structure GlasshouseGeometry {}

structure SphereGeometry {
    /// At least 2, so an avatar can stand.
    @required
    radius: Double
}

list WorldPlayers {
    member: WorldPlayer
}

/// Position is on the world's surface (see Geometry: a flat surface's
/// [x, 0, z] within ±50, or a point on the sphere's wall); color is [r, g, b]
/// in 0..1; shape is 0 (sphere), 1 (cube) or 2 (pyramid). The hub refuses
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

/// The world changed shape under everyone standing in it, the actor
/// included: the new surface, and every player where the hub placed
/// them on it (the nearest point of the new surface to where they
/// stood). Replaces every position the client holds.
structure GeometryChanged {
    @required
    geometry: Geometry

    @required
    players: WorldPlayers

    /// What is already on the glass, on the same terms as WorldState.tape
    /// — a room that becomes a glasshouse gets its walls filled for the
    /// people already standing in it, not only for whoever joins next.
    /// Absent when the new surface has no glass, and until the first
    /// event lands.
    tape: TapeSplats
}

/// A deliberate leave or a closed socket, alike.
structure PlayerLeft {
    @required
    playerId: String
}

/// One deja event on the glass (#1554, #1150). The hub is deja's second
/// consumer and the tape is the world's, not the browser's: the hub polls
/// deja while — and only while — somebody is standing in a glasshouse,
/// and fans each new event to that world the way playerMoved goes out. A
/// client never subscribes to deja and never asks where the splat goes.
structure TapeSplat {
    /// deja's sequence number: the event's identity, and the only input
    /// to where it lands, so two clients in one room draw it on the same
    /// square inch without agreeing on anything but this.
    @required
    seq: Long

    /// Which wall: 0 (-z), 1 (+x), 2 (+z), 3 (-x).
    @required
    wall: Integer

    /// Along the wall, in [0, 1).
    @required
    u: Double

    /// Up the glass, in [0, 1) of however tall the client draws it.
    @required
    v: Double

    /// When deja scored it, in epoch seconds. A joiner is handed a ring
    /// that may be minutes old, so the age of a splat is the client's
    /// business and has to reach it.
    @required
    ts: Double

    /// The lane's recent tokens, oldest first — the context chips.
    @required
    context: TapeTokens

    /// The request the lane actually made.
    @required
    actual: String

    /// deja's judgement: "warmup", "expected", "anomaly" or "novel". A
    /// string, not an enum, so a verdict deja learns to say reaches the
    /// wall instead of failing the hub's parse.
    @required
    verdict: String

    /// Each predictor's top guess; absent when it had none to offer.
    bigram: TapeGuess

    net: TapeGuess
}

structure TapeGuess {
    @required
    token: String

    /// The probability the predictor gave it, in 0..1.
    @required
    p: Double
}

list TapeTokens {
    member: String
}

list TapeSplats {
    member: TapeSplat
}

/// [x, y, z], as three.js vectors serialize; doubles as an RGB triple.
/// Always three members — the hub refuses any other length.
list Vec3 {
    member: Double
}
