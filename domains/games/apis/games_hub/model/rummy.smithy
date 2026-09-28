$version: "2.0"

namespace moonbase.rummy

use moonbase.games#Card
use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#StartGame
use moonbase.games#TurnChanged

// Rummy's vocabulary (#245): the basic game, the third on the room
// layer. It rides the room's Play stream as one `rummy` member per
// direction and reuses the shared lifecycle shapes, the way castle does.
// Shape names carry the game's name: codegen flattens every namespace
// into one.

/// The rummy envelope on the command stream.
structure RummyCommand {
    @required
    move: RummyMove
}

/// A turn is a draw (stock or discard), then any melds and lay-offs, then
/// a discard; a seat that empties its hand wins. Moves name the cards
/// they mean (#1505): a card the hand does not hold is refused in band,
/// so a move sent against a view the table has moved past costs a
/// refusal rather than playing a different card.
union RummyMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: StartGame
    leaveGame: LeaveGame
    drawStock: RummyDrawStock
    drawDiscard: RummyDrawDiscard
    meld: RummyMeld
    layOff: RummyLayOff
    discard: RummyDiscard
}

/// Take the top of the stock. An empty stock is refilled from the
/// discard pile, turned over under its top card.
structure RummyDrawStock {}

/// Take the top of the discard pile. That card may not be discarded again
/// this turn, unless it is the last card in the hand.
structure RummyDrawDiscard {}

/// Lay down a set (three or four of a rank) or a run (three or more of a
/// suit in sequence, the ace low or high but not both).
structure RummyMeld {
    @required
    cards: RummyCards
}

/// Add one card to a meld on the table, anyone's, naming it by its place
/// in RummyView.melds. Melds only ever grow and are never reordered, so
/// the place is stable for the whole game.
structure RummyLayOff {
    @required
    card: Card

    @required
    meldIndex: Integer
}

/// End the turn with one card on the discard pile.
structure RummyDiscard {
    @required
    card: Card
}

/// The rummy envelope on the event stream.
structure RummyEvent {
    @required
    update: RummyUpdate
}

union RummyUpdate {
    gameJoined: RummyGameJoined
    gameState: RummyGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    turnChanged: TurnChanged
    gameEnded: RummyGameEnded
    gameLeft: GameLeft
}

structure RummyGameJoined {
    @required
    view: RummyView
}

structure RummyGameState {
    @required
    view: RummyView
}

/// How the game ended: the seat that went out and what it scored (every
/// other seat's cards left in hand), or neither for an abandoned table.
/// scores holds every seat at the table at the end, in seat order.
structure RummyGameEnded {
    winner: String

    @required
    points: Integer

    @required
    scores: RummyScores
}

list RummyScores {
    member: RummyScore
}

/// What a seat still held at the end: an ace one, two to ten their pips,
/// a face card ten.
structure RummyScore {
    @required
    playerId: String

    @required
    deadwood: Integer
}

/// One player's redacted view: own hand faces, every other hand as a
/// count, the melds on the table, the discard pile's top, and the stock
/// as a count. Every hand is revealed once the game ends. An ended view
/// is always followed by gameEnded.
structure RummyView {
    @required
    gameId: String

    /// waiting | playing | ended
    @required
    phase: String

    @required
    players: RummyPlayers

    currentPlayerId: String

    /// Where the seat on turn is: draw (about to draw) | play (drawn;
    /// melds, lay-offs, then a discard). Absent when nobody is on turn.
    stage: String

    @required
    stockCount: Integer

    /// Whether drawStock would take a card now: the stock holds one, or
    /// the discard pile has cards under its top to turn over.
    @required
    canDrawStock: Boolean

    @required
    discardCount: Integer

    /// The discard pile's top; absent while the pile is empty.
    discardTop: Card

    /// The card the seat on turn took from the discard pile this turn,
    /// which it may not throw back unless it is its last. Public: everyone
    /// saw it taken.
    takenDiscard: Card

    /// The melds on the table in the order they were laid; a lay-off names
    /// one by its place here.
    @required
    melds: RummyTableMelds

    /// The table's most recent move, until the next replaces it. Absent
    /// before the first.
    lastMove: RummyLastMove
}

list RummyPlayers {
    member: RummyPlayer
}

structure RummyPlayer {
    @required
    playerId: String

    @required
    handCount: Integer

    /// The viewer's own hand, in the order its cards arrived (a drawn
    /// card last); empty for everyone else until the game ends.
    @required
    hand: RummyCards
}

list RummyTableMelds {
    member: RummyTableMeld
}

structure RummyTableMeld {
    /// Who laid it down. May name a seat that has since left.
    @required
    owner: String

    /// Low to high for a run, by suit for a set.
    @required
    cards: RummyCards
}

/// A move as the table saw it.
structure RummyLastMove {
    /// Who moved. May name a seat that has since left the game.
    @required
    playerId: String

    /// drawStock | drawDiscard | meld | layOff | discard
    @required
    move: String

    /// What went on or came off the table: nothing for a stock draw
    /// (nobody else sees it), the card taken, the meld as laid, the card
    /// laid off, the card discarded.
    @required
    cards: RummyCards

    /// The meld a meld or lay-off made or grew; absent otherwise.
    meldIndex: Integer
}

list RummyCards {
    member: Card
}
