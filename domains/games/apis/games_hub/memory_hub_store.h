#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_MEMORY_HUB_STORE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_MEMORY_HUB_STORE_H

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/games/apis/games_hub/surface.h"

namespace games_hub {

/// Process-local production storage. Operations are synchronous, but use
/// the same conditional commit and terminal-row semantics as PostgreSQL.
class MemoryHubStore final : public HubStore {
 public:
  /// `now_ms` stamps when an archived game ended; the wall clock unless a
  /// test fixes it.
  explicit MemoryHubStore(std::function<int64_t()> now_ms = nullptr);

  void Enqueue(std::vector<Op> ops) override;
  void Flush() override {}
  absl::StatusOr<Snapshot> LoadSnapshot() override;
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override;
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& stats,
                                        const std::string& notify_payload) override;
  absl::StatusOr<bool> CommitChessEvent(const ChessEventRow& row,
                                        const std::string& notify_payload) override;
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override;
  absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) override;
  absl::StatusOr<ChessHistory> LoadChessHistory(const std::string& room_id, int limit) override;
  absl::StatusOr<std::optional<ChessGameRow>> LoadChessGame(const std::string& room_id,
                                                            const ChessGameKey& key) override;
  absl::StatusOr<std::vector<PublishedChessGame>> LoadPublishedChess(int64_t after_archive_id,
                                                                     int limit) override;

 private:
  using Key = std::pair<std::string, std::string>;

  struct RoomData {
    Surface surface;
    bool chess_published = false;
    /// Oldest first.
    std::vector<ChessGameRow> chess_games;
    /// By event id.
    std::map<std::string, ChessEventRow> events = {};
    /// Each with its event's id, in archive order.
    std::vector<std::pair<std::string, EventGame>> event_games = {};
  };

  bool CommitGameLocked(const GameRow& row);
  /// The room's events, each with its archived games.
  std::vector<ChessEventRow> EventsLocked(const RoomData& room) const;
  void ApplyLocked(const Op& op);

  const std::function<int64_t()> now_ms_;
  std::mutex mu_;
  int64_t last_archive_id_ = 0;
  std::map<std::string, RoomData> rooms_;
  /// In archive order.
  std::vector<PublishedChessGame> published_;
  std::map<Key, MemberRow> members_;
  std::map<Key, GameRow> games_;
};

}  // namespace games_hub

#endif  // DOMAINS_GAMES_APIS_GAMES_HUB_MEMORY_HUB_STORE_H
