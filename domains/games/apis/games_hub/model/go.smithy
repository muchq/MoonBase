$version: "2.0"

namespace moonbase.go

use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#TurnChanged

// Go's vocabulary: two seats, one game after another, on a 9, 13 or 19
// board with a Fischer clock. Captures, no suicide, simple ko; two passes
// in a row open the scoring, where the seats mark dead groups and accept,
// and the game is scored by area (Tromp-Taylor, dead stones counted as
// empty) with komi to White.
// It rides the room's Play stream as one `go` member per direction and
// reuses the shared lifecycle shapes, except startGame, which names the
// board, komi and clock. Shapes are prefixed `Go`: codegen flattens every
// namespace into one.

/// The go envelope on the command stream.
structure GoCommand {
    @required
    move: GoMove
}

/// A move off turn, on an occupied point, a suicide, a ko recapture, or
/// one on a finished game is refused in band (commandRejected). A move
/// that arrives after the mover's time ran out is not played: the game
/// ends on time.
union GoMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: GoStartGame
    leaveGame: LeaveGame
    play: GoPlay
    pass: GoPass
    resign: GoResign
    watch: GoWatch
    markDead: GoMarkDead
    accept: GoAccept
}

/// Seats two and opens an empty board, Black's clock running; the first
/// game's Black is the table's creator. On a table whose game has ended,
/// either seat starts the next one, colours swapped. Absent fields take
/// the defaults: 19 by 19, komi 6.5, ten minutes and five seconds.
structure GoStartGame {
    /// 9, 13 or 19.
    @range(min: 9, max: 19)
    size: Integer

    /// Added to White's score, a multiple of 0.5 from -150 to 150.
    komi: Double

    /// Each side's time, 30 to 3600.
    @range(min: 30, max: 3600)
    initialSeconds: Integer

    /// Added to a side's clock once it moves or passes, 0 to 60.
    @range(min: 0, max: 60)
    incrementSeconds: Integer
}

/// A stone on the point at column `x` and row `y`, from the board's
/// top-left corner (GoView.board[y][x]).
structure GoPlay {
    @required
    @range(min: 0, max: 18)
    x: Integer

    @required
    @range(min: 0, max: 18)
    y: Integer
}

/// The seat on turn passes; a second pass in a row opens the scoring
/// (phase `scoring`), where a pass is refused and a play by the seat on
/// turn drops the marks and plays on.
structure GoPass {}

/// Either seat, in the scoring: the group of stones at (x, y) dead, or
/// alive again if it was marked. Withdraws both seats' acceptance.
structure GoMarkDead {
    @required
    @range(min: 0, max: 18)
    x: Integer

    @required
    @range(min: 0, max: 18)
    y: Integer
}

/// Either seat, in the scoring, accepts the marks; once both have, the
/// game ends on the score.
structure GoAccept {}

/// Either seat, on turn or not.
structure GoResign {}

/// Watches a go table in the room, in any phase, from no seat, on
/// chess's terms: the answer is a gameState, and from then on every
/// gameState and gameEnded the seats get.
structure GoWatch {
    @required
    gameId: String
}

/// The go envelope on the event stream.
structure GoEvent {
    @required
    update: GoUpdate
}

union GoUpdate {
    gameJoined: GoGameJoined
    gameState: GoGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    turnChanged: TurnChanged
    gameEnded: GoGameEnded
    gameLeft: GameLeft
}

structure GoGameJoined {
    @required
    view: GoView
}

structure GoGameState {
    @required
    view: GoView
}

structure GoGameEnded {
    @required
    result: GoResult
}

/// One view serves both seats and every watcher: go hides nothing.
structure GoView {
    @required
    gameId: String

    /// waiting | playing | scoring (after two passes) | ended (between
    /// games) | closed (a seat left).
    @required
    phase: String

    /// The board's side, once started.
    size: Integer

    komi: Double

    /// Black's seat first once started; in join order while waiting.
    @required
    players: GoPlayers

    /// One string per row, top to bottom, one character per point left to
    /// right: `.` empty, `b` Black, `w` White. Empty while waiting.
    @required
    board: GoRows

    /// black | white; absent once the game is over.
    sideToMove: String

    currentPlayerId: String

    /// Stones each side has taken.
    captures: GoCaptures

    /// The game's last move, absent before the first.
    lastMove: GoLastMove

    @required
    moveCount: Integer

    clock: GoClock

    /// The stones marked dead, while scoring and in a game it scored;
    /// absent when there are none.
    dead: GoPoints

    /// Present only in phase `scoring`.
    scoring: GoScoring

    /// The game's result once it is over.
    result: GoResult

    @required
    scoreSheet: GoScoreSheet
}

list GoPlayers {
    member: GoPlayer
}

structure GoPlayer {
    @required
    playerId: String

    /// black | white, once started.
    color: String
}

list GoRows {
    member: String
}

structure GoCaptures {
    @required
    black: Integer

    @required
    white: Integer
}

structure GoLastMove {
    /// black | white: who moved.
    @required
    color: String

    @required
    pass: Boolean

    /// The point played; absent for a pass.
    x: Integer

    y: Integer
}

list GoPoints {
    member: GoPoint
}

structure GoPoint {
    @required
    x: Integer

    @required
    y: Integer
}

structure GoScoring {
    /// The seats that accept the marks as they stand.
    @required
    acceptedBy: GoPlayerIds

    /// The count if the marks were accepted now.
    @required
    score: GoScore
}

list GoPlayerIds {
    member: String
}

structure GoClock {
    @required
    blackMs: Long

    @required
    whiteMs: Long

    @required
    initialMs: Long

    @required
    incrementMs: Long
}

structure GoResult {
    /// Absent for a draw (an even score).
    winner: String

    winnerColor: String

    /// score | resignation | timeout | abandoned.
    @required
    ending: String

    /// The area count, dead stones counted as empty and komi included in
    /// White's, for a game ended on the score.
    score: GoScore
}

structure GoScore {
    @required
    black: Double

    @required
    white: Double
}

list GoScoreSheet {
    member: GoScoreLine
}

structure GoScoreLine {
    winner: String

    @required
    ending: String
}
