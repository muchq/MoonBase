#include "domains/games/apis/games_hub/adapters.h"

#include <algorithm>

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

nlohmann::json RoundRobinBodyOf(const HubStore::ChessEventRow& row) {
  return ForKind(RoundRobinKind(row), [&](auto adapter) {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      nlohmann::json fields = A::RoundRobinBody(std::get<typename A::Terms>(row.terms));
      if (A::kRoundRobinBodyNamed) fields["game"] = A::kName;
      return fields;
    } else {
      return nlohmann::json::object();
    }
  });
}

bool RoundRobinFromBody(const nlohmann::json& body, HubStore::ChessEventRow& row) {
  const bool named = body.contains("game");
  if (named && !body["game"].is_string()) return false;
  const std::optional<GameKind> kind =
      named ? ParseGameKind(body["game"].get<std::string>())
            : std::optional(static_cast<GameKind>(UnnamedRoundRobinGame()));
  if (!kind.has_value()) return false;
  return ForKind(*kind, [&](auto adapter) {
    using A = decltype(adapter);
    if constexpr (kHas<A, kRoundRobins>) {
      if (named != A::kRoundRobinBodyNamed) return false;
      auto terms = A::FromRoundRobinBody(body);
      if (!terms.has_value()) return false;
      row.terms = *std::move(terms);
      return true;
    } else {
      return false;
    }
  });
}

nlohmann::json WaitingTermsJson(GameKind kind, const HostedTerms& terms) {
  return ForKind(kind, [&](auto adapter) {
    using A = decltype(adapter);
    if constexpr (kHas<A, kChallenges>) {
      return A::TermsJson(std::get<typename A::Terms>(terms));
    } else {
      return nlohmann::json();
    }
  });
}

std::optional<HostedTerms> WaitingTermsFromJson(GameKind kind, const nlohmann::json& stored) {
  return ForKind(kind, [&](auto adapter) -> std::optional<HostedTerms> {
    using A = decltype(adapter);
    if constexpr (kHas<A, kChallenges>) {
      return A::TermsFromJson(stored);
    } else {
      return std::nullopt;
    }
  });
}

std::string TermsLineOf(GameKind kind, const HostedTerms& terms) {
  return ForKind(kind, [&](auto adapter) {
    using A = decltype(adapter);
    if constexpr (kHas<A, kChallenges>) {
      return A::TermsLine(std::get<typename A::Terms>(terms));
    } else {
      return std::string();
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
      return A::Wrap(A::Update::FromRoundrobin(A::RoundRobinWire(held)));
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
      typename A::RoundRobins round_robins;
      for (const HeldRoundRobin& one : held) {
        round_robins.roundRobins.push_back(A::RoundRobinWire(one));
      }
      return A::Wrap(A::Update::FromRoundrobins(std::move(round_robins)));
    } else {
      return std::nullopt;
    }
  });
}

std::optional<HostedTerms> RoundRobinTermsOf(const HubStore::ChessEventRow& row) {
  if (!HasCapability(RoundRobinKind(row), kChallenges)) return std::nullopt;
  return row.terms;
}

std::optional<HubStore::ArchivedGame> ArchivedGameOf(const HubStore::GameRow& row) {
  if (!row.state.has_value()) return std::nullopt;
  return VisitAdapter(
      *row.state, [&](auto adapter, const auto& engine) -> std::optional<HubStore::ArchivedGame> {
        using A = decltype(adapter);
        if constexpr (kHas<A, kArchived>) {
          auto archived = A::Archived(engine);
          if (archived.has_value()) {
            std::visit([&](auto& game) { game.game_id = row.game_id; }, *archived);
          }
          return archived;
        } else {
          return std::nullopt;
        }
      });
}

void AddRoundRobinTableGames(const std::vector<HubStore::GameRow>& games,
                             std::vector<HubStore::ChessEventRow>& events) {
  std::vector<std::pair<const HubStore::GameRow*, HubStore::EventGame>> tagged;
  for (const HubStore::GameRow& game : games) {
    if (!game.event.has_value()) continue;
    std::optional<HubStore::EventGame> played =
        VisitTable(game.kind, game.state,
                   [](auto adapter, const auto* engine) -> std::optional<HubStore::EventGame> {
                     using A = decltype(adapter);
                     if constexpr (kHas<A, kRoundRobins>) {
                       if (engine != nullptr) return A::TableGame(*engine);
                     }
                     return std::nullopt;
                   });
    if (!played.has_value()) continue;
    played->game_id = game.game_id;
    played->pairing = game.event->pairing;
    tagged.emplace_back(&game, *std::move(played));
  }
  // By code, so every instance reads a pairing's tables in one order.
  std::sort(tagged.begin(), tagged.end(),
            [](const auto& a, const auto& b) { return a.first->game_id < b.first->game_id; });
  for (HubStore::ChessEventRow& event : events) {
    for (const auto& [game, played] : tagged) {
      if (game->kind != RoundRobinKind(event) || game->room_id != event.room_id ||
          game->event->event_id != event.event_id) {
        continue;
      }
      event.games.push_back(played);
    }
  }
}

}  // namespace games_hub
