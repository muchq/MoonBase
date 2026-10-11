$version: "2.0"

namespace moonbase.pong

use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.chess#ChessEntrants
use moonbase.chess#ChessForfeit
use moonbase.chess#ChessPlayRoundRobin
use moonbase.chess#ChessRoundRobinsRequest
use moonbase.chess#ChessStandings
use moonbase.chess#ChessWithdraw

// Pong's vocabulary: two seats, one game after another, on smallcat's flat
// court or in the 3D tunnel. It rides the room's Play stream as one `pong`
// member per direction and reuses the shared lifecycle shapes, except
// startGame, which names the variant. Pong is the hub's one real-time
// game: the rally runs on the instance that last committed the table and
// reaches the seats and watchers as frames, while the score commits like
// any game's moves. Shapes are prefixed `Pong`: codegen flattens every
// namespace into one.

/// The pong envelope on the command stream.
structure PongCommand {
    @required
    move: PongMove
}

union PongMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: PongStartGame
    leaveGame: LeaveGame
    paddle: PongPaddle
    addBot: PongAddBot
    watch: PongWatch
    createRoundRobin: PongCreateRoundRobin
    withdraw: ChessWithdraw
    forfeit: ChessForfeit
    roundRobins: ChessRoundRobinsRequest
    playRoundRobin: ChessPlayRoundRobin
}

/// Starts a pong round robin among the room's members, as chess's
/// createRoundRobin does: everyone plays everyone once, on `variant`. The
/// room's round robins of either game count toward its 16. Withdraw,
/// forfeit, roundRobins and playRoundRobin are chess's shapes and rules on
/// pong's round robins; each refuses a round robin of the other game.
/// A pairing's table plays its one game on the round robin's variant,
/// starting when its second seat fills, and that game is the pairing's
/// result.
structure PongCreateRoundRobin {
    @required
    entrants: ChessEntrants

    /// smallcat | 3d; absent, smallcat.
    @length(min: 1, max: 16)
    variant: String
}

/// Starts the game once both seats are filled, on `variant`; on a table
/// whose game has ended, either seat starts the next. Absent, smallcat.
structure PongStartGame {
    /// smallcat | 3d
    @length(min: 1, max: 16)
    variant: String
}

/// Where the seat wants its paddle's centre, in PongCourt units. The
/// paddle chases it at the court's playerSpeed per tick and stays on its
/// end, so a client sends where the pointer is, or a step past the paddle
/// for a held key. Refused from no seat at a pong table; ignored with no
/// game in play, since a pointer keeps moving between games. Each session
/// may send sixty a second, in bursts of up to 120; past that a paddle is
/// refused with "slow down".
structure PongPaddle {
    /// Across the court; left out, the seat's last. The flat court has no
    /// width, and ignores it.
    x: Double

    @required
    y: Double
}

/// Seats smallcat's own opponent in the empty second seat of a table not
/// yet started, at `level`. Only the table's one player may; the bot
/// leaves with them.
structure PongAddBot {
    @required
    @range(min: 1, max: 3)
    level: Integer
}

/// Watches a pong table in the room, in any phase, from no seat: the
/// answer is a gameState, and from then on every gameState, frame and
/// gameEnded the seats get. As chess's watch: refused while seated;
/// watching another table switches to it; sitting down, leaveGame (acked
/// with gameLeft), leaving the room and a closed socket each end it.
structure PongWatch {
    @required
    gameId: String
}

/// The pong envelope on the event stream.
structure PongEvent {
    @required
    update: PongUpdate
}

union PongUpdate {
    gameJoined: PongGameJoined
    gameState: PongGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    gameEnded: PongGameEnded
    gameLeft: GameLeft
    frame: PongFrame
    roundRobin: PongRoundRobin
    roundRobins: PongRoundRobins
}

/// The room's pong round robins.
structure PongRoundRobins {
    @required
    roundRobins: PongRoundRobinList
}

list PongRoundRobinList {
    member: PongRoundRobin
}

/// A pong round robin as every member sees it, sent to them all whenever
/// it changes.
structure PongRoundRobin {
    @required
    roundRobinId: String

    @required
    creator: String

    /// In the order the pairings were drawn from.
    @required
    entrants: ChessEntrants

    /// smallcat | 3d
    @required
    variant: String

    /// In round order.
    @required
    pairings: PongPairings

    @required
    withdrawn: ChessEntrants

    /// Best first, as chess ranks them; pong has no draws.
    @required
    standings: ChessStandings
}

list PongPairings {
    member: PongPairing
}

structure PongPairing {
    /// From 1. Rounds order a player's list; pairings are played whenever
    /// both players are free.
    @required
    round: Integer

    /// The seat that opens the pairing's table plays end 0, whichever
    /// of the two it is.
    @required
    first: String

    @required
    second: String

    /// Absent until decided.
    winner: String

    /// The result was recorded by the creator, not played.
    @required
    forfeit: Boolean

    /// Never to be played: an entrant in it withdrew before it was.
    @required
    voided: Boolean

    /// The table playing it now; absent otherwise.
    gameId: String
}

structure PongGameJoined {
    @required
    view: PongView
}

structure PongGameState {
    @required
    view: PongView
}

/// A game is over; the view before it carries the same result. The table
/// stays for the next game unless a leave ended it.
structure PongGameEnded {
    @required
    result: PongResult
}

/// The table as both seats and every watcher see it.
structure PongView {
    @required
    gameId: String

    /// waiting | playing | ended | closed: ended between games, closed
    /// once a seat left the table.
    @required
    phase: String

    /// smallcat | 3d; absent while waiting.
    variant: String

    /// The court the game is played on; absent while waiting.
    court: PongCourt

    /// Seat order: the first seat defends end 0, the second end 1.
    @required
    players: PongPlayers

    /// Present once the game is over.
    result: PongResult

    /// Every game this table finished, in order.
    @required
    scoreSheet: PongScoreSheet

    /// The round robin whose pairing this table plays, from its opening to
    /// its close: one game, on the round robin's variant, with no next
    /// game or bot. Absent at any other table.
    roundRobinId: String
}

list PongPlayers {
    member: PongPlayer
}

structure PongPlayer {
    @required
    playerId: String

    /// Lives left this game; absent while waiting.
    lives: Integer

    /// The seat is a bot's, its playerId naming its level
    /// ("smallcat@2"). Absent for a person.
    bot: Boolean
}

/// The court in its own units: the ball travels along z, from end 0 at
/// z = 0 to end 1 at z = depth; a paddle moves across x and y on its end,
/// `inset` in from it. The flat court has no width. y runs down the screen,
/// as smallcat's canvas draws it; a view from behind end 1 looks back
/// along z, so it mirrors x, and a paddle it sends is in court x.
structure PongCourt {
    @required
    width: Double

    @required
    height: Double

    @required
    depth: Double

    @required
    paddleWidth: Double

    @required
    paddleHeight: Double

    @required
    ballRadius: Double

    @required
    inset: Double

    /// The farthest a paddle moves in a tick, on each axis.
    @required
    playerSpeed: Double

    /// Ticks a second; velocities are per tick.
    @required
    ticksPerSecond: Integer
}

/// Where the rally stands, sent to the seats and watchers about thirty
/// times a second while a game is in play, and never after its end.
/// Frames are droppable: a slow reader gets the newest, and a client moves
/// the ball on by its velocity between them.
structure PongFrame {
    @required
    gameId: String

    /// Counts up while a game is in play, on from the clock's tick whenever
    /// an instance takes the rally up, so it never runs backwards: a frame
    /// with a lower tick than one already drawn is stale.
    @required
    tick: Long

    @required
    ball: PongVector

    /// Per tick.
    @required
    velocity: PongVector

    /// Paddle centres, in seat order.
    @required
    paddles: PongPaddles

    /// Ticks until the ball is served; 0 once it is in flight.
    @required
    serveIn: Integer
}

structure PongVector {
    @required
    x: Double

    @required
    y: Double

    @required
    z: Double
}

list PongPaddles {
    member: PongPoint
}

structure PongPoint {
    @required
    x: Double

    @required
    y: Double
}

structure PongResult {
    /// The winning seat. May name a seat that has since left.
    @required
    winner: String

    /// points | abandoned
    @required
    ending: String
}

list PongScoreSheet {
    member: PongScoreLine
}

/// One finished game.
structure PongScoreLine {
    @required
    winner: String

    /// As PongResult.ending.
    @required
    ending: String
}
