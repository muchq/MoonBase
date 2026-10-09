#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_HUB_STORE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_HUB_STORE_H

#include <chrono>
#include <cstdint>
#include <functional>
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
#include "domains/games/libs/chess_play/game_state.h"

namespace games_hub {

inline constexpr char kRoomsChannel[] = "golf_rooms";
inline std::string RoomChannel(const std::string& room_id) { return "room_" + room_id; }
/// The payload of the wake a sweep sends a deleted room's channel. It is
/// no instance's id, so every instance holding the room acts on it.
inline constexpr char kSweepWake[] = "sweep";

/// A chess challenge's terms (#1633): the setup and clock a waiting
/// table starts on once its second seat fills.
struct ChessTerms {
  std::string setup_id;
  chess_play::TimeControl time_control;
  bool operator==(const ChessTerms&) const = default;
};

/// The hub's authoritative room, member, and game persistence contract.
/// Implementations serialize conditional game commits and retain terminal
/// rows until their room is deleted so another instance can finish its
/// ceremony after a delayed wake.
///
/// A room keeps its finished chess games (#1637): a commit that lands a
/// chess table whose game is over archives that game in the same step,
/// under an id of its own, with when it ended and whether the room was
/// published then. Later commits still carrying the ended game add
/// nothing. The archive dies with its room; a game archived published is
/// also copied, room unnamed, to the public feed, which outlives the room
/// until swept.
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
    /// A waiting chess table's posted challenge; dropped once started.
    std::optional<ChessTerms> terms = std::nullopt;
  };

  /// A room and the surface it chose at creation (#1554).
  struct RoomRow {
    std::string room_id;
    Surface surface;
    /// Whether the room publishes its chess games (#1637).
    bool chess_published = false;
  };

  struct Snapshot {
    std::vector<RoomRow> rooms;
    std::vector<MemberRow> members;
    std::vector<GameRow> games;
  };

  /// Creates the room on its surface; an upsert of a room that exists
  /// changes nothing (two instances minting one code keep the first).
  /// A room that chose nothing is a plane.
  struct UpsertRoom {
    std::string room_id;
    Surface surface = Surface::Plane();
  };
  /// Changes an existing room's surface; nothing for a room that is not.
  struct SetRoomSurface {
    std::string room_id;
    Surface surface;
  };
  /// Publishes a room's chess games, or stops; nothing for a room that
  /// is not.
  struct SetChessPublished {
    std::string room_id;
    bool published = false;
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
  /// Vouches that these rooms are still held by a live instance: the
  /// heartbeat's write, which keeps them out of the room sweep. A room
  /// named here that no longer exists is not an error.
  struct TouchRooms {
    std::vector<std::string> room_ids;
  };
  /// Deletes every room no instance has stamped for `older_than`, and
  /// wakes each one's channel with kSweepWake so its holders drop it.
  struct SweepRooms {
    std::chrono::seconds older_than;
  };
  /// Deletes every published chess game that ended `older_than` ago.
  struct SweepPublishedChess {
    std::chrono::seconds older_than;
  };
  using Op =
      std::variant<UpsertRoom, SetRoomSurface, SetChessPublished, DeleteRoom, UpsertMember,
                   DeleteMember, DeleteGame, Notify, TouchRooms, SweepRooms, SweepPublishedChess>;

  struct StatsDelta {
    std::string player_id;
    int played = 0;
    int won = 0;
    int score = 0;
  };

  struct RoomRows {
    bool exists = false;
    Surface surface;
    bool chess_published = false;
    std::vector<MemberRow> members;
    std::vector<GameRow> games;
  };

  /// One finished chess game in its room's archive.
  struct ChessGameRow {
    /// The game's identity, increasing in the order games were archived;
    /// the feed's cursor. A table code alone is not one: codes are minted
    /// again once their table is gone.
    int64_t archive_id = 0;
    std::string game_id;
    /// The game's line on its table's score sheet, from 1.
    int ordinal = 0;
    chess_play::GameState game;
    int64_t ended_at_ms = 0;
    /// The room was published when it ended.
    bool published = false;
  };

  struct ChessHistory {
    /// Whether the room publishes now.
    bool published = false;
    /// Newest first.
    std::vector<ChessGameRow> games;
  };

  /// Names one archived game in a room: by its id, or by its table code
  /// and line, which names that table's newest such game.
  struct ChessGameKey {
    std::optional<int64_t> archive_id;
    std::string game_id;
    int ordinal = 0;
  };

  /// A published game in the public feed: no room, no table code.
  struct PublishedChessGame {
    int64_t archive_id = 0;
    chess_play::GameState game;
    int64_t ended_at_ms = 0;
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
  /// The room's newest `limit` finished chess games, and whether it
  /// publishes; NotFound for a room that is not.
  virtual absl::StatusOr<ChessHistory> LoadChessHistory(const std::string& room_id, int limit) = 0;
  /// One archived game of the room; absent when the room has none such.
  virtual absl::StatusOr<std::optional<ChessGameRow>> LoadChessGame(const std::string& room_id,
                                                                    const ChessGameKey& key) = 0;
  /// The public feed past `after_archive_id`, in archive order, at most
  /// `limit`.
  virtual absl::StatusOr<std::vector<PublishedChessGame>> LoadPublishedChess(
      int64_t after_archive_id, int limit) = 0;
};

/// The chess game a committed row archives, and its ordinal: a chess
/// table's game once it is over. Absent for anything else. Its id and
/// stamps are the store's to give.
std::optional<HubStore::ChessGameRow> ArchivedChessGame(const HubStore::GameRow& row);

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
  };

  bool CommitGameLocked(const GameRow& row);
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

#endif
