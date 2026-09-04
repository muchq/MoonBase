$version: "2.0"

namespace moonbase.castle

use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#StartGame
use moonbase.games#Card
use moonbase.games#CardIndexes
use moonbase.games#PlayerIds
use moonbase.games#TurnChanged

// Castle's vocabulary (#77): the shedding game also played as Palace, the
// second game on the room layer. It rides golf's Play stream as one
// `castle` member per direction — a room hosts tables of either game, so
// the stream is the room's, not golf's — and reuses the shared lifecycle
// shapes (create/join/start/leave and their announcements).

/// The castle envelope on the command stream.
structure CastleCommand {
    @required
    move: CastleMove
}

/// Setup: swap hand cards for face-up ones, then ready. Play: cards of
/// one rank from the hand, then the face-up row, then a blind face-down
/// card once the hand is gone; a player who cannot play picks the pile
/// up. The engine refuses anything else in-band (commandRejected).
union CastleMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: StartGame
    leaveGame: LeaveGame
    swapForSetup: SwapForSetup
    ready: Ready
    playFromHand: PlayFromHand
    playFaceUp: PlayFaceUp
    playFaceDown: PlayFaceDown
    pickUp: PickUp
}

/// Exchange one hand card for one face-up card before declaring ready.
structure SwapForSetup {
    @required
    handIndex: Integer

    @required
    faceUpIndex: Integer
}

/// Done arranging; play begins once every seat is ready.
structure Ready {}

/// Hand indexes, all of one rank.
structure PlayFromHand {
    @required
    indexes: CardIndexes
}

/// Face-up row indexes, all of one rank; only once the hand is empty.
structure PlayFaceUp {
    @required
    indexes: CardIndexes
}

/// One face-down card, played blind; only once the face-up row is gone.
/// An unplayable flip picks the pile up, card included.
structure PlayFaceDown {
    @required
    index: Integer
}

/// Take the pile into the hand, when nothing in the row in play can go
/// on it.
structure PickUp {}

/// The castle envelope on the event stream.
structure CastleEvent {
    @required
    update: CastleUpdate
}

union CastleUpdate {
    gameJoined: CastleGameJoined
    gameState: CastleGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    turnChanged: TurnChanged
    gameEnded: CastleGameEnded
    gameLeft: GameLeft
}

structure CastleGameJoined {
    @required
    view: CastleView
}

structure CastleGameState {
    @required
    view: CastleView
}

/// The finish order: the first seat out wins and ends the game, so it
/// holds one name. A two-seat game names the other seat as the loser; a
/// bigger table ends with a winner and no loser. An abandoned game has
/// neither: nobody went out, and the order is empty.
structure CastleGameEnded {
    @required
    finished: PlayerIds

    loser: String
}

/// One player's redacted view: own hand faces, everyone's face-up rows,
/// face-down rows as counts, the pile top and the draw-pile count, and
/// the pile's last play — the one way to see a burn, which leaves the
/// pile empty. Every hand is revealed once the game ends. An ended view
/// is always followed by gameEnded, which says who lost (or that nobody
/// did); the view alone cannot tell an abandoned table from a finished
/// one.
structure CastleView {
    @required
    gameId: String

    /// waiting | setup | playing | ended
    @required
    phase: String

    @required
    players: CastlePlayers

    currentPlayerId: String

    @required
    drawPileCount: Integer

    @required
    pileCount: Integer

    pileTop: Card

    /// First out first; complete only once the game ends.
    @required
    finished: PlayerIds

    /// The pile's most recent play, until a play or pick-up replaces it.
    /// Absent before the first play and after a pick-up.
    lastPlay: CastleLastPlay
}

structure CastleLastPlay {
    @required
    playerId: String

    @required
    cards: Cards

    /// The play burned the pile: a ten, or the four of a kind it completed.
    @required
    burned: Boolean
}

list CastlePlayers {
    member: CastlePlayer
}

structure CastlePlayer {
    @required
    playerId: String

    @required
    ready: Boolean

    @required
    handCount: Integer

    /// The viewer's own hand, in hand order; empty for everyone else
    /// until the game ends.
    @required
    hand: Cards

    @required
    faceUp: Cards

    @required
    faceDownCount: Integer

    /// Shed every card: finished, and out of the turn order.
    @required
    out: Boolean

    /// It is this seat's turn and its row in play holds a legal play on
    /// the pile as it stands; false off turn, for a blind row, and for
    /// every other seat. A seat on turn without one must pick up.
    @required
    canPlay: Boolean
}

list Cards {
    member: Card
}
