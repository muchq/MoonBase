#include "domains/games/apis/games_hub/adapters.h"

#include "absl/log/log.h"

namespace games_hub {

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

}  // namespace games_hub
