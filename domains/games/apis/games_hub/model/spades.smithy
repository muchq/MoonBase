$version: "2.0"

namespace moonbase.spades

use moonbase.games#Card
use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#PlayerIds
use moonbase.games#TurnChanged

// Spades' vocabulary (#1658): two to four seats, in the order they joined,
// playing hands until a team wins the game, then game after game until a
// seat leaves. Partners seats four, partners across — seats 0 and 2 are
// team 0, seats 1 and 3 team 1; cutthroat seats two to four, every seat a
// team of its own. Two seats are dealt thirteen each, the rest unseen;
// three, seventeen each without the two of clubs; four, thirteen each. It
// rides the room's Play stream as one `spades` member per direction and
// reuses the shared lifecycle shapes but startGame, which names the mode.
// Shapes are prefixed `Spades`: codegen flattens every namespace into one.

/// The spades envelope on the command stream.
structure SpadesCommand {
    @required
    move: SpadesMove
}

/// A hand is bid round the table from the dealer's left, then played
/// out trick by trick; a play names its card (#1505), so one sent
/// against a view the table has moved past is refused in band.
union SpadesMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: SpadesStartGame
    leaveGame: LeaveGame
    bid: SpadesBid
    play: SpadesPlay
    deal: SpadesDeal
}

/// Deals the first hand once two or more seats are filled.
structure SpadesStartGame {
    /// partners | cutthroat; absent, partners at four seats and cutthroat
    /// at fewer. Partners needs four.
    @length(min: 1, max: 16)
    mode: String
}

/// The seat's bid: tricks it means to take, 0 (nil) to the view's
/// handSize.
structure SpadesBid {
    @required
    @range(min: 0, max: 17)
    tricks: Integer
}

/// A card to the trick: one of SpadesView.legal. The suit led must be
/// followed if the hand holds it; spades are not led until broken.
structure SpadesPlay {
    @required
    card: Card
}

/// Any seat deals the next hand once this one is scored; after a won
/// game, the first hand of a new game, the scores at nothing.
structure SpadesDeal {}

/// The spades envelope on the event stream.
structure SpadesEvent {
    @required
    update: SpadesUpdate
}

union SpadesUpdate {
    gameJoined: SpadesGameJoined
    gameState: SpadesGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    turnChanged: TurnChanged
    gameEnded: SpadesGameEnded
    gameLeft: GameLeft
}

structure SpadesGameJoined {
    @required
    view: SpadesView
}

structure SpadesGameState {
    @required
    view: SpadesView
}

/// A game is over; the view before it carries the same result.
structure SpadesGameEnded {
    @required
    result: SpadesResult
}

/// A game's end: won on points by the team the winners name, or
/// abandoned by a leave, with no winner.
structure SpadesResult {
    /// points | abandoned
    @required
    ending: String

    /// The winning team's seats; empty when abandoned.
    @required
    winners: PlayerIds

    /// Each team's score at the end, in the order of the view's teams.
    @required
    scores: SpadesCounts
}

/// One seat's redacted view of the table: its own hand faces, every
/// other hand as a count, every bid and the tricks each seat has taken,
/// the trick in play, and each team's score.
structure SpadesView {
    @required
    gameId: String

    /// waiting | bidding | playing | scored | won | closed: scored once a
    /// hand is played out and the next is not dealt; won once a hand has
    /// won the game; closed once a seat has left.
    @required
    phase: String

    /// partners | cutthroat; absent while waiting.
    mode: String

    /// Cards a seat is dealt, and the most it may bid; absent while
    /// waiting.
    handSize: Integer

    /// Seat order, which is turn order.
    @required
    players: SpadesPlayers

    /// Absent while waiting.
    dealer: String

    /// The seat to bid or play; absent when nobody is.
    currentPlayerId: String

    /// Hands dealt in this game, from 1; 0 while waiting.
    @required
    handNumber: Integer

    /// The trick in play, from its lead.
    @required
    trick: SpadesTrickPlays

    /// The trick last taken; absent before the first of a hand.
    lastTrick: SpadesTrick

    @required
    spadesBroken: Boolean

    /// The cards the viewer may play now; empty off turn.
    @required
    legal: SpadesCards

    /// In team order: partners' two, or cutthroat's one a seat in seat
    /// order; empty while waiting.
    @required
    teams: SpadesTeams

    /// This game's hands, in order.
    @required
    hands: SpadesHandLines

    /// The game's end, once won (phase won) or abandoned by the leave
    /// that closed the table (phase closed).
    result: SpadesResult

    /// The seat that left, once closed.
    left: String
}

list SpadesPlayers {
    member: SpadesPlayer
}

structure SpadesPlayer {
    @required
    playerId: String

    /// Its index in the view's teams; 0 while waiting.
    @required
    team: Integer

    @required
    handCount: Integer

    /// The viewer's own hand, by suit then rank; empty for everyone else.
    @required
    hand: SpadesCards

    /// Absent until the seat bids; 0 is nil.
    bid: Integer

    /// Tricks taken this hand.
    @required
    tricks: Integer
}

list SpadesCards {
    member: Card
}

list SpadesTrickPlays {
    member: SpadesTrickPlay
}

structure SpadesTrickPlay {
    @required
    playerId: String

    @required
    card: Card
}

structure SpadesTrick {
    @required
    plays: SpadesTrickPlays

    @required
    winner: String
}

list SpadesTeams {
    member: SpadesTeam
}

structure SpadesTeam {
    /// Its seats, in seat order: two partners, or one.
    @required
    players: PlayerIds

    @required
    score: Integer

    /// Bags carried, 0 to 9: the tenth costs 100.
    @required
    bags: Integer

    /// Games this table has seen the team win.
    @required
    gamesWon: Integer
}

list SpadesHandLines {
    member: SpadesHandLine
}

/// One hand: bids and tricks seat by seat, what each team scored on it
/// and its total after.
structure SpadesHandLine {
    @required
    bids: SpadesCounts

    @required
    tricks: SpadesCounts

    @required
    points: SpadesCounts

    @required
    scores: SpadesCounts
}

list SpadesCounts {
    member: Integer
}
