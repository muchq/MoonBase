#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H

#include <concepts>
#include <cstddef>
#include <optional>
#include <string>
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
  { A::WithoutSeat(engine, 0, int64_t{0}) } -> std::same_as<absl::StatusOr<typename A::Engine>>;
  { A::ViewOf(context, &engine, id) } -> std::same_as<typename A::View>;
  { A::StateEvent(view) } -> std::same_as<moonbase::games::GameEvents>;
  { A::JoinedEvent(view) } -> std::same_as<moonbase::games::GameEvents>;
  {
    A::GamesEnded(engine, std::size_t{0})
  } -> std::same_as<std::vector<moonbase::games::GameEvents>>;
  { A::ClosedEvent(engine, roster) } -> std::same_as<std::optional<moonbase::games::GameEvents>>;
};

/// Each HostedState alternative has a full adapter that names the GameKind
/// at its index, so a kind and its engine never disagree.
template <std::size_t... I>
constexpr bool KindsLineUp(std::index_sequence<I...>) {
  return ((GameAdapter<Adapter<EngineAt<I>>> &&
           std::is_same_v<typename Adapter<EngineAt<I>>::Engine, EngineAt<I>> &&
           Adapter<EngineAt<I>>::kKind == static_cast<GameKind>(I)) &&
          ...);
}
static_assert(KindsLineUp(std::make_index_sequence<kGames>{}));

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

/// Whether a `kind` table's adapter opts into `capability`.
inline bool HasCapability(GameKind kind, Capability capability) {
  return ForKind(kind, [capability](auto adapter) {
    return (decltype(adapter)::kCapabilities & capability) != 0;
  });
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
