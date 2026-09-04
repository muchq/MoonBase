#ifndef DOMAINS_GAMES_APIS_GAMES_HUB_PG_HUB_STORE_H
#define DOMAINS_GAMES_APIS_GAMES_HUB_PG_HUB_STORE_H

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
#include "domains/games/apis/games_hub/hub_store.h"
#include "domains/platform/libs/pg/pg.h"

namespace games_hub {

/// The hub's postgres component (#1194 steps 2-3), with two write paths
/// matching what each kind of state can afford. Rooms and members ride
/// the step-2 outbox: ops staged under the hub's lock and handed over
/// while still holding it — so queue order is the truth's order — then
/// applied FIFO by one writer thread; a failed write degrades
/// durability, never gameplay (each player's rows have one writer, so
/// there is nothing to conflict with). Games are the multi-instance
/// contended state, and step 3 makes PostgreSQL their authority:
/// CommitGameSave/CommitGameFinish run synchronously, conditional on
/// version-1, with the NOTIFY riding the same statement.
///
/// This component owns the row encodings end to end: game state
/// serializes through the step-0 serde and deserializes in the loads.
class PgHubStore final : public HubStore {
 public:
  explicit PgHubStore(std::shared_ptr<pg::Client> db);
  /// Drains the queue, then joins the writer.
  ~PgHubStore() override;
  PgHubStore(const PgHubStore&) = delete;
  PgHubStore& operator=(const PgHubStore&) = delete;

  /// Appends ops to the writer's queue under one lock acquisition; the
  /// writer applies them FIFO. Not a transaction.
  void Enqueue(std::vector<Op> ops) override;
  /// Blocks until everything enqueued so far has been applied.
  void Flush() override;

  /// The boot-time restore read. Synchronous; call before serving. A row
  /// whose stored bytes don't decode is dropped with a loud log — one
  /// bad game costs that game, not the boot.
  absl::StatusOr<Snapshot> LoadSnapshot() override;

  /// The step-3 synchronous commit path (#1194: load -> pure transition
  /// -> conditional update, retry on miss; NOTIFY rides the same
  /// commit). Returns whether the conditional write landed; false means
  /// the stored row didn't hold version-1 (or, for version 1, the code
  /// is already taken) — the caller reloads via LoadGame to tell a
  /// conflict from a vanished game and rebases. row.version == 1 inserts
  /// a fresh row; anything later updates the existing one only. On
  /// success the payload lands on RoomChannel(row.room_id) in the same
  /// statement; a miss notifies nobody.
  absl::StatusOr<bool> CommitGameSave(const GameRow& row,
                                      const std::string& notify_payload) override;

  /// The finishing commit: final state, per-member stat deltas, and the
  /// notify, all in one transaction. The ended row stays (remote
  /// instances read it for the game-over ceremony) until room deletion
  /// removes it through the foreign-key cascade.
  absl::StatusOr<bool> CommitGameFinish(const GameRow& row, const std::vector<StatsDelta>& stats,
                                        const std::string& notify_payload) override;

  /// Rebase read after a conditional miss; nullopt = the game is gone.
  /// An undecodable row also reads as gone (logged loudly) — the same
  /// one-bad-row-costs-one-game policy as LoadSnapshot.
  absl::StatusOr<std::optional<GameRow>> LoadGame(const std::string& room_id,
                                                  const std::string& game_id) override;

  /// One room's rows, for notify-driven refresh and join-miss lookups.
  absl::StatusOr<RoomRows> LoadRoom(const std::string& room_id) override;

 private:
  void WriterLoop();
  void Apply(const Op& op);
  /// Runs one statement; returns its row count, or nullopt after a
  /// logged failure (memory stays authoritative — degraded durability,
  /// not a gameplay error).
  std::optional<int> ExecOrWarn(const char* what, const char* sql,
                                const std::vector<std::string>& params);
  absl::StatusOr<GameRow> RowFromColumns(const std::string& room_id, const std::string& game_id,
                                         const std::string& roster_json,
                                         const std::string& state_json, int64_t version,
                                         const std::string& game);

  const std::shared_ptr<pg::Client> db_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Op> queue_;
  bool busy_ = false;
  bool stopping_ = false;
  std::thread writer_;
};

}  // namespace games_hub

#endif
