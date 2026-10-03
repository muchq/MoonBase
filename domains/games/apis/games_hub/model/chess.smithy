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
// Starting positions are selected from the hub's server-owned practice
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
/// On a table whose game has ended, either seat starts the next one, sides
/// swapped. Absent fields take the default, three minutes and two seconds.
structure ChessStartGame {
    /// A server-owned practice setup identifier. Absent selects random-kpk.
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

/// The table as both seats see it: chess hides nothing.
structure ChessView {
    @required
    gameId: String

    /// The server-owned setup catalog, available before and during play.
    @required
    availableSetups: ChessSetupOptions

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
