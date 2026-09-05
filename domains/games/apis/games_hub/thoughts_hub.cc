#include "domains/games/apis/games_hub/thoughts_hub.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "domains/games/apis/games_hub/protocol_input.h"
#include "domains/games/apis/games_hub/rate_limiter.h"
#include "smithy/core/error.h"

namespace games_hub {

using moonbase::games::LobbyUpdate;
using moonbase::games::ThoughtsCommands;
using moonbase::games::ThoughtsEvents;

namespace {

// The world's update as this stream's event: the same members, minus the
// session's own sessionReady and commandRejected.
ThoughtsEvents ToThoughtsEvent(LobbyUpdate update) {
  if (const auto* state = update.as_worldState_or_null()) {
    return ThoughtsEvents::FromWorldstate(*state);
  }
  if (const auto* joined = update.as_playerJoined_or_null()) {
    return ThoughtsEvents::FromPlayerjoined(*joined);
  }
  if (const auto* moved = update.as_playerMoved_or_null()) {
    return ThoughtsEvents::FromPlayermoved(*moved);
  }
  if (const auto* changed = update.as_shapeChanged_or_null()) {
    return ThoughtsEvents::FromShapechanged(*changed);
  }
  if (const auto* left = update.as_playerLeft_or_null()) {
    return ThoughtsEvents::FromPlayerleft(*left);
  }
  // Every update is World's, and World speaks the cases above; a new one
  // is a case to map here, not to drop.
  LOG(FATAL) << "lobby update with no Think event: " << update.case_name();
}

}  // namespace

ThoughtsHub::ThoughtsHub(std::shared_ptr<TicketVault> vault,
                         std::shared_ptr<futility::otel::MetricsRecorder> metrics,
                         ThoughtsLimits limits, ThoughtsTestHooks hooks)
    : vault_(std::move(vault)),
      metrics_(std::move(metrics)),
      limits_(limits),
      hooks_(std::move(hooks)),
      registry_([] {
        Registry::Options options;
        options.async_delivery = true;  // chains, not writer threads (ADR-0019)
        // No grace: a closed socket leaves the world. The registry then
        // never parks a seat, so ResumeOrAdd only ever adds or refuses.
        options.grace_period = std::chrono::seconds(0);
        return options;
      }()) {
  if (metrics_) {
    for (const CounterSeries& series : DeclaredCounterSeries()) {
      metrics_->DeclareCounter(series.name, series.attributes);
    }
  }
}

// Written out in full so each entry greps against its emit site (the
// GolfHub::DeclaredCounterSeries convention). thoughts_rejections
// declares only the kinds this hub can emit — there is no engine to
// refuse a move and no store to be unavailable.
const std::vector<CounterSeries>& ThoughtsHub::DeclaredCounterSeries() {
  static const auto* kSeries = new std::vector<CounterSeries>{
      {"thoughts_admissions_refused", {{"reason", "bad_ticket"}}},
      {"thoughts_admissions_refused", {{"reason", "seat_conflict"}}},
      // The ThoughtsCommands union in model order — HandleCommand's naming.
      {"thoughts_commands", {{"command", "join"}}},
      {"thoughts_commands", {{"command", "move"}}},
      {"thoughts_commands", {{"command", "shape"}}},
      {"thoughts_commands", {{"command", "leave"}}},
      {"thoughts_disconnects", {{"kind", "clean"}}},
      {"thoughts_disconnects", {{"kind", "abrupt"}}},
      // The ThoughtsEvents union in model order — Send's naming.
      {"thoughts_events", {{"event", "sessionReady"}}},
      {"thoughts_events", {{"event", "worldState"}}},
      {"thoughts_events", {{"event", "playerJoined"}}},
      {"thoughts_events", {{"event", "playerMoved"}}},
      {"thoughts_events", {{"event", "shapeChanged"}}},
      {"thoughts_events", {{"event", "playerLeft"}}},
      {"thoughts_events", {{"event", "commandRejected"}}},
      {"thoughts_rate_limited", {}},
      {"thoughts_rejections", {{"kind", "rate_limited"}}},
      {"thoughts_rejections", {{"kind", "invalid"}}},
      {"thoughts_rejections", {{"kind", "state"}}},
      {"thoughts_rejections", {{"kind", "unknown"}}},
      {"thoughts_sessions", {}},
  };
  return *kSeries;
}

smithy::eventstream::StreamTask ThoughtsHub::Think(
    moonbase::games::ThinkInput input, moonbase::games::ThinkAsyncServerStream& stream) {
  if (HasEmbeddedNul(input.ticket)) {
    Count("thoughts_admissions_refused", {{"reason", "bad_ticket"}});
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  auto player = vault_->SpendTicket(input.ticket);
  if (!player.has_value()) {
    Count("thoughts_admissions_refused", {{"reason", "bad_ticket"}});
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  const std::string player_id = *player;

  // The blessed admission call (ADR-0022), pre-first-suspend on the
  // launching thread. Without grace there is nothing to resume, so this
  // is add-or-refuse: a second live socket for the same player is turned
  // away rather than allowed to shadow the first.
  const auto admission = registry_.ResumeOrAdd(
      player_id, [&stream] { return stream.Share(); }, std::chrono::seconds(1));
  if (admission == Registry::Admission::kRefused) {
    Count("thoughts_admissions_refused", {{"reason", "seat_conflict"}});
    co_return smithy::Error::Modeled("SeatConflict", "player already has a live connection");
  }
  Count("thoughts_sessions");
  TrackActive(+1);

  moonbase::games::SessionReady ready;
  ready.playerId = player_id;
  ready.resumed = false;
  Send(player_id, ThoughtsEvents::FromSessionready(std::move(ready)));

  // Owned by this coroutine frame: frames are handled sequentially per
  // session, so no locking, and the budget dies with the connection.
  TokenBucket budget(limits_.command_burst, limits_.command_refill_per_sec);

  while (true) {
    auto received = co_await stream.Receive();
    if (!received.ok() || !received->has_value()) {
      // Any close is a leave: whoever remains hears playerLeft, and the seat
      // is removed rather than parked. The world entry goes first, while
      // this seat still blocks admission of the same player: once Remove
      // returns, a reconnect can be admitted, and its join must find the
      // world already empty of its predecessor rather than be refused as
      // "already in the world" — or worse, be erased by the old frame's
      // leave.
      Leave(player_id);
      if (hooks_.before_seat_release) hooks_.before_seat_release(player_id);
      registry_.Remove(player_id);
      TrackActive(-1);
      Count("thoughts_disconnects", {{"kind", received.ok() ? "clean" : "abrupt"}});
      co_return smithy::Unit{};
    }
    if (!budget.Admit(std::chrono::steady_clock::now())) {
      // Refused before any locked work, and the session stays open: the
      // bucket already bounds the damage, and closing would only convert
      // a flood into reconnect load.
      Count("thoughts_rate_limited");
      Reject(player_id, RejectKind::kRateLimited, "slow down");
      continue;
    }
    HandleCommand(player_id, **received);
  }
}

void ThoughtsHub::HandleCommand(const std::string& player_id, const ThoughtsCommands& command) {
  Count("thoughts_commands", {{"command", std::string(command.case_name())}});

  std::optional<World::Refusal> refusal;
  if (const auto* join = command.as_join_or_null()) {
    if (const auto problem = World::RoomProblem(join->roomId)) {
      Reject(player_id, RejectKind::kInvalid, *problem);
      return;
    }
    const std::lock_guard<std::mutex> lock(mu_);
    World::Deliveries deliveries;
    refusal = world_.Join(player_id, join->roomId.value_or(World::kPlaza), *join, deliveries);
    if (!refusal.has_value()) {
      // The joiner's snapshot is the first delivery; the seam sits right
      // behind it, with the lock still held.
      Send(deliveries.front().to, ToThoughtsEvent(std::move(deliveries.front().update)));
      if (hooks_.after_snapshot_queued) hooks_.after_snapshot_queued();
      deliveries.erase(deliveries.begin());
      SendLocked(deliveries);
    }
  } else if (const auto* move = command.as_move_or_null()) {
    const std::lock_guard<std::mutex> lock(mu_);
    World::Deliveries deliveries;
    refusal = world_.Move(player_id, *move, deliveries);
    SendLocked(deliveries);
  } else if (const auto* shape = command.as_shape_or_null()) {
    const std::lock_guard<std::mutex> lock(mu_);
    World::Deliveries deliveries;
    refusal = world_.Shape(player_id, *shape, deliveries);
    SendLocked(deliveries);
  } else if (command.as_leave_or_null() != nullptr) {
    if (!Leave(player_id)) refusal = World::Refusal{RejectKind::kState, "not in the world"};
  } else {
    refusal = World::Refusal{RejectKind::kUnknown, "unknown command"};
  }
  if (refusal.has_value()) Reject(player_id, refusal->kind, std::move(refusal->reason));
}

bool ThoughtsHub::Leave(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  World::Deliveries deliveries;
  const bool left = world_.Leave(player_id, deliveries);
  SendLocked(deliveries);
  return left;
}

void ThoughtsHub::Reject(const std::string& player_id, RejectKind kind, std::string reason) {
  // The bounded kind is the metric; the free text goes only to the player.
  Count("thoughts_rejections", {{"kind", RejectKindName(kind)}});
  moonbase::games::CommandRejected rejected;
  rejected.reason = std::move(reason);
  Send(player_id, ThoughtsEvents::FromCommandrejected(std::move(rejected)));
}

void ThoughtsHub::Send(const std::string& player_id, ThoughtsEvents event) {
  if (metrics_) {
    metrics_->RecordCounter("thoughts_events", 1, {{"event", std::string(event.case_name())}});
  }
  registry_.SendTo(player_id, std::move(event));
}

void ThoughtsHub::SendLocked(World::Deliveries& deliveries) {
  for (auto& delivery : deliveries) {
    Send(delivery.to, ToThoughtsEvent(std::move(delivery.update)));
  }
  deliveries.clear();
}

void ThoughtsHub::Count(const char* name, const std::map<std::string, std::string>& attributes) {
  if (metrics_) metrics_->RecordCounter(name, 1, attributes);
}

void ThoughtsHub::TrackActive(int delta) {
  // Delta form, like golf_sessions_active: the collector sums an up-down
  // counter into the live-session count.
  if (metrics_) metrics_->RecordGauge("thoughts_sessions_active", delta);
}

}  // namespace games_hub
