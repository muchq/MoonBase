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

// Rummy's vocabulary (#245): the third game on the room layer, played at
// a dealer's-choice table (#1609) — one deal after another, the dealer
// picking each deal's variant. It rides the room's Play stream as one
// `rummy` member per direction and reuses the shared lifecycle shapes, the
// way castle does; startGame seats the table, and the dealer's
// chooseVariant deals.
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
    chooseVariant: RummyChooseVariant
    pass: RummyPass
    drawStock: RummyDrawStock
    drawDiscard: RummyDrawDiscard
    meld: RummyMeld
    layOff: RummyLayOff
    discard: RummyDiscard
    knock: RummyKnock
}

/// Between deals, the dealer deals the next: its variant, one the table's
/// view offers (RummyChoosing.options). A dealer who is not connected lets
/// any seat deal.
structure RummyChooseVariant {
    /// 7-card | 10-card | gin
    @required
    variant: String
}

/// Gin: turn down the upcard (stage upcard). Passed by both, the opener
/// draws from the stock.
structure RummyPass {}

/// Take the top of the stock. An empty stock is refilled from the
/// discard pile, turned over under its top card.
structure RummyDrawStock {}

/// Take the top of the discard pile — or, but in gin, every card from the
/// top down to `card`. A card taken alone may not be discarded again this
/// turn, unless it is the last in the hand. Taking more than one binds the
/// seat to play `card` (meld it or lay it off) before it discards, so it
/// may only be a card the view offers in discardTakeable.
structure RummyDrawDiscard {
    /// The deepest card to take; absent, the top.
    card: Card
}

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

/// Gin: end the deal, throwing `card`, with 10 or less deadwood left in
/// the hand's best arrangement. The hub arranges both hands and lays the
/// defender's cards off; the result rides lastDeal.gin.
structure RummyKnock {
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

/// The table broke up — below two seats. Each deal's result was the view's
/// lastDeal as it ended; this is the evening's: the hands each seat won,
/// every seat still at the table in seat order.
structure RummyGameEnded {
    @required
    standings: RummyStandings

    @required
    dealsPlayed: Integer
}

list RummyStandings {
    member: RummyStanding
}

structure RummyStanding {
    @required
    playerId: String

    @required
    handsWon: Integer

    /// The seat's running score: what its won deals scored, summed.
    @required
    points: Integer
}

/// The table's score sheet: a line a deal played to its end, in order.
list RummyScoreSheet {
    member: RummyScoreLine
}

structure RummyScoreLine {
    @required
    variant: String

    /// Who won the deal; absent for a gin draw. May name a seat that has
    /// since left.
    winner: String

    @required
    points: Integer
}

/// A deal's result: the seat that scored and what (every other seat's
/// cards left in hand, or gin's reckoning), and each seat's deadwood in
/// seat order. No winner for a deal broken up by a leave or drawn.
structure RummyDealResult {
    @required
    variant: String

    winner: String

    @required
    points: Integer

    @required
    scores: RummyScores

    /// A gin deal's end, as the hub arranged it.
    gin: RummyGinResult
}

structure RummyGinResult {
    /// knock | gin | undercut | draw
    @required
    ending: String

    /// Absent for a draw.
    knocker: String

    /// Each seat's hand as melds and deadwood, in seat order; the
    /// defender's without what it laid off.
    @required
    hands: RummyArrangedHands

    /// The defender's cards laid off onto the knocker's melds.
    @required
    laidOff: RummyCards
}

list RummyArrangedHands {
    member: RummyArrangedHand
}

structure RummyArrangedHand {
    @required
    playerId: String

    @required
    melds: RummyCardGroups

    @required
    deadwood: RummyCards
}

list RummyCardGroups {
    member: RummyCards
}

/// Between deals: who deals next and what they may deal. A dealer the
/// room shows as not connected (RoomState's PlayerInfo.connected) lets any
/// seat deal; the room, not this view, is what says so as it changes.
structure RummyChoosing {
    @required
    dealer: String

    /// The variants that fit the seats, in offer order.
    @required
    options: RummyVariants
}

list RummyVariants {
    member: String
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

/// One player's redacted view of the table and its deal: own hand faces,
/// every other hand as a count, the melds on the table, the discard pile's
/// top, and the stock as a count. Every hand of a deal is revealed once
/// the deal ends — between deals, the last deal is what the table shows.
/// An ended view is always followed by gameEnded.
structure RummyView {
    @required
    gameId: String

    /// waiting | choosing | playing | ended
    @required
    phase: String

    /// The deal in play's variant, or the last one's; absent before the
    /// first deal.
    variant: String

    /// Deals dealt so far.
    @required
    dealNumber: Integer

    /// Hands won, seat by seat.
    @required
    standings: RummyStandings

    /// Present between deals.
    choosing: RummyChoosing

    /// The last deal's result, between deals and once the table ends.
    lastDeal: RummyDealResult

    @required
    players: RummyPlayers

    currentPlayerId: String

    /// Where the seat on turn is: upcard (gin: take the upcard or pass) |
    /// draw (about to draw) | play (drawn; melds, lay-offs, then a
    /// discard — or in gin, a discard or a knock). Absent when nobody is on
    /// turn.
    stage: String

    @required
    stockCount: Integer

    /// Whether drawStock would take a card now: the stock holds one, or
    /// the discard pile has cards under its top to turn over.
    @required
    canDrawStock: Boolean

    /// Whether drawDiscard would take the discard pile's top now: not
    /// after gin's upcard was passed by both.
    @required
    canDrawDiscard: Boolean

    @required
    discardCount: Integer

    /// The discard pile's top; absent while the pile is empty.
    discardTop: Card

    /// The whole discard pile, bottom to top: public, as it lies face up.
    @required
    discardPile: RummyCards

    /// The cards the viewer may take the discard pile down to now (drawDiscard's
    /// card), bottom to top: the top, and each deeper card it could then
    /// play. Empty but on the viewer's own draw, and in gin.
    @required
    discardTakeable: RummyCards

    /// The deepest card the seat on turn took the discard pile down to,
    /// while it is still in hand: it must be played before the turn ends.
    mustPlay: Card

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

    /// Every deal played to its end, in order.
    @required
    scoreSheet: RummyScoreSheet
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

    /// drawStock | drawDiscard | meld | layOff | discard | pass | knock
    @required
    move: String

    /// What went on or came off the table: nothing for a stock draw
    /// (nobody else sees it) or a pass, the cards taken, the meld as laid,
    /// the card laid off, the card discarded or knocked on.
    @required
    cards: RummyCards

    /// The meld a meld or lay-off made or grew; absent otherwise.
    meldIndex: Integer
}

list RummyCards {
    member: Card
}
