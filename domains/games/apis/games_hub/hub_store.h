#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HUB_STORE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HUB_STORE_H

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/apis/games_hub/surface.h"

namespace games_hub {

inline constexpr char kRoomsChannel[] = "golf_rooms";
inline std::string RoomChannel(const std::string& room_id) { return "room_" + room_id; }

/// The hub's authoritative room, member, and game persistence contract.
/// Implementations serialize conditional game commits and retain terminal
/// rows until their room is deleted so another instance can finish its
/// ceremony after a delayed wake.
class HubStore {
 public:
  struct MemberRow {
    std::string room_id;
    std::string player_id;
    bool connected = false;
    int games_played = 0;
    int games_won = 0;
    int total_score = 0;
  };

  struct GameRow {
    std::string room_id;
    std::string game_id;
    std::vector<std::string> roster;
    std::optional<HostedState> state;
    int64_t version = 0;
    /// Which game the table plays, fixed at creation; `state` is that
    /// engine's once started.
    GameKind kind = GameKind::kGolf;
  };

  /// A room and the surface it chose at creation (#1554).
  struct RoomRow {
    std::string room_id;
    Surface surface;
  };

  struct Snapshot {
    std::vector<RoomRow> rooms;
    std::vector<MemberRow> members;
    std::vector<GameRow> games;
  };

  /// The first upsert of a room fixes its surface; a later one for the
  /// same room changes nothing. A room that chose nothing is a plane.
  struct UpsertRoom {
    std::string room_id;
    Surface surface = Surface::Plane();
  };
  struct DeleteRoom {
    std::string room_id;
  };
  struct UpsertMember {
    MemberRow row;
  };
  struct DeleteMember {
    std::string room_id;
    std::string player_id;
  };
  struct DeleteGame {
    std::string room_id;
    std::string game_id;
  };
  struct Notify {
    std::string channel;
    std::string payload;
  };
  using Op = std::variant<UpsertRoom, DeleteRoom, UpsertMember, DeleteMember, DeleteGame, Notify>;

  struct StatsDelta {
    std::string player_id;
    int played = 0;
    int won = 0;
    int score = 0;
  };

  struct RoomRows {
    bool exists = false;
    Surface surface;
    std::vector<MemberRow> members;
    std::vector<GameRow> games;
  };

  virtual ~HubStore() = default;

  virtual void Enqueue(std::vector<Op> ops) = 0;
  virtual void Flush() = 0;
  virtual absl::StatusOr<Snapshot> LoadSnapshot() = 0;
  virtual absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                              const std::string& notify_payload) = 0;
  virtual absl::StatusOr<bool> CommitGameFinish(const GameRow& row,
                                                const std::vector<StatsDelta>& stats,
                                                const std::string& notify_payload) = 0;
  virtual absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                          const std::string& game_id) = 0;
  virtual absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) = 0;
};

/// Process-local production storage. Operations are synchronous, but use
/// the same conditional commit and terminal-row semantics as PostgreSQL.
class MemoryHubStore final : public HubStore {
 public:
  void Enqueue(std::vector<Op> ops) override;
  void Flush() override {}
  absl::StatusOr<Snapshot> LoadSnapshot() override;
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override;
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& stats,
                                        const std::string& notify_payload) override;
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override;
  absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) override;

 private:
  using Key = std::pair<std::string, std::string>;

  bool CommitGameLocked(const GameRow& row);
  void ApplyLocked(const Op& op);

  std::mutex mu_;
  std::map<std::string, Surface> rooms_;
  std::map<Key, MemberRow> members_;
  std::map<Key, GameRow> games_;
};

}  // namespace games_hub

#endif
