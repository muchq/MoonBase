#include "domains/games/apis/games_hub/hub_store.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/libs/chess_play/game_state_serde.h"

namespace games_hub {

std::optional<HubStore::ChessGameRow> ArchivedChessGame(const HubStore::GameRow& row) {
  if (!row.state.has_value()) return std::nullopt;
  const auto* table = std::get_if<chess_play::Table>(&*row.state);
  if (table == nullptr || !table->game().isOver()) return std::nullopt;
  return HubStore::ChessGameRow{
      0, row.game_id, static_cast<int>(table->scoreSheet().size()), table->game(), 0, false};
}

MemoryHubStore::MemoryHubStore(std::function<int64_t()> now_ms)
    : now_ms_(now_ms ? std::move(now_ms) : [] { return absl::ToUnixMillis(absl::Now()); }) {}

void MemoryHubStore::Enqueue(std::vector<Op> ops) {
  const std::lock_guard<std::mutex> lock(mu_);
  for (const Op& op : ops) ApplyLocked(op);
}

absl::StatusOr<HubStore::Snapshot> MemoryHubStore::LoadSnapshot() {
  const std::lock_guard<std::mutex> lock(mu_);
  Snapshot snapshot;
  for (const auto& [room_id, room] : rooms_) {
    snapshot.rooms.push_back({room_id, room.surface, room.chess_published});
  }
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
  rows.surface = room->second.surface;
  rows.chess_published = room->second.chess_published;
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
  if (auto archived = ArchivedChessGame(row); archived.has_value()) {
    // Keyed as postgres keys it: the same table code, line and game.
    RoomData& room = rooms_.at(row.room_id);
    const std::string serialized = chess_play::serializeGameState(archived->game);
    const bool known = std::any_of(
        room.chess_games.begin(), room.chess_games.end(), [&](const ChessGameRow& kept) {
          return kept.game_id == archived->game_id && kept.ordinal == archived->ordinal &&
                 chess_play::serializeGameState(kept.game) == serialized;
        });
    if (!known) {
      archived->archive_id = ++last_archive_id_;
      archived->ended_at_ms = now_ms_();
      archived->published = room.chess_published;
      if (archived->published) {
        published_.push_back({archived->archive_id, archived->game, archived->ended_at_ms});
      }
      room.chess_games.push_back(*std::move(archived));
    }
  }
  return true;
}

void MemoryHubStore::ApplyLocked(const Op& op) {
  if (const auto* upsert = std::get_if<UpsertRoom>(&op)) {
    rooms_.emplace(upsert->room_id, RoomData{upsert->surface, false, {}});
  } else if (const auto* set = std::get_if<SetRoomSurface>(&op)) {
    if (const auto room = rooms_.find(set->room_id); room != rooms_.end()) {
      room->second.surface = set->surface;
    }
  } else if (const auto* set = std::get_if<SetChessPublished>(&op)) {
    if (const auto room = rooms_.find(set->room_id); room != rooms_.end()) {
      room->second.chess_published = set->published;
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
  } else if (const auto* sweep = std::get_if<SweepPublishedChess>(&op)) {
    const int64_t cutoff =
        now_ms_() -
        std::chrono::duration_cast<std::chrono::milliseconds>(sweep->older_than).count();
    std::erase_if(published_,
                  [&](const PublishedChessGame& game) { return game.ended_at_ms < cutoff; });
  } else if (std::holds_alternative<TouchRooms>(op) || std::holds_alternative<SweepRooms>(op)) {
    // One process holds every room here, and a crash takes them all with
    // it: there is no fleet to vouch to and no ghost to sweep.
  }
}

absl::StatusOr<HubStore::ChessHistory> MemoryHubStore::LoadChessHistory(const std::string& room_id,
                                                                        int limit) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return absl::NotFoundError("no such room");
  ChessHistory history;
  history.published = room->second.chess_published;
  const auto& games = room->second.chess_games;
  for (auto game = games.rbegin(); game != games.rend() && std::ssize(history.games) < limit;
       ++game) {
    history.games.push_back(*game);
  }
  return history;
}

absl::StatusOr<std::optional<HubStore::ChessGameRow>> MemoryHubStore::LoadChessGame(
    const std::string& room_id, const ChessGameKey& key) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return std::nullopt;
  const auto& games = room->second.chess_games;
  for (auto game = games.rbegin(); game != games.rend(); ++game) {
    const bool named = key.archive_id.has_value()
                           ? game->archive_id == *key.archive_id
                           : game->game_id == key.game_id && game->ordinal == key.ordinal;
    if (named) return *game;
  }
  return std::nullopt;
}

absl::StatusOr<std::vector<HubStore::PublishedChessGame>> MemoryHubStore::LoadPublishedChess(
    int64_t after_archive_id, int limit) {
  const std::lock_guard<std::mutex> lock(mu_);
  std::vector<PublishedChessGame> page;
  for (const PublishedChessGame& game : published_) {
    if (std::ssize(page) >= limit) break;
    if (game.archive_id > after_archive_id) page.push_back(game);
  }
  return page;
}

}  // namespace games_hub
