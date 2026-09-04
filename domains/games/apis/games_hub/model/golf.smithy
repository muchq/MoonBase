$version: "2.0"

namespace moonbase.golf

use moonbase.castle#CastleCommand
use moonbase.castle#CastleEvent
use moonbase.games#Card
use moonbase.games#CardIndexes
use moonbase.games#Chat
use moonbase.games#ChatHistory
use moonbase.games#ChatMessage
use moonbase.games#CommandRejected
use moonbase.games#CreateGame
use moonbase.games#CreateRoom
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#GetRoomState
use moonbase.games#JoinGame
use moonbase.games#JoinRoom
use moonbase.games#LeaveGame
use moonbase.games#LeaveRoom
use moonbase.games#PlayerIds
use moonbase.games#RoomLeft
use moonbase.games#RoomState
use moonbase.games#SeatConflict
use moonbase.games#SessionReady
use moonbase.games#StartGame
use moonbase.games#TurnChanged
use moonbase.games#Unauthenticated

// Golf's vocabulary (#79): the Play stream and the moves and updates nested
// under one `golf` member per direction, so the room layer never changes
// shape when a game joins the hub. Castle (#77) is the second such member:
// the stream is the room's, and a room hosts tables of either game.

/// The one WebSocket session per player: commands up, events down. The
/// ticket rides the upgrade GET as a query member (browsers cannot set
/// upgrade headers); the gate checks it pre-101 and the handler spends it
/// (single use). Invalid moves never end the stream — they come back as
/// commandRejected events; the modeled errors below are terminal.
@http(method: "POST", uri: "/games/v2/golf/play")
operation Play {
    input := {
        @required
        @httpQuery("ticket")
        ticket: String

        @httpPayload
        commands: GolfCommands
    }
    output := {
        @httpPayload
        events: GolfEvents
    }
    errors: [Unauthenticated, SeatConflict]
}

@streaming
union GolfCommands {
    createRoom: CreateRoom
    joinRoom: JoinRoom
    leaveRoom: LeaveRoom
    getRoomState: GetRoomState
    chat: Chat
    golf: GolfCommand
    castle: CastleCommand
}

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

@streaming
union GolfEvents {
    sessionReady: SessionReady
    roomState: RoomState
    roomLeft: RoomLeft
    roomChat: ChatMessage
    roomChatHistory: ChatHistory
    commandRejected: CommandRejected
    golf: GolfEvent
    castle: CastleEvent
}

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
