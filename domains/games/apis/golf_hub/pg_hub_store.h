#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_PG_HUB_STORE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_PG_HUB_STORE_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/games/libs/cards/golf/game_state.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

/// The hub's rooms/members/games write-through (#1194 step 2): a durable-
/// write outbox, not a repository — the load→transition→commit loop the
/// issue's Apply(game_id, transition) seam will own still lives in the
/// hub, and gets carved out (with this queue behind it) when step 3 flips
/// authority to the database. Not an interface either — the issue's
/// design note rejects a store seam ahead of the backend; this is the
/// concrete postgres component, injected optionally (nullptr keeps the
/// all-in-memory hub, which stays the test mode).
///
/// Memory is authoritative in step 2. The hub stages ops and hands them
/// over while still holding its lock — so queue order is the truth's
/// order — and a single writer thread applies them FIFO, one statement
/// each: no DB latency inside the hub's lock, no transactions (a batch
/// can land partially), and a failed write degrades durability, never
/// gameplay. This component owns the row encodings end to end: game
/// state serializes through the step-0 serde on the writer thread and
/// deserializes in LoadSnapshot. Game saves are conditional on
/// version-1; a miss means the DB diverged from the only writer, which
/// is logged loudly and repaired last-writer-wins.
class PgHubStore {
 public:
  struct MemberRow {
    std::string room_id;
    std::string player_id;
    bool connected = false;
    int games_played = 0;
    int games_won = 0;
    int total_score = 0;
  };
  /// state disengaged = not started (NULL in the column).
  struct GameRow {
    std::string room_id;
    std::string game_id;
    std::vector<std::string> roster;
    std::optional<golf::GameState> state;
    int64_t version = 0;
  };
  struct Snapshot {
    std::vector<std::string> rooms;
    std::vector<MemberRow> members;
    std::vector<GameRow> games;
  };

  struct UpsertRoom {
    std::string room_id;
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
  struct SaveGame {
    GameRow row;
  };
  struct DeleteGame {
    std::string room_id;
    std::string game_id;
  };
  using Op = std::variant<UpsertRoom, DeleteRoom, UpsertMember, DeleteMember, SaveGame, DeleteGame>;

  explicit PgHubStore(std::shared_ptr<pg::Client> db);
  /// Drains the queue, then joins the writer.
  ~PgHubStore();
  PgHubStore(const PgHubStore&) = delete;
  PgHubStore& operator=(const PgHubStore&) = delete;

  /// Appends ops to the writer's queue under one lock acquisition; the
  /// writer applies them FIFO. Not a transaction.
  void Enqueue(std::vector<Op> ops);
  /// Blocks until everything enqueued so far has been applied.
  void Flush();

  /// The boot-time restore read. Synchronous; call before serving. A row
  /// whose stored bytes don't decode is dropped with a loud log — one
  /// bad game costs that game, not the boot.
  absl::StatusOr<Snapshot> LoadSnapshot();

 private:
  void WriterLoop();
  void Apply(const Op& op);
  /// Runs one statement; returns its row count, or nullopt after a
  /// logged failure (memory stays authoritative — degraded durability,
  /// not a gameplay error).
  std::optional<int> ExecOrWarn(const char* what, const char* sql,
                                const std::vector<std::string>& params);

  const std::shared_ptr<pg::Client> db_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Op> queue_;
  bool busy_ = false;
  bool stopping_ = false;
  std::thread writer_;
};

}  // namespace golf_hub

#endif
