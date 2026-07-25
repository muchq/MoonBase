#include "domains/games/apis/golf_hub/pg_hub_store.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "domains/games/libs/cards/golf/game_state_serde.h"

namespace golf_hub {
namespace {

using nlohmann::json;

constexpr char kUpsertRoom[] = R"sql(
    INSERT INTO rooms (room_id) VALUES ($1)
    ON CONFLICT (room_id) DO NOTHING)sql";
constexpr char kDeleteRoom[] = "DELETE FROM rooms WHERE room_id = $1";
constexpr char kUpsertMember[] = R"sql(
    INSERT INTO room_members (room_id, player_id, connected, games_played, games_won, total_score)
    VALUES ($1, $2, $3::boolean, $4::integer, $5::integer, $6::integer)
    ON CONFLICT (room_id, player_id) DO UPDATE
      SET connected = EXCLUDED.connected, games_played = EXCLUDED.games_played,
          games_won = EXCLUDED.games_won, total_score = EXCLUDED.total_score)sql";
constexpr char kDeleteMember[] = "DELETE FROM room_members WHERE room_id = $1 AND player_id = $2";
// The conditional save: an UPDATE lands only when the row holds the
// previous version, and the RETURNING is load-bearing — rows() is the
// divergence probe. kRepairGame is the same write unconditioned; the
// pair stays duplicated because sharing the prefix would cost the
// RETURNING or need string assembly.
constexpr char kSaveGame[] = R"sql(
    INSERT INTO games (room_id, game_id, roster, state, version)
    VALUES ($1, $2, $3::jsonb, NULLIF($4, '')::jsonb, $5::bigint)
    ON CONFLICT (room_id, game_id) DO UPDATE
      SET roster = EXCLUDED.roster, state = EXCLUDED.state, version = EXCLUDED.version
      WHERE games.version = EXCLUDED.version - 1
    RETURNING version)sql";
constexpr char kRepairGame[] = R"sql(
    INSERT INTO games (room_id, game_id, roster, state, version)
    VALUES ($1, $2, $3::jsonb, NULLIF($4, '')::jsonb, $5::bigint)
    ON CONFLICT (room_id, game_id) DO UPDATE
      SET roster = EXCLUDED.roster, state = EXCLUDED.state, version = EXCLUDED.version)sql";
constexpr char kDeleteGame[] = "DELETE FROM games WHERE room_id = $1 AND game_id = $2";

std::string RosterJson(const std::vector<std::string>& roster) {
  json names = json::array();
  for (const std::string& player_id : roster) names.push_back(player_id);
  return names.dump();
}

}  // namespace

PgHubStore::PgHubStore(std::shared_ptr<pg::Client> db)
    : db_(std::move(db)), writer_([this] { WriterLoop(); }) {}

PgHubStore::~PgHubStore() {
  {
    const std::lock_guard<std::mutex> lock(mu_);
    stopping_ = true;
  }
  cv_.notify_all();
  writer_.join();
}

void PgHubStore::Enqueue(std::vector<Op> ops) {
  if (ops.empty()) return;
  {
    const std::lock_guard<std::mutex> lock(mu_);
    for (Op& op : ops) queue_.push_back(std::move(op));
  }
  cv_.notify_all();
}

void PgHubStore::Flush() {
  std::unique_lock<std::mutex> lock(mu_);
  cv_.wait(lock, [this] { return queue_.empty() && !busy_; });
}

void PgHubStore::WriterLoop() {
  std::unique_lock<std::mutex> lock(mu_);
  while (true) {
    cv_.wait(lock, [this] { return !queue_.empty() || stopping_; });
    // Drain before stopping: shutdown must not drop staged writes.
    if (queue_.empty() && stopping_) return;
    const Op op = std::move(queue_.front());
    queue_.pop_front();
    busy_ = true;
    lock.unlock();
    Apply(op);
    lock.lock();
    busy_ = false;
    cv_.notify_all();
  }
}

std::optional<int> PgHubStore::ExecOrWarn(const char* what, const char* sql,
                                          const std::vector<std::string>& params) {
  auto result = db_->Exec(sql, params);
  if (!result.ok()) {
    LOG(WARNING) << "hub write-through " << what << " failed: " << result.status();
    return std::nullopt;
  }
  return result->rows();
}

void PgHubStore::Apply(const Op& op) {
  if (const auto* upsert = std::get_if<UpsertRoom>(&op)) {
    ExecOrWarn("UpsertRoom", kUpsertRoom, {upsert->room_id});
  } else if (const auto* erase = std::get_if<DeleteRoom>(&op)) {
    ExecOrWarn("DeleteRoom", kDeleteRoom, {erase->room_id});
  } else if (const auto* upsert = std::get_if<UpsertMember>(&op)) {
    const MemberRow& row = upsert->row;
    ExecOrWarn("UpsertMember", kUpsertMember,
               {row.room_id, row.player_id, row.connected ? "true" : "false",
                std::to_string(row.games_played), std::to_string(row.games_won),
                std::to_string(row.total_score)});
  } else if (const auto* erase = std::get_if<DeleteMember>(&op)) {
    ExecOrWarn("DeleteMember", kDeleteMember, {erase->room_id, erase->player_id});
  } else if (const auto* save = std::get_if<SaveGame>(&op)) {
    const GameRow& row = save->row;
    const std::vector<std::string> params = {
        row.room_id, row.game_id, RosterJson(row.roster),
        row.state.has_value() ? golf::serializeGameState(*row.state) : "",
        std::to_string(row.version)};
    // nullopt != 0, so a failed Exec skips the repair.
    if (ExecOrWarn("SaveGame", kSaveGame, params) == 0) {
      // Single-writer invariant broken (or a retry raced its own first
      // execution). Loud, then last-writer-wins: memory is the truth.
      LOG(ERROR) << "game " << row.room_id << "/" << row.game_id << " version " << row.version
                 << " did not follow the stored row; repairing";
      ExecOrWarn("RepairGame", kRepairGame, params);
    }
  } else if (const auto* erase = std::get_if<DeleteGame>(&op)) {
    ExecOrWarn("DeleteGame", kDeleteGame, {erase->room_id, erase->game_id});
  }
}

absl::StatusOr<PgHubStore::Snapshot> PgHubStore::LoadSnapshot() {
  Snapshot snapshot;
  auto rooms = db_->Exec("SELECT room_id FROM rooms");
  if (!rooms.ok()) return rooms.status();
  for (int i = 0; i < rooms->rows(); ++i) {
    snapshot.rooms.push_back(rooms->Get(i, 0).value_or(""));
  }

  auto members = db_->Exec(
      "SELECT room_id, player_id, connected, games_played, games_won, total_score"
      " FROM room_members");
  if (!members.ok()) return members.status();
  for (int i = 0; i < members->rows(); ++i) {
    MemberRow row;
    row.room_id = members->Get(i, 0).value_or("");
    row.player_id = members->Get(i, 1).value_or("");
    row.connected = members->Get(i, 2).value_or("f") == "t";
    row.games_played = std::atoi(members->Get(i, 3).value_or("0").c_str());
    row.games_won = std::atoi(members->Get(i, 4).value_or("0").c_str());
    row.total_score = std::atoi(members->Get(i, 5).value_or("0").c_str());
    snapshot.members.push_back(std::move(row));
  }

  auto games = db_->Exec(
      "SELECT room_id, game_id, roster::text, COALESCE(state::text, ''), version FROM games");
  if (!games.ok()) return games.status();
  for (int i = 0; i < games->rows(); ++i) {
    GameRow row;
    row.room_id = games->Get(i, 0).value_or("");
    row.game_id = games->Get(i, 1).value_or("");
    row.version = std::atoll(games->Get(i, 4).value_or("0").c_str());

    // Undecodable rows cost their game, not the boot — the same blast
    // radius whichever column is bad.
    const json roster = json::parse(games->Get(i, 2).value_or("[]"), /*cb=*/nullptr,
                                    /*allow_exceptions=*/false);
    bool roster_ok = roster.is_array();
    if (roster_ok) {
      for (const json& entry : roster) {
        if (!entry.is_string()) {
          roster_ok = false;
          break;
        }
        row.roster.push_back(entry.get<std::string>());
      }
    }
    if (!roster_ok) {
      LOG(ERROR) << "dropping game " << row.room_id << "/" << row.game_id
                 << ": roster is not an array of player ids";
      continue;
    }
    const std::string state_json = games->Get(i, 3).value_or("");
    if (!state_json.empty()) {
      auto state = golf::deserializeGameState(state_json);
      if (!state.ok()) {
        LOG(ERROR) << "dropping game " << row.room_id << "/" << row.game_id
                   << ": unreadable state: " << state.status();
        continue;
      }
      row.state.emplace(*std::move(state));
    }
    snapshot.games.push_back(std::move(row));
  }
  return snapshot;
}

}  // namespace golf_hub
