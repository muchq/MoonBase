#include "domains/games/apis/games_hub/memory_hub_store.h"

#include <algorithm>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/apis/games_hub/adapters.h"
#include "domains/games/libs/chess_play/game_state_serde.h"
#include "domains/games/libs/go/table_serde.h"

namespace games_hub {

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
  for (const auto& [room_id, room] : rooms_) {
    for (ChessEventRow& event : EventsLocked(room)) snapshot.events.push_back(std::move(event));
  }
  AddRoundRobinTableGames(snapshot.games, snapshot.events);
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
  rows.events = EventsLocked(room->second);
  AddRoundRobinTableGames(rows.games, rows.events);
  return rows;
}

namespace {

// Keyed as postgres keys it: the same table code, line and game.
template <typename Row, typename Serialize>
bool Known(const std::vector<Row>& kept, const Row& archived, Serialize serialize) {
  const std::string serialized = serialize(archived.game);
  return std::any_of(kept.begin(), kept.end(), [&](const Row& row) {
    return row.game_id == archived.game_id && row.ordinal == archived.ordinal &&
           serialize(row.game) == serialized;
  });
}

}  // namespace

bool MemoryHubStore::CommitGameLocked(const GameRow& row) {
  if (!rooms_.contains(row.room_id)) return false;
  const Key key{row.room_id, row.game_id};
  const auto game = games_.find(key);
  if (row.version == 1) {
    if (game != games_.end()) return false;
  } else if (game == games_.end() || game->second.version != row.version - 1) {
    return false;
  }
  GameRow stored = row;
  if (game != games_.end()) stored.event = game->second.event;
  games_.erase(key);
  games_.emplace(key, stored);
  auto archived = ArchivedGameOf(stored);
  if (!archived.has_value()) return true;
  RoomData& room = rooms_.at(row.room_id);
  if (auto* chess = std::get_if<ChessGameRow>(&*archived)) {
    if (Known(room.chess_games, *chess, chess_play::serializeGameState)) return true;
    chess->archive_id = ++last_archive_id_;
    chess->ended_at_ms = now_ms_();
    chess->published = room.chess_published;
    if (chess->published)
      published_.push_back({chess->archive_id, chess->game, chess->ended_at_ms});
    if (stored.event.has_value() && chess->ordinal == 1) {
      room.event_games.push_back(
          {stored.event->event_id,
           {chess->archive_id, row.game_id, stored.event->pairing, ChessWinner(chess->game)}});
    }
    room.chess_games.push_back(std::move(*chess));
  } else {
    // A round robin scores go off its tables (TableGame), and go has no
    // feed: the archive is its history alone.
    auto& go = std::get<GoGameRow>(*archived);
    if (Known(room.go_games, go, go::serializeGameState)) return true;
    go.archive_id = ++last_archive_id_;
    go.ended_at_ms = now_ms_();
    room.go_games.push_back(std::move(go));
  }
  return true;
}

absl::StatusOr<bool> MemoryHubStore::CommitChessEvent(const ChessEventRow& row,
                                                      const std::string& /*notify_payload*/) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto room = rooms_.find(row.room_id);
  if (room == rooms_.end()) return false;
  auto& events = room->second.events;
  const auto event = events.find(row.event_id);
  if (row.version == 1) {
    if (event != events.end()) return false;
  } else if (event == events.end() || event->second.version != row.version - 1) {
    return false;
  }
  ChessEventRow stored = row;
  stored.games.clear();
  events.insert_or_assign(row.event_id, std::move(stored));
  return true;
}

std::vector<HubStore::ChessEventRow> MemoryHubStore::EventsLocked(const RoomData& room) const {
  std::vector<ChessEventRow> events;
  for (const auto& [event_id, event] : room.events) {
    ChessEventRow loaded = event;
    for (const auto& [tagged, game] : room.event_games) {
      if (tagged == event_id) loaded.games.push_back(game);
    }
    events.push_back(std::move(loaded));
  }
  return events;
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

absl::StatusOr<std::vector<HubStore::GoGameRow>> MemoryHubStore::LoadGoHistory(
    const std::string& room_id, int limit) {
  const std::lock_guard<std::mutex> lock(mu_);
  const auto room = rooms_.find(room_id);
  if (room == rooms_.end()) return absl::NotFoundError("no such room");
  const auto& games = room->second.go_games;
  std::vector<GoGameRow> history;
  for (auto game = games.rbegin(); game != games.rend() && std::ssize(history) < limit; ++game) {
    history.push_back(*game);
  }
  return history;
}

}  // namespace games_hub
