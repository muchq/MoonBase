$version: "2.0"

namespace moonbase.golf

use moonbase.games#Card
use moonbase.games#CardIndexes
use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#PlayerIds
use moonbase.games#StartGame
use moonbase.games#TurnChanged

// Golf's vocabulary (#79): the moves and updates nested under one `golf`
// member per direction of the room's Play stream (games.smithy), so the
// room layer never changes shape when a game joins the hub. Castle (#77)
// and the lobby (#1490) are the other such members.

/// The game-specific envelope: exactly one move.
structure GolfCommand {
    @required
    move: GolfMove
}

union GolfMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: StartGame
    leaveGame: LeaveGame
    peekCard: PeekCard
    drawCard: DrawCard
    takeFromDiscard: TakeFromDiscard
    swapCard: SwapCard
    discardDrawn: DiscardDrawn
    knock: Knock
    hideCards: HideCards
}

/// Reveal one of your own four cards to yourself; two peeks per player,
/// then the hub flips the game to its reveal countdown.
structure PeekCard {
    @required
    cardIndex: Integer
}

/// Look at the top of the draw pile; commits you to swapCard or
/// discardDrawn this turn.
structure DrawCard {}

/// Take the (public) discard top straight into a slot — one step, since
/// no information is revealed by holding it first.
structure TakeFromDiscard {
    @required
    cardIndex: Integer
}

/// Swap the drawn card into a slot; the old card goes to the discard.
structure SwapCard {
    @required
    cardIndex: Integer
}

/// Reject the drawn card onto the discard pile.
structure DiscardDrawn {}

structure Knock {}

/// Ends the post-peek reveal countdown for the whole game.
structure HideCards {}


/// The game-specific envelope: exactly one update.
structure GolfEvent {
    @required
    update: GolfUpdate
}

union GolfUpdate {
    gameJoined: GameJoined
    gameState: GameStateUpdate
    gameCreated: GameCreated
    gameStarted: GameStarted
    turnChanged: TurnChanged
    playerKnocked: PlayerKnocked
    gameEnded: GameEnded
    gameLeft: GameLeft
}

structure GameJoined {
    @required
    view: GameView
}

structure GameStateUpdate {
    @required
    view: GameView
}

structure PlayerKnocked {
    @required
    playerId: String
}

/// winners is the typed list (ties are shared wins); winner is the joined
/// display string ("a & b") for anything that only shows one line.
structure GameEnded {
    @required
    winner: String

    @required
    winners: PlayerIds

    @required
    finalScores: FinalScores
}


list FinalScores {
    member: FinalScore
}

structure FinalScore {
    @required
    playerId: String

    @required
    score: Integer
}

/// One player's redacted view of a game. Own card faces appear only at
/// the revealed indexes (and everything at game end); other hands are
/// always nulls; the drawn card rides only to its holder. The server
/// never sends a fact the viewer is not entitled to — tighter than v1,
/// which shipped the whole hand to its owner during peek windows.
structure GameView {
    @required
    gameId: String

    /// waiting | playing | peeking | knocked | ended
    @required
    phase: String

    @required
    players: GamePlayers

    currentPlayerId: String

    @required
    drawPileCount: Integer

    @required
    discardCount: Integer

    discardTop: Card

    drawnCard: Card

    knockedPlayerId: String

    @required
    allPlayersPeeked: Boolean
}

list GamePlayers {
    member: GamePlayer
}

structure GamePlayer {
    @required
    playerId: String

    /// Always 4 entries; a slot without a card is face down for this
    /// viewer.
    @required
    cards: CardSlots

    /// The viewer's own revealed indexes; empty for everyone else.
    @required
    revealedIndexes: CardIndexes

    @required
    hasPeeked: Boolean

    /// Absent until the game ends.
    score: Integer
}

list CardSlots {
    member: CardSlot
}

structure CardSlot {
    card: Card
}
