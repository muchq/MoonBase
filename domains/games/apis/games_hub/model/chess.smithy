$version: "2.0"

namespace moonbase.chess

use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#TurnChanged

// Chess's vocabulary: two seats, the full rules, a Fischer clock. It rides
// the room's Play stream as one `chess` member per direction and reuses
// the shared lifecycle shapes, except startGame, which names the clock.
// Starting positions are selected from the hub's server-owned setup
// catalog. Shape names carry the game's name: codegen flattens every
// namespace into one.

/// The chess envelope on the command stream.
structure ChessCommand {
    @required
    move: ChessMove
}

/// A move off turn, one not in ChessView.legalMoves, or one on a finished
/// game is refused in band (commandRejected). A move that arrives after
/// the mover's time ran out is not played: the game ends on time.
union ChessMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: ChessStartGame
    leaveGame: LeaveGame
    play: ChessPlay
    resign: ChessResign
    addBot: ChessAddBot
    watch: ChessWatch
    challenge: ChessStartGame
    history: ChessHistoryRequest
    review: ChessReviewRequest
    publish: ChessPublish
}

/// The room's finished games, newest first: answered with a history.
/// Any member, seated, watching or neither.
structure ChessHistoryRequest {}

/// One finished game from the room's history, answered with a review:
/// by its archiveId, or by its table and line on that table's score sheet
/// (ChessScoreLine's position, from 1), which names that table's newest
/// such game. Exactly one of the two.
structure ChessReviewRequest {
    archiveId: Long

    gameId: String

    @range(min: 1)
    ordinal: Integer
}

/// Publishes the room's chess games, or stops: any member may, and every
/// member hears published. A game that ends while its room is published
/// joins the public feed, GET /games/v2/chess.pgn, which names no room and
/// keeps it for 30 days, room or no room. Stopping keeps the next games
/// private; those already out stay out.
structure ChessPublish {
    @required
    published: Boolean
}

/// A challenge's terms, as `challenge` posted them with the defaults filled
/// in.
structure ChessTerms {
    @required
    setupId: String

    @required
    setupName: String

    @required
    initialSeconds: Integer

    @required
    incrementSeconds: Integer
}

/// Watches a chess table in the room, in any phase, from no seat (#1633):
/// the answer is a gameState, and from then on every gameState and
/// gameEnded the seats get. Refused while seated. Watching another table
/// switches to it; sitting down anywhere, leaveGame (acked with
/// gameLeft), leaving the room and a closed socket each end it. A table
/// erased before it started tells its watchers gameLeft.
structure ChessWatch {
    @required
    gameId: String
}

/// Seats Stockfish in the empty second seat of a table not yet started
/// (#1618), playing at `elo`. Only the table's one player may; the bot
/// leaves with them. Refused when the hub has no engine to ask.
structure ChessAddBot {
    @required
    @range(min: 1320, max: 3190)
    elo: Integer
}

/// Seats two and deals the variant's position, White's clock running.
/// As `challenge` (#1633), the same fields are the terms a waiting table's
/// one seat posts, before anyone joins; posting again replaces them. The
/// room's GameSummary.terms and every view show them, and the seat that
/// fills the table, a player's or a bot's, starts the game on them.
/// On a table whose game has ended, either seat starts the next one, sides
/// swapped. Absent fields take the defaults: the standard starting
/// position, three minutes and two seconds.
structure ChessStartGame {
    /// A server-owned setup identifier. Absent selects standard.
    @length(min: 1, max: 32)
    setupId: String

    /// Each side's time, 30 to 1800.
    @range(min: 30, max: 1800)
    initialSeconds: Integer

    /// Added to a side's clock once it moves, 0 to 30.
    @range(min: 0, max: 30)
    incrementSeconds: Integer
}

structure ChessPlay {
    /// The move in UCI: from and to squares, and the piece a promotion
    /// makes ("e2e4", "e7e8q"), exactly as ChessView.legalMoves spells it.
    @required
    @length(min: 4, max: 5)
    uci: String
}

/// Either seat, on turn or not: the other side wins.
structure ChessResign {}

structure ChessSetupOption {
    @required
    setupId: String

    @required
    name: String
}

list ChessSetupOptions {
    member: ChessSetupOption
}

/// The chess envelope on the event stream.
structure ChessEvent {
    @required
    update: ChessUpdate
}

union ChessUpdate {
    gameJoined: ChessGameJoined
    gameState: ChessGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    turnChanged: TurnChanged
    gameEnded: ChessGameEnded
    gameLeft: GameLeft
    history: ChessHistory
    review: ChessReview
    published: ChessPublished
}

/// The room's finished games, newest first, at most 100. The room's
/// history lives as long as the room does.
structure ChessHistory {
    /// Whether the room publishes its games now.
    @required
    published: Boolean

    @required
    games: ChessGameSummaries
}

list ChessGameSummaries {
    member: ChessGameSummary
}

/// One finished game, without its moves.
structure ChessGameSummary {
    /// The game's identity: what a review names it by, and its place in
    /// the public feed if it is there.
    @required
    archiveId: Long

    @required
    gameId: String

    /// The game's position on its table's scoreSheet, from 1.
    @required
    ordinal: Integer

    @required
    white: String

    @required
    black: String

    @required
    result: ChessResult

    @required
    setupId: String

    @required
    setupName: String

    /// Half-moves played.
    @required
    plies: Integer

    /// When it ended, epoch milliseconds.
    @required
    endedAtMs: Long

    /// Whether it ended while the room was published.
    @required
    published: Boolean
}

/// One finished game, move by move.
structure ChessReview {
    @required
    summary: ChessGameSummary

    /// Every move in UCI, as ChessView.moves.
    @required
    moves: ChessMoves

    /// The same moves in SAN.
    @required
    san: ChessMoves

    /// Every position as FEN, the start first: one more than the moves.
    @required
    fens: ChessFens

    /// The game as export-format PGN.
    @required
    pgn: String
}

list ChessFens {
    member: String
}

/// The room's games were published or withdrawn. `by` names the member
/// who did it; absent when another instance relays it.
structure ChessPublished {
    @required
    published: Boolean

    by: String
}

structure ChessGameJoined {
    @required
    view: ChessView
}

structure ChessGameState {
    @required
    view: ChessView
}

/// A game is over; the view before it carries the same result. The table
/// stays for the next game unless a leave ended it.
structure ChessGameEnded {
    @required
    result: ChessResult
}

/// The table as both seats and every watcher see it: chess hides nothing.
structure ChessView {
    @required
    gameId: String

    /// The server-owned setup catalog, available before and during play.
    @required
    availableSetups: ChessSetupOptions

    /// The setup selected when ChessStartGame.setupId is absent.
    @required
    defaultSetupId: String

    /// The posted challenge's terms; absent once started, or with none
    /// posted. A table its start failed to start keeps them, full and
    /// waiting, until a startGame or a leave.
    terms: ChessTerms

    /// waiting | playing | ended | closed: ended between games, closed
    /// once a seat left the table.
    @required
    phase: String

    /// The variant being played; absent while waiting.
    variant: String

    /// The selected server-owned setup and its display name; absent while waiting.
    setupId: String
    setupName: String

    /// Seat order; colors are absent while waiting.
    @required
    players: ChessPlayers

    /// The position now, as FEN; absent while waiting.
    fen: String

    /// Every move so far in UCI, from the position the game started at.
    @required
    moves: ChessMoves

    /// white | black; absent unless playing.
    sideToMove: String

    /// The seat on turn; absent unless playing.
    currentPlayerId: String

    /// The side to move is in check; after a mate, the mated side.
    @required
    inCheck: Boolean

    /// The side to move's legal moves in UCI, sorted: what ChessPlay
    /// accepts. Empty unless playing.
    @required
    legalMoves: ChessMoves

    /// Absent while waiting.
    clock: ChessClock

    /// Present once the game is over.
    result: ChessResult

    /// Every game this table finished, in order.
    @required
    scoreSheet: ChessScoreSheet
}

list ChessPlayers {
    member: ChessPlayer
}

structure ChessPlayer {
    @required
    playerId: String

    /// white | black; absent while waiting.
    color: String

    /// The seat is a bot's, its playerId naming the engine and strength
    /// ("stockfish@1500"). Absent for a person.
    bot: Boolean
}

list ChessMoves {
    member: String
}

/// Each side's time left as of the moment the view was built; the side
/// to move's is running, and a client counts it down from there. Frozen
/// once the game is over.
structure ChessClock {
    @required
    whiteMs: Long

    @required
    blackMs: Long

    @required
    initialMs: Long

    @required
    incrementMs: Long
}

structure ChessResult {
    /// The winning seat; absent for a draw. May name a seat that has
    /// since left.
    winner: String

    /// white | black; absent for a draw.
    winnerColor: String

    /// checkmate | stalemate | insufficientMaterial | fiftyMoves |
    /// repetition | resignation | timeout | abandoned
    @required
    ending: String
}

list ChessScoreSheet {
    member: ChessScoreLine
}

/// One finished game.
structure ChessScoreLine {
    /// The winning player; absent for a draw.
    winner: String

    /// As ChessResult.ending.
    @required
    ending: String
}
