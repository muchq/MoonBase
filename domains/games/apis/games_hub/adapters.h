#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

#include "domains/games/apis/games_hub/adapter.h"
#include "domains/games/apis/games_hub/castle_adapter.h"
#include "domains/games/apis/games_hub/chess_adapter.h"
#include "domains/games/apis/games_hub/golf_adapter.h"
#include "domains/games/apis/games_hub/pong_adapter.h"
#include "domains/games/apis/games_hub/rummy_adapter.h"
#include "domains/games/apis/games_hub/spades_adapter.h"

namespace games_hub {

template <std::size_t I>
using EngineAt = std::variant_alternative_t<I, HostedState>;

/// Each HostedState alternative's adapter names the GameKind at its index,
/// so a kind and its engine never disagree.
template <std::size_t... I>
constexpr bool KindsLineUp(std::index_sequence<I...>) {
  return ((Adapter<EngineAt<I>>::kKind == static_cast<GameKind>(I)) && ...);
}
static_assert(KindsLineUp(std::make_index_sequence<std::variant_size_v<HostedState>>{}));

/// The most seats any game's table holds. Stats and deja bound a table's
/// players by it, and otel_contract reads this literal to hold them to it.
inline constexpr std::size_t kTableSeatCap = 4;

template <std::size_t... I>
constexpr std::size_t MostSeats(std::index_sequence<I...>) {
  std::size_t most = 0;
  ((most = Adapter<EngineAt<I>>::kMaxSeats > most ? Adapter<EngineAt<I>>::kMaxSeats : most), ...);
  return most;
}
static_assert(MostSeats(std::make_index_sequence<std::variant_size_v<HostedState>>{}) ==
                  kTableSeatCap,
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
  if constexpr (I + 1 < std::variant_size_v<HostedState>) {
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

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_ADAPTERS_H
