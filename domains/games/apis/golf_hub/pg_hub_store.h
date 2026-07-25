#ifndef DOMAINS_GAMES_APIS_GOLF_HUB_PG_HUB_STORE_H
#define DOMAINS_GAMES_APIS_GOLF_HUB_PG_HUB_STORE_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "absl/status/statusor.h"
#include "domains/platform/libs/pg/pg.h"

namespace golf_hub {

/// The hub's rooms/members/games write-through (#1194 step 2). Not an
/// interface — the issue's design note rejects a store seam ahead of the
/// backend; this is the concrete postgres component, injected optionally
/// (nullptr keeps the all-in-memory hub, which stays the test mode).
///
/// Memory is authoritative in step 2: the hub stages ops under its lock
/// (so staging order is the truth's order) and a single writer thread
/// applies them FIFO — no DB latency inside the hub's lock, and write
/// failures degrade durability, never gameplay. Game saves are
/// conditional on version-1; a miss means the DB diverged from the only
/// writer, which is logged loudly and repaired last-writer-wins. Step 3
/// flips authority to the database and this component grows into the
/// issue's Apply(game_id, transition) repository.
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
  /// state_json empty = not started (NULL in the column).
  struct GameRow {
    std::string room_id;
    std::string game_id;
    std::vector<std::string> roster;
    std::string state_json;
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

  /// Appends a batch atomically; batches apply in enqueue order.
  void Enqueue(std::vector<Op> ops);
  /// Blocks until everything enqueued so far has been applied.
  void Flush();

  /// The boot-time restore read. Synchronous; call before serving.
  absl::StatusOr<Snapshot> LoadSnapshot();

 private:
  void WriterLoop();
  void Apply(const Op& op);

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
