#include "domains/games/apis/golf_hub/hub_handler.h"

#include <utility>
#include <vector>

namespace golf_hub {

using moonbase::golf::GolfCommands;
using moonbase::golf::GolfEvents;

HubHandler::HubHandler(std::shared_ptr<TicketVault> vault, std::chrono::seconds grace_period)
    : vault_(std::move(vault)), registry_([this, grace_period] {
        Registry::Options options;
        options.async_delivery = true;  // chains, not writer threads (ADR-0019)
        options.grace_period = grace_period;
        options.on_expired = [this](const std::string& id) { OnExpired(id); };
        return options;
      }()) {}

smithy::Outcome<moonbase::golf::GetSessionOutput> HubHandler::GetSession(
    const moonbase::golf::GetSessionInput& input,
    const smithy::server::RequestContext& /*context*/) {
  std::string player_id;
  bool token_valid = false;
  if (input.resumeToken.has_value()) {
    if (auto resolved = vault_->ResolveResumeToken(*input.resumeToken)) {
      player_id = std::move(*resolved);
      token_valid = true;
    }
  }
  if (player_id.empty()) player_id = RandomId("p");

  moonbase::golf::GetSessionOutput output;
  output.playerId = player_id;
  output.ticket = vault_->IssueTicket(player_id);
  output.resumeToken = token_valid ? *input.resumeToken : vault_->IssueResumeToken(player_id);
  return output;
}

smithy::eventstream::StreamTask HubHandler::Play(moonbase::golf::PlayInput input,
                                                 moonbase::golf::PlayAsyncServerStream& stream) {
  auto player = vault_->SpendTicket(input.ticket);
  if (!player.has_value()) {
    co_return smithy::Error::Modeled("Unauthenticated", "ticket expired or already spent");
  }
  const std::string player_id = *player;

  // The blessed admission call (ADR-0022): pre-first-suspend, on the
  // launching handler thread, where its brief blocking is legal.
  const auto admission = registry_.ResumeOrAdd(
      player_id, [&stream] { return stream.Share(); }, std::chrono::seconds(1));
  if (admission == Registry::Admission::kRefused) {
    co_return smithy::Error::Modeled("SeatConflict", "player already has a live connection");
  }
  const bool resumed = admission == Registry::Admission::kResumed;

  std::optional<std::string> room;
  if (resumed) {
    SetConnected(player_id, true);
    room = CurrentRoom(player_id);
  }
  moonbase::golf::SessionReady ready;
  ready.playerId = player_id;
  ready.resumed = resumed;
  if (room.has_value()) ready.roomId = *room;
  registry_.SendTo(player_id, GolfEvents::FromSessionready(std::move(ready)));
  // A resumed seat's room sees the connected flip (and the resumer gets
  // the current snapshot it missed).
  if (room.has_value()) BroadcastRoom(*room);

  while (true) {
    auto received = co_await stream.Receive();
    if (!received.ok()) {
      // Abrupt loss (or our own slow-consumer close): park the seat for
      // the grace window (ADR-0020); expiry reaps it. Detach fails only
      // when the entry is already gone — nothing left to do then.
      if (registry_.Detach(player_id)) {
        SetConnected(player_id, false);
        if (auto r = CurrentRoom(player_id)) BroadcastRoom(*r);
      }
      co_return smithy::Unit{};
    }
    if (!received->has_value()) {
      // Clean close: a deliberate leave. Free the seat and the room slot.
      registry_.Remove(player_id);
      if (auto left = LeaveCurrentRoom(player_id)) BroadcastRoom(*left);
      co_return smithy::Unit{};
    }
    HandleCommand(player_id, **received);
  }
}

void HubHandler::HandleCommand(const std::string& player_id, const GolfCommands& command) {
  if (command.as_createRoom_or_null() != nullptr) {
    std::string room_id;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      if (player_room_.contains(player_id)) {
        // Mirrors the Go hub: one room per connection.
      } else {
        room_id = RandomId("r");
        rooms_[room_id].emplace(player_id, true);
        player_room_[player_id] = room_id;
      }
    }
    if (room_id.empty()) {
      Reject(player_id, "already in a room");
    } else {
      BroadcastRoom(room_id);
    }
    return;
  }

  if (const auto* join = command.as_joinRoom_or_null()) {
    bool joined = false;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto room = rooms_.find(join->roomId);
      if (room != rooms_.end() && !player_room_.contains(player_id)) {
        room->second.emplace(player_id, true);
        player_room_[player_id] = join->roomId;
        joined = true;
      }
    }
    if (joined) {
      BroadcastRoom(join->roomId);
    } else {
      Reject(player_id, "room unavailable or already in a room");
    }
    return;
  }

  if (command.as_leaveRoom_or_null() != nullptr) {
    const auto left = LeaveCurrentRoom(player_id);
    if (!left.has_value()) {
      Reject(player_id, "not in a room");
      return;
    }
    moonbase::golf::RoomLeft ack;
    ack.roomId = *left;
    registry_.SendTo(player_id, GolfEvents::FromRoomleft(std::move(ack)));
    BroadcastRoom(*left);
    return;
  }

  if (command.as_getRoomState_or_null() != nullptr) {
    std::optional<RoomStateSnapshot> snapshot;
    {
      const std::lock_guard<std::mutex> lock(mu_);
      const auto it = player_room_.find(player_id);
      if (it != player_room_.end()) snapshot = SnapshotLocked(it->second);
    }
    if (snapshot.has_value()) {
      registry_.SendTo(player_id, GolfEvents::FromRoomstate(snapshot->state));
    } else {
      Reject(player_id, "not in a room");
    }
    return;
  }

  Reject(player_id, "unknown command");
}

void HubHandler::SetConnected(const std::string& player_id, bool connected) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return;
  const auto room = rooms_.find(it->second);
  if (room == rooms_.end()) return;
  const auto member = room->second.find(player_id);
  if (member != room->second.end()) member->second = connected;
}

std::optional<std::string> HubHandler::CurrentRoom(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return std::nullopt;
  return it->second;
}

std::optional<std::string> HubHandler::LeaveCurrentRoom(const std::string& player_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto it = player_room_.find(player_id);
  if (it == player_room_.end()) return std::nullopt;
  const std::string room_id = it->second;
  player_room_.erase(it);
  const auto room = rooms_.find(room_id);
  if (room != rooms_.end()) {
    room->second.erase(player_id);
    if (room->second.empty()) {
      rooms_.erase(room);
      return std::nullopt;  // nobody left to tell
    }
  }
  return room_id;
}

void HubHandler::BroadcastRoom(const std::string& room_id) {
  std::optional<RoomStateSnapshot> snapshot;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    snapshot = SnapshotLocked(room_id);
  }
  if (!snapshot.has_value()) return;
  // Per-recipient construction (identical today; phase 2's per-viewer
  // redaction slots into this callback and nowhere else).
  registry_.Broadcast(snapshot->member_ids, [&snapshot](const std::string& /*recipient*/) {
    return GolfEvents::FromRoomstate(snapshot->state);
  });
}

void HubHandler::Reject(const std::string& player_id, std::string reason) {
  moonbase::golf::CommandRejected rejected;
  rejected.reason = std::move(reason);
  registry_.SendTo(player_id, GolfEvents::FromCommandrejected(std::move(rejected)));
}

void HubHandler::OnExpired(const std::string& player_id) {
  // Grace ran out (ADR-0020): the seat is gone; free the room slot and
  // tell whoever remains. Runs on the registry's expiry thread.
  if (auto left = LeaveCurrentRoom(player_id)) BroadcastRoom(*left);
}

std::optional<HubHandler::RoomStateSnapshot> HubHandler::SnapshotLocked(
    const std::string& room_id) {
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return std::nullopt;
  RoomStateSnapshot snapshot;
  snapshot.state.roomId = room_id;
  for (const auto& [member_id, connected] : room->second) {
    moonbase::golf::PlayerInfo info;
    info.playerId = member_id;
    info.connected = connected;
    snapshot.state.players.push_back(std::move(info));
    snapshot.member_ids.push_back(member_id);
  }
  return snapshot;
}

}  // namespace golf_hub
