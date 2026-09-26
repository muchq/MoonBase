#include "domains/games/apis/games_hub/hub_store.h"

#include <utility>

namespace games_hub {

void MemoryHubStore::Enqueue(std::vector<Op> ops) {
  const std::lock_guard<std::mutex> lock(mu_);
  for (const Op& op : ops) ApplyLocked(op);
}

absl::StatusOr<HubStore::Snapshot> MemoryHubStore::LoadSnapshot() {
  const std::lock_guard<std::mutex> lock(mu_);
  Snapshot snapshot;
  for (const auto& [room_id, surface] : rooms_) snapshot.rooms.push_back({room_id, surface});
  for (const auto& [key, member] : members_) snapshot.members.push_back(member);
  for (const auto& [key, game] : games_) snapshot.games.push_back(game);
  return snapshot;
}

absl::StatusOr<bool> MemoryHubStore::CommitGameSave(const GameRow& row,
                                                    const std::string& /*notify_payload*/) {
  const std::lock_guard<std::mutex> lock(mu_);
  return CommitGameLocked(row);
}

absl::StatusOr<bool> MemoryHubStore::CommitGameFinish(const GameRow& row,
                                                      const std::vector<StatsDelta>& stats,
                                                      const std::string& /*notify_payload*/) {
  const std::lock_guard<std::mutex> lock(mu_);
  if (!CommitGameLocked(row)) return false;
  for (const StatsDelta& delta : stats) {
    const auto member = members_.find({row.room_id, delta.player_id});
    if (member == members_.end()) continue;
    member->second.games_played += delta.played;
    member->second.games_won += delta.won;
    member->second.total_score += delta.score;
  }
  return true;
}

absl::StatusOr<std::optional<HubStore::GameRow>> MemoryHubStore::LoadGame(
    const std::string& room_id, const std::string& game_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto game = games_.find({room_id, game_id});
  if (game == games_.end()) return std::nullopt;
  return game->second;
}

absl::StatusOr<HubStore::RoomRows> MemoryHubStore::LoadRoom(const std::string& room_id) {
  const std::lock_guard<std::mutex> lock(mu_);
  RoomRows rows;
  const auto room = rooms_.find(room_id);
  rows.exists = room != rooms_.end();
  if (!rows.exists) return rows;
  rows.surface = room->second;
  for (const auto& [key, member] : members_) {
    if (key.first == room_id) rows.members.push_back(member);
  }
  for (const auto& [key, game] : games_) {
    if (key.first == room_id) rows.games.push_back(game);
  }
  return rows;
}

bool MemoryHubStore::CommitGameLocked(const GameRow& row) {
  if (!rooms_.contains(row.room_id)) return false;
  const Key key{row.room_id, row.game_id};
  const auto game = games_.find(key);
  if (row.version == 1) {
    if (game != games_.end()) return false;
  } else if (game == games_.end() || game->second.version != row.version - 1) {
    return false;
  }
  games_.erase(key);
  games_.emplace(key, row);
  return true;
}

void MemoryHubStore::ApplyLocked(const Op& op) {
  if (const auto* upsert = std::get_if<UpsertRoom>(&op)) {
    rooms_.emplace(upsert->room_id, upsert->surface);
  } else if (const auto* set = std::get_if<SetRoomSurface>(&op)) {
    if (const auto room = rooms_.find(set->room_id); room != rooms_.end()) {
      room->second = set->surface;
    }
  } else if (const auto* erase = std::get_if<DeleteRoom>(&op)) {
    rooms_.erase(erase->room_id);
    std::erase_if(members_, [&](const auto& entry) { return entry.first.first == erase->room_id; });
    std::erase_if(games_, [&](const auto& entry) { return entry.first.first == erase->room_id; });
  } else if (const auto* upsert = std::get_if<UpsertMember>(&op)) {
    if (rooms_.contains(upsert->row.room_id)) {
      members_[{upsert->row.room_id, upsert->row.player_id}] = upsert->row;
    }
  } else if (const auto* erase = std::get_if<DeleteMember>(&op)) {
    members_.erase({erase->room_id, erase->player_id});
  } else if (const auto* erase = std::get_if<DeleteGame>(&op)) {
    games_.erase({erase->room_id, erase->game_id});
  } else if (std::holds_alternative<TouchRooms>(op) || std::holds_alternative<SweepRooms>(op)) {
    // One process holds every room here, and a crash takes them all with
    // it: there is no fleet to vouch to and no ghost to sweep.
  }
}

}  // namespace games_hub
