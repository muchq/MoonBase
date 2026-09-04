#include "domains/games/apis/games_hub/thoughts_hub.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domains/games/apis/games_hub/protocol_input.h"
#include "domains/games/apis/games_hub/rate_limiter.h"
#include "smithy/core/error.h"

namespace games_hub {

using moonbase::games::ThoughtsCommands;
using moonbase::games::ThoughtsEvents;
using moonbase::games::WorldPlayer;

namespace {

// The world's rules, each answering with the reason a client is told.
// NaN fails every comparison, so it is refused by the bounds checks
// themselves rather than by a separate finiteness rule.
std::optional<std::string> PositionProblem(const std::vector<double>& position) {
  if (position.size() != 3) return "position must be [x, y, z]";
  if (!(position[1] == 0.0)) return "y must be 0";
  const double limit = ThoughtsHub::kWorldHalfExtent;
  if (!(std::abs(position[0]) <= limit) || !(std::abs(position[2]) <= limit)) {
    return "position out of bounds (±50)";
  }
  return std::nullopt;
}

std::optional<std::string> ColorProblem(const std::vector<double>& color) {
  if (color.size() != 3) return "color must be [r, g, b]";
  for (const double component : color) {
    if (!(component >= 0.0 && component <= 1.0)) return "color components must be within 0..1";
  }
  return std::nullopt;
}

std::optional<std::string> ShapeProblem(std::int32_t shape) {
  if (shape < 0 || shape > 2) return "shape must be 0 (sphere), 1 (cube) or 2 (pyramid)";
  return std::nullopt;
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

  if (const auto* join = command.as_join_or_null()) {
    for (const auto& problem :
         {PositionProblem(join->position), ColorProblem(join->color), ShapeProblem(join->shape)}) {
      if (problem.has_value()) {
        Reject(player_id, RejectKind::kInvalid, *problem);
        return;
      }
    }
    WorldPlayer entry;
    entry.playerId = player_id;
    entry.position = join->position;
    entry.color = join->color;
    entry.shape = join->shape;
    bool already_joined = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (world_.contains(player_id)) {
        already_joined = true;
      } else {
        moonbase::games::WorldState snapshot;
        for (const auto& [id, other] : world_) snapshot.players.push_back(other);
        world_.emplace(player_id, entry);
        // Queued to the joiner while mu_ is still held, deliberately: every
        // other player's mutation takes mu_ before it fans out, so with the
        // snapshot queued inside this hold, any playerLeft or playerMoved
        // that reaches the joiner was either already applied to the
        // snapshot or lands behind it. Queued after the unlock, a leave
        // could slip its playerLeft in ahead of a snapshot that still lists
        // the leaver — a ghost nothing later removes. Only the joiner's own
        // queue is touched here, so no other session's close path can be
        // completed inline under this lock.
        Send(player_id, ThoughtsEvents::FromWorldstate(std::move(snapshot)));
        if (hooks_.after_snapshot_queued) hooks_.after_snapshot_queued();
      }
    }
    if (already_joined) {
      Reject(player_id, RejectKind::kState, "already in the world; leave first");
      return;
    }
    // Then the others hear of the joiner. Outside mu_: a fan-out can close a
    // slow session, and on the in-memory wire that completes its Receive —
    // and so its Leave — inline on this thread.
    moonbase::games::PlayerJoined joined;
    joined.player = std::move(entry);
    SendToOthers(player_id, ThoughtsEvents::FromPlayerjoined(std::move(joined)));
    return;
  }

  if (const auto* move = command.as_move_or_null()) {
    if (const auto problem = PositionProblem(move->position)) {
      Reject(player_id, RejectKind::kInvalid, *problem);
      return;
    }
    bool in_world = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (const auto it = world_.find(player_id); it != world_.end()) {
        it->second.position = move->position;
        in_world = true;
      }
    }
    if (!in_world) {
      Reject(player_id, RejectKind::kState, "join the world first");
      return;
    }
    moonbase::games::PlayerMoved moved;
    moved.playerId = player_id;
    moved.position = move->position;
    SendToOthers(player_id, ThoughtsEvents::FromPlayermoved(std::move(moved)));
    return;
  }

  if (const auto* shape = command.as_shape_or_null()) {
    if (const auto problem = ShapeProblem(shape->shape)) {
      Reject(player_id, RejectKind::kInvalid, *problem);
      return;
    }
    bool in_world = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (const auto it = world_.find(player_id); it != world_.end()) {
        it->second.shape = shape->shape;
        in_world = true;
      }
    }
    if (!in_world) {
      Reject(player_id, RejectKind::kState, "join the world first");
      return;
    }
    moonbase::games::ShapeChanged changed;
    changed.playerId = player_id;
    changed.shape = shape->shape;
    SendToOthers(player_id, ThoughtsEvents::FromShapechanged(std::move(changed)));
    return;
  }

  if (command.as_leave_or_null() != nullptr) {
    if (!Leave(player_id)) Reject(player_id, RejectKind::kState, "not in the world");
    return;
  }

  Reject(player_id, RejectKind::kUnknown, "unknown command");
}

bool ThoughtsHub::Leave(const std::string& player_id) {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    if (world_.erase(player_id) == 0) return false;
  }
  moonbase::games::PlayerLeft left;
  left.playerId = player_id;
  SendToOthers(player_id, ThoughtsEvents::FromPlayerleft(std::move(left)));
  return true;
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

void ThoughtsHub::SendToOthers(const std::string& sender_id, const ThoughtsEvents& event) {
  std::vector<std::string> others;
  for (auto& id : registry_.Ids()) {
    if (id != sender_id) others.push_back(std::move(id));
  }
  if (others.empty()) return;
  if (metrics_) {
    // One per delivery, as Send counts: the series is how many events left
    // the hub, not how many commands produced them.
    metrics_->RecordCounter("thoughts_events", static_cast<int64_t>(others.size()),
                            {{"event", std::string(event.case_name())}});
  }
  registry_.Broadcast(others, event);
}

void ThoughtsHub::Count(const char* name, const std::map<std::string, std::string>& attributes) {
  if (metrics_) metrics_->RecordCounter(name, 1, attributes);
}

void ThoughtsHub::TrackActive(int delta) {
  // Delta form, like stream_sessions_active: the collector sums an up-down
  // counter into the live-session count.
  if (metrics_) metrics_->RecordGauge("thoughts_sessions_active", delta);
}

}  // namespace games_hub
