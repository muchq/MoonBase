$version: "2.0"

namespace moonbase.wordchain

use moonbase.games#CreateGame
use moonbase.games#GameCreated
use moonbase.games#GameLeft
use moonbase.games#GameStarted
use moonbase.games#JoinGame
use moonbase.games#LeaveGame
use moonbase.games#PlayerIds

// Wordchain's vocabulary: a word ladder race. Two to four seats climb the
// same puzzle, each on their own ladder, against one clock: from the start
// word to the target, one letter at a time, every rung a word. The fewest
// words wins, the earlier finish breaking a tie; a finish on par cannot be
// beaten and ends the race. A table races one puzzle after another until
// fewer than two seats remain. It rides the room's Play stream as one
// `wordchain` member per direction and reuses the shared lifecycle shapes
// but startGame, which names the race. Shapes are prefixed `Wordchain`:
// codegen flattens every namespace into one.

/// The wordchain envelope on the command stream.
structure WordchainCommand {
    @required
    move: WordchainMove
}

/// startGame opens the first race once two seats are filled, and each
/// next race once the last is over.
union WordchainMove {
    createGame: CreateGame
    joinGame: JoinGame
    startGame: WordchainStartGame
    leaveGame: LeaveGame
    play: WordchainPlay
    retract: WordchainRetract
}

/// The race to run; absent fields take 4 letters and 180 seconds.
structure WordchainStartGame {
    @range(min: 3, max: 5)
    letters: Integer

    @range(min: 60, max: 600)
    seconds: Integer
}

/// The next rung on the sender's ladder: a word one letter from their
/// last, in any case.
structure WordchainPlay {
    @required
    @length(min: 3, max: 5)
    word: String
}

/// Takes back the sender's last rung, until they finish.
structure WordchainRetract {}

/// The wordchain envelope on the event stream.
structure WordchainEvent {
    @required
    update: WordchainUpdate
}

union WordchainUpdate {
    gameJoined: WordchainGameJoined
    gameState: WordchainGameState
    gameCreated: GameCreated
    gameStarted: GameStarted
    gameEnded: WordchainGameEnded
    gameLeft: GameLeft
}

structure WordchainGameJoined {
    @required
    view: WordchainView
}

structure WordchainGameState {
    @required
    view: WordchainView
}

/// A race is over; the view after it carries the same result.
structure WordchainGameEnded {
    @required
    result: WordchainResult
}

/// A race's end.
structure WordchainResult {
    /// finished | timeout | leave: everyone seated finished or one did on
    /// par; the clock ran out; too few seats stayed.
    @required
    ending: String

    /// The finishers with the fewest words and, among them, the earliest
    /// finish: more than one when they share both; empty when nobody
    /// finished.
    @required
    winners: PlayerIds

    /// Finishers best first, then everyone else in the order they joined.
    @required
    standings: PlayerIds
}

/// One seat's view of the table. While a race runs it sees its own ladder
/// and only how far everyone else has climbed; once it is over, every
/// ladder.
structure WordchainView {
    @required
    gameId: String

    /// waiting | racing | over | closed: over between races, closed once
    /// too few seats remain.
    @required
    phase: String

    /// The race's puzzle, its clock in epoch milliseconds, and how many
    /// words a shortest ladder holds, both ends included; absent while
    /// waiting.
    start: String
    target: String
    par: Integer
    startedAtMs: Long
    deadlineMs: Long

    /// The word list the race is played against: enable.
    lexicon: String

    /// Every runner of the race (or every seat, while waiting), in the
    /// order they joined.
    @required
    runners: WordchainRunners

    /// The race's end, once it is over.
    result: WordchainResult

    /// Races this table has ended.
    @required
    racesPlayed: Integer
}

list WordchainRunners {
    member: WordchainRunner
}

structure WordchainRunner {
    @required
    playerId: String

    /// Rungs climbed so far, the target among them once finished.
    @required
    words: Integer

    /// When the ladder reached the target.
    finishedAtMs: Long

    /// Left the table; their ladder and any finish still count.
    @required
    left: Boolean

    /// The rungs after the start: the viewer's own always, everyone's once
    /// the race is over.
    ladder: WordchainWords

    /// Races this runner has won at the table.
    @required
    wins: Integer
}

list WordchainWords {
    member: String
}
