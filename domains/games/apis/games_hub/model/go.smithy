$version: "2.0"

namespace moonbase.go

use moonbase.chess#ChessEntrants
use moonbase.chess#ChessForfeit
use moonbase.chess#ChessPairings
use moonbase.chess#ChessPlayRoundRobin
use moonbase.chess#ChessRoundRobinsRequest
use moonbase.chess#ChessStandings
use moonbase.chess#ChessWithdraw
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
    challenge: GoStartGame
    createRoundRobin: GoCreateRoundRobin
    withdraw: ChessWithdraw
    forfeit: ChessForfeit
    roundRobins: ChessRoundRobinsRequest
    playRoundRobin: ChessPlayRoundRobin
    addBot: GoAddBot
}

/// GNU Go to the table's next seat, at a level (1 weakest, 10 strongest),
/// for a table not yet started; refused when the hub has no go engine, and
/// at a round robin's pairing. In the scoring it marks the stones the engine
/// counts dead, and accepts.
structure GoAddBot {
    @required
    @range(min: 1, max: 10)
    level: Integer
}

/// Starts a go round robin among the room's members, as chess's
/// createRoundRobin does: everyone plays everyone once, on `terms` (absent
/// fields take startGame's defaults; terms no game could open on are
/// refused). The room's round robins of every game count toward its 16.
/// Withdraw, forfeit, roundRobins and playRoundRobin are chess's shapes and
/// rules on go's round robins; each refuses a round robin of another game.
/// A pairing's table waits on the terms, starts when its second seat fills
/// with the pairing's Black to move, and its one game is the pairing's
/// result.
structure GoCreateRoundRobin {
    @required
    entrants: ChessEntrants

    terms: GoStartGame
}

/// Seats two and opens an empty board, Black's clock running; the first
/// game's Black is the table's creator (a round robin pairing's Black at
/// its table). On a table whose game has ended,
/// either seat starts the next one, colours swapped. Absent fields take
/// the defaults: 19 by 19, komi 6.5, ten minutes and five seconds.
/// As `challenge`, the same fields are the terms a waiting table's one
/// seat posts, before anyone joins; posting again replaces them. The
/// room's GameSummary.terms and every view show them, and the seat that
/// fills the table starts the game on them. Terms no game could start on
/// are refused as they are posted.
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

/// A challenge's or round robin's terms, as posted with the defaults
/// filled in.
structure GoTerms {
    @required
    size: Integer

    @required
    komi: Double

    @required
    initialSeconds: Integer

    @required
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

/// Watches a go table in the room, in any phase, from no seat, as chess's
/// watch does: the answer is a gameState, and from then on every
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
    roundRobin: GoRoundRobin
    roundRobins: GoRoundRobins
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

    /// The posted challenge's terms, or the round robin's at its pairing's
    /// table; absent once started, or with none posted.
    terms: GoTerms

    /// The round robin whose pairing this table plays, from its opening to
    /// its close: one game, on the round robin's terms, with no next game.
    /// Absent at any other table.
    roundRobinId: String

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

    /// A bot's seat (GNU Go).
    bot: Boolean
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

/// The room's go round robins.
structure GoRoundRobins {
    @required
    roundRobins: GoRoundRobinList
}

list GoRoundRobinList {
    member: GoRoundRobin
}

/// A go round robin as every member sees it, sent to them all whenever it
/// changes: chess's, its terms go's. A pairing's white and black are its
/// colours, and its result is white | black | draw.
structure GoRoundRobin {
    @required
    roundRobinId: String

    @required
    creator: String

    /// In the order the pairings were drawn from.
    @required
    entrants: ChessEntrants

    @required
    terms: GoTerms

    /// In round order.
    @required
    pairings: ChessPairings

    @required
    withdrawn: ChessEntrants

    /// Best first, as chess ranks them.
    @required
    standings: ChessStandings
}
