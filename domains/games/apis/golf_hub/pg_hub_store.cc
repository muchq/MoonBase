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
constexpr char kDeleteGame[] = "DELETE FROM games WHERE room_id = $1 AND game_id = $2";

// The step-3 commit statements: CTE-chained so the conditional write and
// its NOTIFY are one atomic statement — the notify fires exactly when
// the save lands, and a retried statement (pg::Client may run one twice
// after a reconnect) misses the condition the second time, so nothing
// double-fires. rows() of the outer SELECT is the landed/missed probe.
constexpr char kCommitInsert[] = R"sql(
    WITH save AS (
      INSERT INTO games (room_id, game_id, roster, state, version)
      VALUES ($1, $2, $3::jsonb, NULLIF($4, '')::jsonb, $5::bigint)
      ON CONFLICT (room_id, game_id) DO NOTHING
      RETURNING version)
    SELECT pg_notify($6, $7) FROM save)sql";
constexpr char kCommitUpdate[] = R"sql(
    WITH save AS (
      UPDATE games
      SET roster = $3::jsonb, state = NULLIF($4, '')::jsonb, version = $5::bigint
      WHERE room_id = $1 AND game_id = $2 AND version = $5::bigint - 1
      RETURNING version)
    SELECT pg_notify($6, $7) FROM save)sql";
// The finishing commit adds the stat deltas, guarded on the save landing
// so a conflicted (or retried) finish applies them zero times, not
// twice. Postgres runs every data-modifying CTE exactly once whether or
// not it is read, so the guard must live in the WHERE.
constexpr char kCommitFinish[] = R"sql(
    WITH save AS (
      UPDATE games
      SET roster = $3::jsonb, state = NULLIF($4, '')::jsonb, version = $5::bigint
      WHERE room_id = $1 AND game_id = $2 AND version = $5::bigint - 1
      RETURNING version),
    stats AS (
      UPDATE room_members m
      SET games_played = m.games_played + s.played,
          games_won = m.games_won + s.won,
          total_score = m.total_score + s.score
      FROM jsonb_to_recordset($6::jsonb) AS s(player_id text, played int, won int, score int)
      WHERE m.room_id = $1 AND m.player_id = s.player_id
        AND EXISTS (SELECT 1 FROM save))
    SELECT pg_notify($7, $8) FROM save)sql";

std::string RosterJson(const std::vector<std::string>& roster) {
  json names = json::array();
  for (const std::string& player_id : roster) names.push_back(player_id);
  return names.dump();
}

std::string StatsJson(const std::vector<PgHubStore::StatsDelta>& stats) {
  json rows = json::array();
  for (const PgHubStore::StatsDelta& delta : stats) {
    rows.push_back({{"player_id", delta.player_id},
                    {"played", delta.played},
                    {"won", delta.won},
                    {"score", delta.score}});
  }
  return rows.dump();
}

std::string StateJson(const PgHubStore::GameRow& row) {
  return row.state.has_value() ? golf::serializeGameState(*row.state) : "";
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
  } else if (const auto* erase = std::get_if<DeleteGame>(&op)) {
    ExecOrWarn("DeleteGame", kDeleteGame, {erase->room_id, erase->game_id});
  } else if (const auto* notify = std::get_if<Notify>(&op)) {
    ExecOrWarn("Notify", "SELECT pg_notify($1, $2)", {notify->channel, notify->payload});
  }
}

absl::StatusOr<bool> PgHubStore::CommitGameSave(const GameRow& row,
                                                const std::string& notify_payload) {
  auto result = db_->Exec(row.version == 1 ? kCommitInsert : kCommitUpdate,
                          {row.room_id, row.game_id, RosterJson(row.roster), StateJson(row),
                           std::to_string(row.version), RoomChannel(row.room_id), notify_payload});
  if (!result.ok()) return result.status();
  return result->rows() == 1;
}

absl::StatusOr<bool> PgHubStore::CommitGameFinish(const GameRow& row,
                                                  const std::vector<StatsDelta>& stats,
                                                  const std::string& notify_payload) {
  auto result =
      db_->Exec(kCommitFinish, {row.room_id, row.game_id, RosterJson(row.roster), StateJson(row),
                                std::to_string(row.version), StatsJson(stats),
                                RoomChannel(row.room_id), notify_payload});
  if (!result.ok()) return result.status();
  return result->rows() == 1;
}

absl::StatusOr<std::optional<PgHubStore::GameRow>> PgHubStore::LoadGame(
    const std::string& room_id, const std::string& game_id) {
  auto result = db_->Exec(
      "SELECT roster::text, COALESCE(state::text, ''), version FROM games"
      " WHERE room_id = $1 AND game_id = $2",
      {room_id, game_id});
  if (!result.ok()) return result.status();
  if (result->rows() == 0) return std::nullopt;
  auto row = RowFromColumns(room_id, game_id, result->Get(0, 0).value_or("[]"),
                            result->Get(0, 1).value_or(""),
                            std::atoll(result->Get(0, 2).value_or("0").c_str()));
  if (!row.ok()) {
    LOG(ERROR) << "game " << room_id << "/" << game_id
               << " unreadable, treating as gone: " << row.status();
    return std::nullopt;
  }
  return std::optional<GameRow>(*std::move(row));
}

absl::StatusOr<PgHubStore::RoomRows> PgHubStore::LoadRoom(const std::string& room_id) {
  RoomRows out;
  auto room = db_->Exec("SELECT 1 FROM rooms WHERE room_id = $1", {room_id});
  if (!room.ok()) return room.status();
  out.exists = room->rows() > 0;
  if (!out.exists) return out;

  auto members = db_->Exec(
      "SELECT player_id, connected, games_played, games_won, total_score"
      " FROM room_members WHERE room_id = $1",
      {room_id});
  if (!members.ok()) return members.status();
  for (int i = 0; i < members->rows(); ++i) {
    MemberRow row;
    row.room_id = room_id;
    row.player_id = members->Get(i, 0).value_or("");
    row.connected = members->Get(i, 1).value_or("f") == "t";
    row.games_played = std::atoi(members->Get(i, 2).value_or("0").c_str());
    row.games_won = std::atoi(members->Get(i, 3).value_or("0").c_str());
    row.total_score = std::atoi(members->Get(i, 4).value_or("0").c_str());
    out.members.push_back(std::move(row));
  }

  auto games = db_->Exec(
      "SELECT game_id, roster::text, COALESCE(state::text, ''), version FROM games"
      " WHERE room_id = $1",
      {room_id});
  if (!games.ok()) return games.status();
  for (int i = 0; i < games->rows(); ++i) {
    const std::string game_id = games->Get(i, 0).value_or("");
    auto row = RowFromColumns(room_id, game_id, games->Get(i, 1).value_or("[]"),
                              games->Get(i, 2).value_or(""),
                              std::atoll(games->Get(i, 3).value_or("0").c_str()));
    if (!row.ok()) {
      LOG(ERROR) << "dropping game " << room_id << "/" << game_id << ": " << row.status();
      continue;
    }
    out.games.push_back(*std::move(row));
  }
  return out;
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
    const std::string room_id = games->Get(i, 0).value_or("");
    const std::string game_id = games->Get(i, 1).value_or("");
    // Undecodable rows cost their game, not the boot — the same blast
    // radius whichever column is bad.
    auto row = RowFromColumns(room_id, game_id, games->Get(i, 2).value_or("[]"),
                              games->Get(i, 3).value_or(""),
                              std::atoll(games->Get(i, 4).value_or("0").c_str()));
    if (!row.ok()) {
      LOG(ERROR) << "dropping game " << room_id << "/" << game_id << ": " << row.status();
      continue;
    }
    snapshot.games.push_back(*std::move(row));
  }
  return snapshot;
}

absl::StatusOr<PgHubStore::GameRow> PgHubStore::RowFromColumns(const std::string& room_id,
                                                               const std::string& game_id,
                                                               const std::string& roster_json,
                                                               const std::string& state_json,
                                                               int64_t version) {
  GameRow row;
  row.room_id = room_id;
  row.game_id = game_id;
  row.version = version;
  const json roster = json::parse(roster_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (!roster.is_array()) return absl::DataLossError("roster is not an array of player ids");
  for (const json& entry : roster) {
    if (!entry.is_string()) return absl::DataLossError("roster is not an array of player ids");
    row.roster.push_back(entry.get<std::string>());
  }
  if (!state_json.empty()) {
    auto state = golf::deserializeGameState(state_json);
    if (!state.ok()) return state.status();
    row.state.emplace(*std::move(state));
  }
  return row;
}

}  // namespace golf_hub
