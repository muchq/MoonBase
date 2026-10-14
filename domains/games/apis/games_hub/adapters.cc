#include "domains/games/apis/games_hub/adapters.h"

#include "absl/log/log.h"

namespace games_hub {
namespace {

template <typename A>
constexpr bool UnnamedRoundRobinBodies() {
  if constexpr (kHas<A, kRoundRobins>) {
    return !A::kRoundRobinBodyNamed;
  } else {
    return false;
  }
}

// The index of the one round robin game whose bodies name no game.
constexpr std::size_t UnnamedRoundRobinGame() {
  return []<std::size_t... I>(std::index_sequence<I...>) {
    std::size_t found = kGames;
    std::size_t count = 0;
    ((UnnamedRoundRobinBodies<Adapter<EngineAt<I>>>() ? (found = I, ++count) : count), ...);
    return count == 1 ? found : kGames;
  }(std::make_index_sequence<kGames>{});
}
static_assert(UnnamedRoundRobinGame() < kGames,
              "exactly one round robin game's stored bodies name no game");

}  // namespace

std::vector<std::pair<std::string, std::string>> RoundRobinBodyOf(
    const HubStore::ChessEventRow& row) {
  return ForKind(row.kind, [&](auto adapter) -> std::vector<std::pair<std::string, std::string>> {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      auto fields = A::RoundRobinBody(row);
      if (A::kRoundRobinBodyNamed) fields.emplace_back("game", std::string(A::kName));
      return fields;
    } else {
      return {};
    }
  });
}

bool RoundRobinFromBody(const std::map<std::string, std::string>& fields,
                        HubStore::ChessEventRow& row) {
  const auto game = fields.find("game");
  const std::optional<GameKind> kind =
      game == fields.end() ? std::optional(static_cast<GameKind>(UnnamedRoundRobinGame()))
                           : ParseGameKind(game->second);
  if (!kind.has_value()) return false;
  return ForKind(*kind, [&](auto adapter) {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      if (game != fields.end() && !A::kRoundRobinBodyNamed) return false;
      row.kind = A::kKind;
      return A::FromRoundRobinBody(fields, row);
    } else {
      return false;
    }
  });
}

moonbase::games::GameEvents CreatedEvent(GameKind kind,
                                         const moonbase::games::GameCreated& created) {
  return InTableEnvelope(kind, [&](auto tag) { return decltype(tag)::FromGamecreated(created); });
}

moonbase::games::GameEvents StartedEvent(GameKind kind) {
  return InTableEnvelope(kind, [](auto tag) {
    return decltype(tag)::FromGamestarted(moonbase::games::GameStarted{});
  });
}

moonbase::games::GameEvents TurnEvent(GameKind kind, const moonbase::games::TurnChanged& turn) {
  return InTableEnvelope(kind, [&](auto tag) {
    if constexpr (requires { decltype(tag)::FromTurnchanged(turn); }) {
      return decltype(tag)::FromTurnchanged(turn);
    } else {
      LOG(DFATAL) << "a turn announced at a table without turns";
      return decltype(tag)::FromGamestarted(moonbase::games::GameStarted{});
    }
  });
}

moonbase::games::GameEvents LeftEvent(GameKind kind, const moonbase::games::GameLeft& left) {
  return InTableEnvelope(kind, [&](auto tag) { return decltype(tag)::FromGameleft(left); });
}

moonbase::games::GameEvents JoinedEventOf(GameKind kind, const TableContext& context,
                                          const std::optional<HostedState>& state,
                                          const std::string& viewer) {
  return VisitTable(kind, state, [&](auto adapter, const auto* engine) {
    using A = decltype(adapter);
    return A::JoinedEvent(A::ViewOf(context, engine, viewer));
  });
}

moonbase::games::GameEvents StateEventOf(GameKind kind, const TableContext& context,
                                         const std::optional<HostedState>& state,
                                         const std::string& viewer) {
  return VisitTable(kind, state, [&](auto adapter, const auto* engine) {
    using A = decltype(adapter);
    return A::StateEvent(A::ViewOf(context, engine, viewer));
  });
}

std::size_t StageViews(GameKind kind, const TableContext& context,
                       const std::optional<HostedState>& state, std::size_t told, Staged& staged) {
  return VisitTable(kind, state, [&](auto adapter, const auto* engine) {
    using A = decltype(adapter);
    if constexpr ((A::kCapabilities & kWatchable) != 0) {
      const moonbase::games::GameEvents event = A::StateEvent(A::ViewOf(context, engine, ""));
      for (const std::string& recipient : context.roster) staged.emplace_back(recipient, event);
      for (const std::string& watcher : context.watchers) staged.emplace_back(watcher, event);
    } else {
      for (const std::string& recipient : context.roster) {
        staged.emplace_back(recipient, A::StateEvent(A::ViewOf(context, engine, recipient)));
      }
    }
    if (engine == nullptr) return told;
    for (const moonbase::games::GameEvents& ended : A::GamesEnded(*engine, told)) {
      for (const std::string& recipient : context.roster) staged.emplace_back(recipient, ended);
      for (const std::string& watcher : context.watchers) staged.emplace_back(watcher, ended);
    }
    return A::GamesFinished(*engine);
  });
}

void StageClosedNotice(GameKind kind, const TableContext& context,
                       const std::optional<HostedState>& state, Staged& staged) {
  VisitTable(kind, state, [&](auto adapter, const auto* engine) {
    if (engine == nullptr) return;
    const auto closed = decltype(adapter)::ClosedEvent(*engine, context.roster);
    if (!closed.has_value()) return;
    for (const std::string& recipient : context.roster) staged.emplace_back(recipient, *closed);
  });
}

std::vector<CounterSeries> GameCounterSeries() {
  std::vector<CounterSeries> series;
  AnyAdapter([&](auto adapter) {
    using A = decltype(adapter);
    for (const std::string_view command : A::kCommands) {
      series.push_back({std::string(A::kCommandSeries), {{"command", std::string(command)}}});
    }
    for (const std::string_view event : A::kEvents) {
      series.push_back({std::string(A::kEventSeries), {{"event", std::string(event)}}});
    }
    return false;
  });
  return series;
}

std::optional<moonbase::games::GameEvents> RoundRobinEventOf(GameKind kind,
                                                             const HeldRoundRobin& held) {
  return ForKind(kind, [&](auto adapter) -> std::optional<moonbase::games::GameEvents> {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      return A::RoundRobinEvent(held);
    } else {
      return std::nullopt;
    }
  });
}

std::optional<moonbase::games::GameEvents> RoundRobinsEventOf(
    GameKind kind, const std::vector<HeldRoundRobin>& held) {
  return ForKind(kind, [&](auto adapter) -> std::optional<moonbase::games::GameEvents> {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      return A::RoundRobinsEvent(held);
    } else {
      return std::nullopt;
    }
  });
}

std::optional<ChessTerms> RoundRobinTermsOf(GameKind kind, const HubStore::ChessEventRow& row) {
  return ForKind(kind, [&](auto adapter) -> std::optional<ChessTerms> {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      return A::RoundRobinTerms(row);
    } else {
      return std::nullopt;
    }
  });
}

}  // namespace games_hub
