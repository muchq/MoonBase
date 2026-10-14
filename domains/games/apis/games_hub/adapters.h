#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H

#include <array>
#include <concepts>
#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/apis/games_hub/castle_adapter.h"
#include "domains/games/apis/games_hub/chess_adapter.h"
#include "domains/games/apis/games_hub/golf_adapter.h"
#include "domains/games/apis/games_hub/hub_metrics.h"
#include "domains/games/apis/games_hub/pong_adapter.h"
#include "domains/games/apis/games_hub/rummy_adapter.h"
#include "domains/games/apis/games_hub/spades_adapter.h"

namespace games_hub {

template <std::size_t I>
using EngineAt = std::variant_alternative_t<I, HostedState>;

inline constexpr std::size_t kGames = std::variant_size_v<HostedState>;

/// The hooks every adapter holds (adapter.h), so a game missing one fails
/// here rather than at whichever hub path first reaches for it.
template <typename A>
concept GameAdapter = requires(
    const typename A::Engine& engine, const typename A::View& view, const TableContext& context,
    const std::string& id, const std::vector<std::string>& roster,
    const moonbase::games::GameCommands& commands, const moonbase::games::GameEvents& events) {
  { A::kKind } -> std::convertible_to<GameKind>;
  { A::kName } -> std::convertible_to<std::string_view>;
  { A::kMaxSeats } -> std::convertible_to<std::size_t>;
  { A::kCapabilities } -> std::convertible_to<unsigned>;
  { A::kCommandSeries } -> std::convertible_to<std::string_view>;
  { A::kEventSeries } -> std::convertible_to<std::string_view>;
  { *A::kCommands.begin() } -> std::convertible_to<std::string_view>;
  { *A::kEvents.begin() } -> std::convertible_to<std::string_view>;
  { A::MoveOf(commands) } -> std::same_as<const typename A::Move*>;
  { A::UpdateOf(events) } -> std::same_as<const typename A::Update*>;
  { A::Wrap(typename A::Update{}) } -> std::same_as<moonbase::games::GameEvents>;
  { A::Phase(engine) } -> std::same_as<std::string>;
  { A::Turn(engine) } -> std::same_as<std::string>;
  { A::GamesFinished(engine) } -> std::same_as<std::size_t>;
  { A::ClosedDeltas(engine, roster) } -> std::same_as<StatsDeltas>;
  { A::DealEndDeltas(engine, engine) } -> std::same_as<std::optional<StatsDeltas>>;
  { A::DealStarted(engine, engine) } -> std::same_as<bool>;
  { A::Ended(engine) } -> std::same_as<std::optional<GameEnd>>;
  { A::WithoutSeat(engine, 0, int64_t{0}) } -> std::same_as<absl::StatusOr<typename A::Engine>>;
  { A::ViewOf(context, &engine, id) } -> std::same_as<typename A::View>;
  { A::StateEvent(view) } -> std::same_as<moonbase::games::GameEvents>;
  { A::JoinedEvent(view) } -> std::same_as<moonbase::games::GameEvents>;
  {
    A::GamesEnded(engine, std::size_t{0})
  } -> std::same_as<std::vector<moonbase::games::GameEvents>>;
  { A::ClosedEvent(engine, roster) } -> std::same_as<std::optional<moonbase::games::GameEvents>>;
  { A::kOpenStartsAGame } -> std::convertible_to<bool>;
  {
    A::Decode(std::declval<const typename A::Move&>(), id)
  } -> std::same_as<Action<typename A::Engine, typename A::Start>>;
  {
    A::Open(std::declval<const OpenContext&>(), std::declval<const typename A::Start&>(),
            std::deque<cards::Card>{})
  } -> std::same_as<absl::StatusOr<typename A::Engine>>;
  {
    A::FilledStart(std::declval<const std::optional<ChessTerms>&>(),
                   std::declval<const std::optional<EventTag>&>())
  } -> std::same_as<std::optional<StartGame<typename A::Start>>>;
  { A::IsBot(std::string_view{}) } -> std::same_as<bool>;
  { A::Serialize(engine) } -> std::same_as<std::string>;
  { A::Deserialize(id) } -> std::same_as<absl::StatusOr<typename A::Engine>>;
};

/// Each capability's hooks (adapter.h), held by every adapter that
/// declares it.
template <typename A>
concept NextGameHooks = requires(const typename A::Engine& engine, const typename A::Start& start) {
  {
    A::Next(engine, start, std::declval<const MoveContext&>())
  } -> std::same_as<absl::StatusOr<typename A::Engine>>;
  { A::CurrentGameOver(engine) } -> std::same_as<bool>;
};

template <typename A>
concept RoundRobinHooks =
    NextGameHooks<A> && requires(typename A::Start& start, const HubStore::ChessEventRow& row,
                                 const HeldRoundRobin& held) {
      A::RoundRobinStart(start, std::declval<const std::optional<ChessTerms>&>(), &row);
      { A::RoundRobinTerms(row) } -> std::same_as<std::optional<ChessTerms>>;
      { A::RoundRobinEvent(held) } -> std::same_as<moonbase::games::GameEvents>;
      {
        A::RoundRobinsEvent(std::vector<HeldRoundRobin>{})
      } -> std::same_as<moonbase::games::GameEvents>;
      { A::kRoundRobinBodyNamed } -> std::convertible_to<bool>;
      { A::RoundRobinBody(row) } -> std::same_as<std::vector<std::pair<std::string, std::string>>>;
      {
        A::FromRoundRobinBody(std::declval<const std::map<std::string, std::string>&>(),
                              std::declval<HubStore::ChessEventRow&>())
      } -> std::same_as<bool>;
    };

template <typename A>
concept OpeningHooks = requires(typename A::Start& start) {
  { A::Prepare(start, std::declval<const ChessOpener&>()) } -> std::same_as<absl::Status>;
};

template <typename A>
concept ClockHooks = NextGameHooks<A> && requires(const typename A::Engine& engine) {
  { A::FlagDue(engine, int64_t{0}) } -> std::same_as<bool>;
  { A::Flag(engine, int64_t{0}) } -> std::same_as<absl::StatusOr<typename A::Engine>>;
};

template <typename A, Capability C>
constexpr bool kHas = (A::kCapabilities & C) != 0;

template <typename A>
concept CapableAdapter =
    (!kHas<A, kNextGame> || NextGameHooks<A>) && (!kHas<A, kRoundRobins> || RoundRobinHooks<A>) &&
    (!kHas<A, kOpenings> || OpeningHooks<A>) && (!kHas<A, kClocked> || ClockHooks<A>) &&
    (!kHas<A, kPublished> || NextGameHooks<A>);

/// Each HostedState alternative has a full adapter that names the GameKind
/// at its index, so a kind and its engine never disagree.
template <std::size_t... I>
constexpr bool KindsLineUp(std::index_sequence<I...>) {
  return ((GameAdapter<Adapter<EngineAt<I>>> && CapableAdapter<Adapter<EngineAt<I>>> &&
           std::is_same_v<typename Adapter<EngineAt<I>>::Engine, EngineAt<I>> &&
           Adapter<EngineAt<I>>::kKind == static_cast<GameKind>(I)) &&
          ...);
}
static_assert(KindsLineUp(std::make_index_sequence<kGames>{}));

/// Each kind's word, by kind: its adapter's kName.
inline constexpr std::array<std::string_view, kGames> kKindNames =
    []<std::size_t... I>(std::index_sequence<I...>) {
      return std::array<std::string_view, kGames>{Adapter<EngineAt<I>>::kName...};
    }(std::make_index_sequence<kGames>{});

constexpr bool NamesAreDistinct() {
  for (std::size_t i = 0; i < kGames; ++i) {
    if (kKindNames[i].empty()) return false;
    for (std::size_t j = 0; j < i; ++j) {
      if (kKindNames[i] == kKindNames[j]) return false;
    }
  }
  return true;
}
static_assert(NamesAreDistinct(), "every game's kName is its own, and not empty");

/// The word a kind is written as: GameSummary.game on the wire, the games
/// row's game column, and the word its games are recorded under.
inline std::string_view GameKindName(GameKind kind) {
  return kKindNames[static_cast<std::size_t>(kind)];
}

inline std::optional<GameKind> ParseGameKind(std::string_view name) {
  for (std::size_t i = 0; i < kGames; ++i) {
    if (kKindNames[i] == name) return static_cast<GameKind>(i);
  }
  return std::nullopt;
}

/// The most seats any game's table holds. Stats and deja bound a table's
/// players by it, and otel_contract reads this literal to hold them to it.
inline constexpr std::size_t kTableSeatCap = 4;

template <std::size_t... I>
constexpr std::size_t MostSeats(std::index_sequence<I...>) {
  std::size_t most = 0;
  ((most = Adapter<EngineAt<I>>::kMaxSeats > most ? Adapter<EngineAt<I>>::kMaxSeats : most), ...);
  return most;
}
static_assert(MostSeats(std::make_index_sequence<kGames>{}) == kTableSeatCap,
              "a game's seat cap moved; raise kTableSeatCap and the readers bound by it");

/// `f(Adapter<E>{}, engine)` for the engine `state` holds.
template <typename F>
decltype(auto) VisitAdapter(const HostedState& state, F&& f) {
  return std::visit(
      [&](const auto& engine) -> decltype(auto) {
        return f(Adapter<std::decay_t<decltype(engine)>>{}, engine);
      },
      state);
}

/// The word a game is recorded under (#1571): its adapter's
/// RecordedName(engine) where it has one, as rummy's does for a table
/// whose deals are each recorded under its variant's word, and its kind's
/// otherwise.
inline std::string_view VariantWordOf(const HostedState& state) {
  return VisitAdapter(state, [](auto adapter, const auto& engine) -> std::string_view {
    using A = decltype(adapter);
    if constexpr (requires { A::RecordedName(engine); }) {
      return A::RecordedName(engine);
    } else {
      return A::kName;
    }
  });
}

/// `f(Adapter<E>{})` for the engine a `kind` table plays.
template <typename F, std::size_t I = 0>
decltype(auto) ForKind(GameKind kind, F&& f) {
  if constexpr (I + 1 < kGames) {
    if (static_cast<std::size_t>(kind) != I) return ForKind<F, I + 1>(kind, std::forward<F>(f));
  }
  return f(Adapter<EngineAt<I>>{});
}

/// `f(Adapter<E>{}, before, after)` when both hold engine E; `otherwise`
/// when they differ.
template <typename F, typename R>
R VisitPair(const HostedState& before, const HostedState& after, F&& f, R otherwise) {
  return std::visit(
      [&](const auto& was) -> R {
        using E = std::decay_t<decltype(was)>;
        const auto* now = std::get_if<E>(&after);
        if (now == nullptr) return otherwise;
        return f(Adapter<E>{}, was, *now);
      },
      before);
}

/// Whether any adapter's `f(Adapter<E>{})` holds, asking each in turn
/// until one does.
template <typename F>
bool AnyAdapter(F&& f) {
  return [&]<std::size_t... I>(std::index_sequence<I...>) {
    return (f(Adapter<EngineAt<I>>{}) || ...);
  }(std::make_index_sequence<kGames>{});
}

/// `f(Adapter<E>{}, engine)` for a `kind` table: its engine if `state`
/// holds that game's, null for a table waiting to start.
template <typename F>
decltype(auto) VisitTable(GameKind kind, const std::optional<HostedState>& state, F&& f) {
  return ForKind(kind, [&](auto adapter) -> decltype(auto) {
    using E = typename decltype(adapter)::Engine;
    const E* engine = state.has_value() ? std::get_if<E>(&*state) : nullptr;
    return f(adapter, engine);
  });
}

/// The index of the one game whose adapter opts into `C`, kGames unless
/// exactly one does.
template <Capability C>
constexpr std::size_t OnlyGameWith() {
  return []<std::size_t... I>(std::index_sequence<I...>) {
    std::size_t found = kGames;
    std::size_t count = 0;
    ((kHas<Adapter<EngineAt<I>>, C> ? (found = I, ++count) : count), ...);
    return count == 1 ? found : kGames;
  }(std::make_index_sequence<kGames>{});
}

/// The engine of the one game whose adapter opts into `C`. The machinery
/// that is one game's alone — chess's clocks, engine bots and public
/// feed, pong's rallies — is typed on this, and reaches a table through
/// CapableEngine, never through its kind; a second game opting in fails
/// here until that machinery serves both.
template <Capability C>
struct OnlyEngineWith {
  static_assert(OnlyGameWith<C>() < kGames, "exactly one game opts into this capability");
  using type = EngineAt<OnlyGameWith<C>()>;
};
template <Capability C>
using EngineWith = typename OnlyEngineWith<C>::type;

/// The engine a started table plays, if its game is the one opting into
/// `C`; null otherwise, and before the table starts.
template <Capability C>
const EngineWith<C>* CapableEngine(const std::optional<HostedState>& state) {
  return state.has_value() ? std::get_if<EngineWith<C>>(&*state) : nullptr;
}

/// Whether a started `kind` table whose game opts into `C` has ended the
/// game it last started; false for any other table.
template <Capability C>
bool GameOverWith(GameKind kind, const std::optional<HostedState>& state) {
  return VisitTable(kind, state, [](auto adapter, const auto* engine) {
    using A = decltype(adapter);
    if constexpr (kHas<A, C> && NextGameHooks<A>) {
      return engine != nullptr && A::CurrentGameOver(*engine);
    } else {
      return false;
    }
  });
}

/// A `kind` round robin in its game's envelope, one or the room's all;
/// nothing for a game without round robins.
std::optional<moonbase::games::GameEvents> RoundRobinEventOf(GameKind kind,
                                                             const HeldRoundRobin& held);
std::optional<moonbase::games::GameEvents> RoundRobinsEventOf(
    GameKind kind, const std::vector<HeldRoundRobin>& held);
/// The terms a `kind` round robin's pairing table waits on.
std::optional<ChessTerms> RoundRobinTermsOf(GameKind kind, const HubStore::ChessEventRow& row);

/// What a `row.kind` round robin's stored body carries beyond the fields
/// every round robin's does: its game's word as "game", unless its game's
/// bodies name none, and its adapter's own fields.
std::vector<std::pair<std::string, std::string>> RoundRobinBodyOf(
    const HubStore::ChessEventRow& row);

/// Reads a stored round robin body's string fields onto `row`: its game
/// from "game" (a body naming none is the one game's whose bodies go
/// unnamed), then that game's own fields. False when the body is no round
/// robin game's.
bool RoundRobinFromBody(const std::map<std::string, std::string>& fields,
                        HubStore::ChessEventRow& row);

/// Whether a `kind` table's adapter opts into `capability`.
inline bool HasCapability(GameKind kind, Capability capability) {
  return ForKind(kind, [capability](auto adapter) {
    return (decltype(adapter)::kCapabilities & capability) != 0;
  });
}

/// Whether opening a `kind` table starts a game the event log records.
inline bool OpenStartsAGame(GameKind kind) {
  return ForKind(kind, [](auto adapter) { return decltype(adapter)::kOpenStartsAGame; });
}

/// Whether the id is a bot some game seats.
inline bool IsBotId(std::string_view player_id) {
  return AnyAdapter([player_id](auto adapter) { return decltype(adapter)::IsBot(player_id); });
}

/// The shared lifecycle announcements, in the table's own envelope. Each
/// game's update union carries the same case for them, so `make` picks the
/// case off whichever union the table's adapter names.
template <typename Make>
moonbase::games::GameEvents InTableEnvelope(GameKind kind, const Make& make) {
  return ForKind(kind, [&](auto adapter) {
    using A = decltype(adapter);
    return A::Wrap(make(typename A::Update{}));
  });
}
moonbase::games::GameEvents CreatedEvent(GameKind kind,
                                         const moonbase::games::GameCreated& created);
moonbase::games::GameEvents StartedEvent(GameKind kind);
/// A table without turns (pong) is never told of one.
moonbase::games::GameEvents TurnEvent(GameKind kind, const moonbase::games::TurnChanged& turn);
moonbase::games::GameEvents LeftEvent(GameKind kind, const moonbase::games::GameLeft& left);

/// One viewer's gameJoined in the table's own vocabulary.
moonbase::games::GameEvents JoinedEventOf(GameKind kind, const TableContext& context,
                                          const std::optional<HostedState>& state,
                                          const std::string& viewer);

/// One viewer's gameState: what a watcher is handed as it sits down.
moonbase::games::GameEvents StateEventOf(GameKind kind, const TableContext& context,
                                         const std::optional<HostedState>& state,
                                         const std::string& viewer);

/// The redaction discipline in one place: each seat's own view (one view
/// for seats and watchers alike at a kWatchable table), then gameEnded for
/// each game past the first `told`, to the seats and the watchers. Returns
/// how many games the table has now told of.
std::size_t StageViews(GameKind kind, const TableContext& context,
                       const std::optional<HostedState>& state, std::size_t told, Staged& staged);

/// What a closed table's seats hear after its final views (StageViews),
/// if its game tells them anything of the close.
void StageClosedNotice(GameKind kind, const TableContext& context,
                       const std::optional<HostedState>& state, Staged& staged);

/// Each game envelope's command and event series, one per union case,
/// from the adapters' lists. The *SeriesMatchTheModelUnions tests hold
/// those lists to the model.
std::vector<CounterSeries> GameCounterSeries();

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H
