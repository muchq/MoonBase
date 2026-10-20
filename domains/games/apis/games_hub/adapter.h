#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/apis/games_hub/hub_metrics.h"
#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/round_robin.h"
#include "domains/games/libs/cards/card.h"
#include "moonbase/games/server.h"

namespace games_hub {

using StatsDeltas = std::vector<HubStore::StatsDelta>;

/// Events for their recipients, in the order each must observe them.
using Staged = std::vector<std::pair<std::string, moonbase::games::GameEvents>>;

/// What a table's views read besides its engine, as the hub holds it for
/// one call. A view is a pure function of this, the engine (null before
/// the table starts) and its viewer.
struct TableContext {
  const std::string& game_id;
  const std::vector<std::string>& roster;
  /// Members watching from no seat; only a kWatchable table has any.
  const std::set<std::string>& watchers;
  const std::optional<HostedTerms>& terms;
  const std::optional<EventTag>& event;
  /// Epoch milliseconds on the hub's clock.
  int64_t now_ms = 0;
};

/// What an engine move may draw on from the hub, as it is applied.
struct MoveContext {
  /// Epoch milliseconds on the hub's clock.
  int64_t now_ms = 0;
  /// A freshly shuffled deck from the hub's dealer.
  std::function<std::deque<cards::Card>()> deck;
  /// Whether `player_id` is away from the table's room: no member of it,
  /// or disconnected.
  std::function<bool(const std::string& player_id)> away;
};

/// What a table opens from at startGame.
struct OpenContext {
  const std::string& game_id;
  const std::vector<std::string>& roster;
  int64_t now_ms = 0;
  /// The round robin pairing the table plays, if its round robin is held.
  const Pairing* pairing = nullptr;
};

/// The shared machinery a game opts into, as Adapter::kCapabilities. A
/// capability's hooks are the adapter's to hold (adapters.h checks them).
enum Capability : unsigned {
  /// One view serves every seat, and members may watch from no seat.
  kWatchable = 1U << 0,
  /// A seated table plays game after game: startGame on one between games
  /// starts the next. Hooks: Next(engine, start, move context), and
  /// CurrentGameOver(engine), whether the game last started has ended.
  kNextGame = 1U << 1,
  /// A round robin's pairings play at the game's tables; implies
  /// kNextGame, a pairing being decided by its table's game ending.
  /// Hooks: Terms, what a round robin plays on (a HostedTerms
  /// alternative); RoundRobinStart(start, table terms, round robin row),
  /// the start a pairing's table plays on; RoundRobinWire(held), one in
  /// the game's wire, and RoundRobins, the wire's list of them, which the
  /// game's envelope carries; kRoundRobinBodyNamed,
  /// whether a stored round robin's body names its game (exactly one
  /// game's bodies name none, and a body naming none is that game's);
  /// RoundRobinBody(terms) and FromRoundRobinBody(body), the game's own
  /// fields in that body, and its terms read back from them, if they do;
  /// TableGame(engine), the pairing's game a tagged table's own row
  /// records, for a game whose pairings' games are not archived.
  kRoundRobins = 1U << 2,
  /// A start draws its opening from the hub's ChessOpener. Hooks:
  /// Prepare(start, opener).
  kOpenings = 1U << 3,
  /// The side to move plays on a clock the hub sweeps (implies
  /// kNextGame). Hooks: FlagDue(engine, now_ms), whether the side to move
  /// has run out; Flag(engine, now_ms), the game ended on time.
  kClocked = 1U << 4,
  /// The hub's engine for the game plays its bot seats: asked without the
  /// hub's lock, its answer played only on the position it was asked
  /// about. Hooks: BotAsk, what the engine is asked; BotTurnOf(engine,
  /// now_ms), a bot seat's move to make, if one has; BotMove(engine, seat,
  /// answer, context), the answer played from the seat.
  kEngineBots = 1U << 5,
  /// The game runs between moves, on the hub's ticks: pong's rallies.
  /// Hooks: Steers(move), whether a move steers the rally, and so rides
  /// the hub's realtime rate budget rather than its command budget.
  kRealtime = 1U << 6,
  /// Finished games are kept as the room's history (implies kNextGame).
  /// Hooks: Archived(engine), the finished game a committed row archives
  /// and its ordinal. Chess's alone, in a published room, reach the public
  /// feed, and the hub's indexer is asked after each one's players.
  kArchived = 1U << 7,
  /// A waiting table may hold posted terms its next seat starts on. The
  /// store checks the flag: a waiting row's state column is read back as
  /// those terms in place of an engine. Posting them is Decode's, as the
  /// PostTerms it returns. Hooks: Terms, as kRoundRobins'; TermsJson(terms)
  /// and TermsFromJson(json), the terms as the state column holds them
  /// and back, if they read; TermsLine(terms), how the lobby lists them.
  kChallenges = 1U << 8,
};

/// How a game ended: played out, or left with too few seats to go on.
enum class GameEnd { kCompleted, kAbandoned };

/// A round robin as the hub holds it, with the tables playing its
/// pairings now, by pairing index.
struct HeldRoundRobin {
  const HubStore::ChessEventRow& row;
  std::map<int, std::string> live;
};

/// A move union's case as the hub runs it, from an adapter's Decode. The
/// lifecycle and the engine moves are every game's; the rest are the
/// shared machinery a game's union reaches.
struct CreateTable {};
struct JoinTable {
  std::string game_id;
};
struct LeaveTable {};
struct WatchTable {
  std::string game_id;
};
/// A bot to the table's next seat; `engine` when the hub's chess engine
/// plays it.
struct AddBot {
  std::string bot_id;
  bool engine = false;
};
/// The terms a waiting table posts for its next seat to start on.
struct PostTerms {
  HostedTerms terms;
};
struct AskHistory {};
struct AskReview {
  moonbase::games::ChessReviewRequest review;
};
struct Publish {
  bool published = false;
};
struct CreateRoundRobin {
  std::vector<std::string> entrants;
  HostedTerms terms;
};
struct Withdraw {
  moonbase::games::ChessWithdraw withdraw;
};
struct Forfeit {
  moonbase::games::ChessForfeit forfeit;
};
struct ListRoundRobins {};
struct PlayRoundRobin {
  moonbase::games::ChessPlayRoundRobin play;
};
struct Steer {
  moonbase::games::PongPaddle paddle;
};

/// An engine move from the mover's seat, and what it announces beyond the
/// views.
template <typename Engine>
struct Transition {
  std::function<absl::StatusOr<Engine>(const Engine&, int seat, const MoveContext&)> next;
  /// turnChanged when the turn passes to someone else.
  bool announce_turn = true;
  /// To the seats ahead of the views.
  std::optional<moonbase::games::GameEvents> first = std::nullopt;
  /// When set, every seat gets the views only if this holds of the table
  /// before and after the move; otherwise the mover alone gets theirs.
  std::function<bool(const Engine& before, const Engine& after)> views_to_all = nullptr;
};

/// A startGame: the table's first game on `start`, or, with `next_game`,
/// a seated table's next (a round robin's table refusing, as it plays one).
template <typename Start>
struct StartGame {
  Start start;
  bool next_game = false;
};

template <typename Engine, typename Start>
using Action =
    std::variant<Refusal, CreateTable, JoinTable, LeaveTable, StartGame<Start>, Transition<Engine>,
                 WatchTable, AddBot, PostTerms, AskHistory, AskReview, Publish, CreateRoundRobin,
                 Withdraw, Forfeit, ListRoundRobins, PlayRoundRobin, Steer>;

/// A bot seat's move to make, as its game reads the table (kEngineBots).
template <typename Ask>
struct BotTurn {
  std::string bot_id;
  /// Which of the table's games: its count of finished ones. A table keeps
  /// its id, and can repeat a start, from one game to the next.
  std::size_t game_no = 0;
  /// Where the game stands; an answer is played only while it still does.
  std::string position;
  Ask ask;
};

/// The engine a game's bots ask, answering a move in the game's own words
/// or why not. Called without the hub's lock, from the bot thread.
template <typename Ask>
using BotEngine = std::function<absl::StatusOr<std::string>(const Ask&)>;

/// The round robin moves every round robin game's union names alike, as
/// the hub runs them; nothing for any other case. createRoundRobin is each
/// game's own, as its terms are.
template <typename Engine, typename Start, typename Move>
std::optional<Action<Engine, Start>> RoundRobinMove(const Move& move) {
  if (const auto* withdraw = move.as_withdraw_or_null()) return Withdraw{*withdraw};
  if (const auto* forfeit = move.as_forfeit_or_null()) return Forfeit{*forfeit};
  if (move.as_roundRobins_or_null() != nullptr) return ListRoundRobins{};
  if (const auto* play = move.as_playRoundRobin_or_null()) return PlayRoundRobin{*play};
  return std::nullopt;
}

/// A game's side of the hub, one specialization per engine in the
/// game's own <game>_adapter.h. adapters.h dispatches on HostedState and
/// GameKind, and holds every specialization to the hooks below, so an
/// engine without one does not compile. Each specialization holds:
///
///   Engine, Move, Update, View, Start
///                           the engine, its envelope's command and update
///                           unions, the view a seat sees, and what a
///                           startGame names
///   kKind, kName, kMaxSeats, kCapabilities
///                           the kind, the word it is written as (wire,
///                           store and event log), its seat cap and the
///                           shared machinery it opts into
///   kOpenStartsAGame        whether opening the table starts a recorded
///                           game (rummy's games are its deals instead)
///   kCommandSeries, kCommands, kEventSeries, kEvents
///                           the counter series its envelope counts on, and
///                           the union cases each is declared with
///   MoveOf(commands), UpdateOf(events)
///                           its envelope's union, if the stream's holds it
///   Wrap(update)            an update in its envelope
///   Phase(engine)           the status the room lists the table under
///   Turn(engine)            the occupant to move, or ""
///   GamesFinished(engine)   games a multi-game table (chess, pong, spades)
///                           has ended, 0 otherwise: the replay cursor of a
///                           table an instance takes on
///   ClosedDeltas(engine, roster)
///                           what a finish that closes the table credits
///   DealEndDeltas(before, after)
///                           what a transition that ends a game without
///                           closing the table credits, if it did
///   DealStarted(before, after)
///                           whether a transition began the table's next game
///   Ended(engine)           how the game a finished or closed table last
///                           played ended, if that ending is not yet recorded
///   WithoutSeat(engine, seat, now_ms)
///                           the engine once `seat` leaves
///   ViewOf(context, engine, viewer)
///                           what `viewer` sees of the table; a null engine
///                           is a table waiting to start
///   StateEvent(view), JoinedEvent(view)
///                           a view as gameState and as gameJoined
///   GamesEnded(engine, told)
///                           gameEnded for each game past the first `told`
///   ClosedEvent(engine, roster)
///                           what the seats hear after the final views of a
///                           closed table, if anything
///   Decode(move, player_id) the Action a move union's case is
///   Open(context, start, deck)
///                           the engine of a table starting on `start`, a
///                           shuffled deck dealt for it
///   FilledStart(terms, event)
///                           the start a table makes once its last seat
///                           fills, if it makes one
///   IsBot(player_id)        whether the id is a bot the game seats
///   Serialize(engine), Deserialize(stored)
///                           the engine as its games row's state column
///                           holds it, and back
///
/// and, optionally:
///
///   RecordedName(engine)    the word a game is recorded under, when it is
///                           not kName (rummy's deals, by variant)
template <typename Engine>
struct Adapter;

/// A clock as the lobby lists it: "3+2", whole minutes in minutes and
/// any other in seconds ("90s+0").
std::string ClockLine(int64_t initial_ms, int64_t increment_ms);

/// Everyone in `players` played, and `winner` won ("" for nobody).
StatsDeltas PlayedWon(const std::vector<std::string>& players, std::string_view winner);

/// The stale-view refusal for a move naming cards: the first named card the
/// hand does not hold, spelled the way the player sees it.
absl::Status HeldInHand(const std::vector<cards::Card>& hand,
                        const std::vector<cards::Card>& named);

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTER_H
