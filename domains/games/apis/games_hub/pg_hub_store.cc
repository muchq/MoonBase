#include "domains/games/apis/games_hub/pg_hub_store.h"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <type_traits>
#include <utility>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/games_hub/hosted_game.h"
#include "domains/games/libs/cards/castle/game_state_serde.h"
#include "domains/games/libs/cards/golf/game_state_serde.h"
#include "domains/games/libs/cards/rummy/table_serde.h"
#include "domains/games/libs/chess_play/game_state_serde.h"
#include "domains/games/libs/chess_play/table_serde.h"

namespace games_hub {
namespace {

using nlohmann::json;

constexpr char kUpsertRoom[] = R"sql(
    INSERT INTO rooms (room_id, geometry) VALUES ($1, $2::jsonb)
    ON CONFLICT (room_id) DO NOTHING)sql";
constexpr char kSetRoomSurface[] = "UPDATE rooms SET geometry = $2::jsonb WHERE room_id = $1";
constexpr char kSetChessPublished[] =
    "UPDATE rooms SET chess_published = $2::boolean WHERE room_id = $1";
constexpr char kDeleteRoom[] = "DELETE FROM rooms WHERE room_id = $1";
// A member's stats are written once, when the row is made; after that
// only a finish's increments move them (CommitGameFinish). A later upsert
// is presence, from an instance whose copy of the stats may predate a
// finish another instance committed.
constexpr char kUpsertMember[] = R"sql(
    INSERT INTO room_members (room_id, player_id, connected, games_played, games_won, total_score)
    VALUES ($1, $2, $3::boolean, $4::integer, $5::integer, $6::integer)
    ON CONFLICT (room_id, player_id) DO UPDATE SET connected = EXCLUDED.connected)sql";
constexpr char kDeleteMember[] = "DELETE FROM room_members WHERE room_id = $1 AND player_id = $2";
constexpr char kDeleteGame[] = "DELETE FROM games WHERE room_id = $1 AND game_id = $2";
// Stamps a room active after a write that names it. Its own statement,
// after the write commits, so it only ever waits on the room row: folded
// into the write, it would take the room after the member or game row,
// the reverse of DeleteRoom's cascade, and the two could deadlock.
constexpr char kTouchRoom[] = "UPDATE rooms SET last_active_at = now() WHERE room_id = $1";
// The sweep. The delete and its wakes are one statement, so every room
// it takes has its holders told. $1 is the threshold in seconds, $2 the
// room-channel prefix, $3 kSweepWake.
constexpr char kSweepRooms[] = R"sql(
    WITH swept AS (
      DELETE FROM rooms
      WHERE last_active_at < now() - make_interval(secs => $1::double precision)
      RETURNING room_id)
    SELECT pg_notify($2 || room_id, $3) FROM swept)sql";
// The public feed's retention; $1 is the age in seconds.
constexpr char kSweepPublishedChess[] = R"sql(
    DELETE FROM published_chess_games
    WHERE ended_at < now() - make_interval(secs => $1::double precision))sql";
// The heartbeat's batch of the same stamp; $1 is a JSON array of ids.
constexpr char kTouchRooms[] = R"sql(
    UPDATE rooms SET last_active_at = now()
    WHERE room_id IN (SELECT jsonb_array_elements_text($1::jsonb)))sql";

// The conditional commit statements: CTE-chained so the conditional write and
// its NOTIFY are one atomic statement — the notify fires exactly when
// the save lands, and a retried statement (pg::Client may run one twice
// after a reconnect) misses the condition the second time, so nothing
// double-fires. rows() of the outer SELECT is the landed/missed probe.
constexpr char kCommitInsert[] = R"sql(
    WITH save AS (
      INSERT INTO games (room_id, game_id, roster, state, version, game, event_id, pairing)
      VALUES ($1, $2, $3::jsonb, NULLIF($4, '')::jsonb, $5::bigint, $8, NULLIF($9, ''),
              NULLIF($10, '')::integer)
      ON CONFLICT (room_id, game_id) DO NOTHING
      RETURNING version)
    SELECT pg_notify($6, $7) FROM save)sql";
constexpr char kCommitUpdate[] = R"sql(
    WITH save AS (
      UPDATE games
      SET roster = $3::jsonb, state = NULLIF($4, '')::jsonb, version = $5::bigint
      WHERE room_id = $1 AND game_id = $2 AND version = $5::bigint - 1
      RETURNING version),
    archive AS (
      INSERT INTO chess_games (room_id, game_id, ordinal, game, published, event_id, pairing,
                               winner)
      SELECT $1, $2, NULLIF($8, '')::integer, NULLIF($9, '')::jsonb, r.chess_published,
             CASE WHEN $8 = '1' THEN g.event_id END, CASE WHEN $8 = '1' THEN g.pairing END,
             NULLIF($10, '')
      FROM rooms r JOIN games g ON g.room_id = r.room_id AND g.game_id = $2
      WHERE r.room_id = $1 AND $9 <> '' AND EXISTS (SELECT 1 FROM save)
      ON CONFLICT DO NOTHING
      RETURNING archive_id, game, ended_at, published),
    feed AS (
      INSERT INTO published_chess_games (archive_id, game, ended_at)
      SELECT archive_id, game, ended_at FROM archive WHERE published)
    SELECT pg_notify($6, $7) FROM save)sql";
// An update or finish that lands a chess table whose game is over also
// archives that game (#1637), guarded on the save the way the finish's
// stats are, with the room's published flag of the moment, and copies it
// to the public feed when that flag is set; a blank game says there is
// none to archive, and a later commit still carrying the same ended game
// conflicts on the archive's unique index, so the feed is not written
// twice either. A table's first game takes the pairing from the row it
// saves, whose tag only the insert wrote (the CTEs read the statement's
// snapshot, which the save does not change); a rematch is not the
// pairing's. The winner rides along so an event's results read without
// restoring a game.
//
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
        AND EXISTS (SELECT 1 FROM save)),
    archive AS (
      INSERT INTO chess_games (room_id, game_id, ordinal, game, published, event_id, pairing,
                               winner)
      SELECT $1, $2, NULLIF($9, '')::integer, NULLIF($10, '')::jsonb, r.chess_published,
             CASE WHEN $9 = '1' THEN g.event_id END, CASE WHEN $9 = '1' THEN g.pairing END,
             NULLIF($11, '')
      FROM rooms r JOIN games g ON g.room_id = r.room_id AND g.game_id = $2
      WHERE r.room_id = $1 AND $10 <> '' AND EXISTS (SELECT 1 FROM save)
      ON CONFLICT DO NOTHING
      RETURNING archive_id, game, ended_at, published),
    feed AS (
      INSERT INTO published_chess_games (archive_id, game, ended_at)
      SELECT archive_id, game, ended_at FROM archive WHERE published)
    SELECT pg_notify($7, $8) FROM save)sql";

// A round robin's commit (#1647), conditional and notifying as a games
// row's: the insert lands only into a room that is, the update only on
// version - 1.
constexpr char kCommitEventInsert[] = R"sql(
    WITH save AS (
      INSERT INTO chess_events (room_id, event_id, version, body)
      SELECT $1, $2, $3::bigint, $4::jsonb FROM rooms WHERE room_id = $1
      ON CONFLICT (room_id, event_id) DO NOTHING
      RETURNING version)
    SELECT pg_notify($5, $6) FROM save)sql";
constexpr char kCommitEventUpdate[] = R"sql(
    WITH save AS (
      UPDATE chess_events SET body = $4::jsonb, version = $3::bigint
      WHERE room_id = $1 AND event_id = $2 AND version = $3::bigint - 1
      RETURNING version)
    SELECT pg_notify($5, $6) FROM save)sql";

// An unreadable geometry costs the room its shape, not the boot: it
// reads as the plane, which is what every row before the column was.
Surface SurfaceFromColumn(const std::string& room_id, const std::string& text) {
  auto surface = SurfaceFromJson(text);
  if (surface.ok()) return *surface;
  LOG(ERROR) << "room " << room_id << " geometry unreadable, drawing it flat: " << surface.status();
  return Surface::Plane();
}

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

// A waiting chess table's challenge (#1633) rides the state column, which
// is otherwise NULL until the start: {"terms":{...}}. No engine's encoding
// has a top-level "terms".
std::string TermsJson(const ChessTerms& terms) {
  return json{{"terms",
               {{"setupId", terms.setup_id},
                {"initialMs", terms.time_control.initial_ms},
                {"incrementMs", terms.time_control.increment_ms}}}}
      .dump();
}

absl::StatusOr<std::optional<ChessTerms>> TermsFromJson(const std::string& state_json) {
  const json parsed = json::parse(state_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (!parsed.is_object() || !parsed.contains("terms")) return std::nullopt;
  const json& terms = parsed["terms"];
  if (!terms.is_object() || !terms.contains("setupId") || !terms["setupId"].is_string() ||
      !terms.contains("initialMs") || !terms["initialMs"].is_number_integer() ||
      !terms.contains("incrementMs") || !terms["incrementMs"].is_number_integer()) {
    return absl::DataLossError("chess terms are malformed");
  }
  return ChessTerms{terms["setupId"].get<std::string>(),
                    {terms["initialMs"].get<int64_t>(), terms["incrementMs"].get<int64_t>()}};
}

// The state column is encoded by whichever engine the variant holds; the
// kind column is what the loads read to pick the decoder back.
std::string StateJson(const PgHubStore::GameRow& row) {
  if (!row.state.has_value()) return row.terms.has_value() ? TermsJson(*row.terms) : "";
  return std::visit(
      [](const auto& state) -> std::string {
        if constexpr (std::is_same_v<std::decay_t<decltype(state)>, rummy::TableState>) {
          return rummy::serializeTableState(state);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(state)>, chess_play::Table>) {
          return chess_play::serializeTable(state);
        } else {
          return serializeGameState(state);
        }
      },
      *row.state);
}

// The archive's three parameters for a committed row: the ordinal, the
// finished game and its winner (blank for a draw), or three blanks when
// the row archives nothing.
std::vector<std::string> ArchiveParams(const PgHubStore::GameRow& row) {
  const auto archived = ArchivedChessGame(row);
  if (!archived.has_value()) return {"", "", ""};
  return {std::to_string(archived->ordinal), chess_play::serializeGameState(archived->game),
          ChessWinner(archived->game).value_or("")};
}

// Milliseconds truncated, not rounded: ended_at holds microseconds.
constexpr char kChessGameColumns[] =
    "archive_id, game_id, ordinal, game::text,"
    " floor(extract(epoch FROM ended_at) * 1000)::bigint, published";

// An event's games, in archive order, without restoring them: room_id
// leads so one query serves a room or the snapshot.
constexpr char kEventGameColumns[] =
    "room_id, event_id, archive_id, game_id, pairing, winner IS NULL, COALESCE(winner, '')";

std::optional<EventTag> TagFromColumns(const std::string& event_id, const std::string& pairing) {
  if (event_id.empty()) return std::nullopt;
  return EventTag{event_id, std::atoi(pairing.c_str())};
}

// Rows of kChessGameColumns, each restored through the game's serde: one
// that no longer restores costs that game, logged, as a games row does.
std::vector<PgHubStore::ChessGameRow> ChessGamesFrom(const std::string& room_id,
                                                     const pg::Result& result) {
  std::vector<PgHubStore::ChessGameRow> games;
  for (int i = 0; i < result.rows(); ++i) {
    const std::string game_id = result.Get(i, 1).value_or("");
    auto game = chess_play::deserializeGameState(result.Get(i, 3).value_or(""));
    if (!game.ok()) {
      LOG(ERROR) << "dropping archived chess game " << room_id << "/" << game_id << ": "
                 << game.status();
      continue;
    }
    games.push_back({std::atoll(result.Get(i, 0).value_or("0").c_str()), game_id,
                     std::atoi(result.Get(i, 2).value_or("0").c_str()), *std::move(game),
                     std::atoll(result.Get(i, 4).value_or("0").c_str()),
                     result.Get(i, 5).value_or("f") == "t"});
  }
  return games;
}

// A round robin's body (#1647): what its creator fixed and moderates.
// The room, id and version are columns of their own.
constexpr std::pair<PairingResult, const char*> kResultNames[] = {
    {PairingResult::kWhite, "white"},
    {PairingResult::kBlack, "black"},
    {PairingResult::kDraw, "draw"},
};

std::string EventBodyJson(const PgHubStore::ChessEventRow& row) {
  json pairings = json::array();
  for (const Pairing& p : row.pairings) {
    json pairing = {{"round", p.round}, {"white", p.white}, {"black", p.black}};
    for (const auto& [result, name] : kResultNames) {
      if (p.result == result) pairing["result"] = name;
    }
    if (p.forfeit) pairing["forfeit"] = true;
    pairings.push_back(std::move(pairing));
  }
  return json{{"creator", row.creator},
              {"entrants", row.entrants},
              {"terms",
               {{"setupId", row.terms.setup_id},
                {"initialMs", row.terms.time_control.initial_ms},
                {"incrementMs", row.terms.time_control.increment_ms}}},
              {"pairings", std::move(pairings)},
              {"withdrawn", row.withdrawn}}
      .dump();
}

bool IsStrings(const json& value) {
  return value.is_array() &&
         std::all_of(value.begin(), value.end(), [](const json& v) { return v.is_string(); });
}

absl::Status EventBodyFromJson(const std::string& text, PgHubStore::ChessEventRow& row) {
  const absl::Status malformed = absl::DataLossError("chess event body is malformed");
  const json body = json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (!body.is_object() || !body.contains("creator") || !body["creator"].is_string() ||
      !body.contains("entrants") || !IsStrings(body["entrants"]) || !body.contains("terms") ||
      !body["terms"].is_object() || !body.contains("pairings") || !body["pairings"].is_array() ||
      !body.contains("withdrawn") || !IsStrings(body["withdrawn"])) {
    return malformed;
  }
  const json& terms = body["terms"];
  if (!terms.contains("setupId") || !terms["setupId"].is_string() || !terms.contains("initialMs") ||
      !terms["initialMs"].is_number_integer() || !terms.contains("incrementMs") ||
      !terms["incrementMs"].is_number_integer()) {
    return malformed;
  }
  row.creator = body["creator"].get<std::string>();
  row.entrants = body["entrants"].get<std::vector<std::string>>();
  row.terms = {terms["setupId"].get<std::string>(),
               {terms["initialMs"].get<int64_t>(), terms["incrementMs"].get<int64_t>()}};
  row.withdrawn = body["withdrawn"].get<std::set<std::string>>();
  row.pairings.clear();
  for (const json& p : body["pairings"]) {
    if (!p.is_object() || !p.contains("round") || !p["round"].is_number_integer() ||
        !p.contains("white") || !p["white"].is_string() || !p.contains("black") ||
        !p["black"].is_string()) {
      return malformed;
    }
    Pairing pairing{p["round"].get<int>(), p["white"].get<std::string>(),
                    p["black"].get<std::string>()};
    if (p.contains("result")) {
      if (!p["result"].is_string()) return malformed;
      for (const auto& [result, name] : kResultNames) {
        if (p["result"].get<std::string>() == name) pairing.result = result;
      }
      if (!pairing.result.has_value()) return malformed;
    }
    if (p.contains("forfeit")) {
      if (!p["forfeit"].is_boolean()) return malformed;
      pairing.forfeit = p["forfeit"].get<bool>();
    }
    row.pairings.push_back(std::move(pairing));
  }
  return absl::OkStatus();
}

// Rows of (room_id, event_id, version, body), each with its games from
// rows of kEventGameColumns. A body that no longer decodes costs its
// event, logged, as an undecodable game row does.
std::vector<PgHubStore::ChessEventRow> EventsFrom(const pg::Result& events,
                                                  const pg::Result& games) {
  std::vector<PgHubStore::ChessEventRow> rows;
  for (int i = 0; i < events.rows(); ++i) {
    PgHubStore::ChessEventRow row;
    row.room_id = events.Get(i, 0).value_or("");
    row.event_id = events.Get(i, 1).value_or("");
    row.version = std::atoll(events.Get(i, 2).value_or("0").c_str());
    if (auto decoded = EventBodyFromJson(events.Get(i, 3).value_or(""), row); !decoded.ok()) {
      LOG(ERROR) << "dropping chess event " << row.room_id << "/" << row.event_id << ": "
                 << decoded;
      continue;
    }
    for (int j = 0; j < games.rows(); ++j) {
      if (games.Get(j, 0).value_or("") != row.room_id ||
          games.Get(j, 1).value_or("") != row.event_id) {
        continue;
      }
      std::optional<std::string> winner;
      if (games.Get(j, 5).value_or("t") == "f") winner = games.Get(j, 6).value_or("");
      row.games.push_back({std::atoll(games.Get(j, 2).value_or("0").c_str()),
                           games.Get(j, 3).value_or(""),
                           std::atoi(games.Get(j, 4).value_or("0").c_str()), std::move(winner)});
    }
    rows.push_back(std::move(row));
  }
  return rows;
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

void PgHubStore::Touch(const std::string& room_id) {
  ExecOrWarn("TouchRoom", kTouchRoom, {room_id});
}

void PgHubStore::Apply(const Op& op) {
  if (const auto* upsert = std::get_if<UpsertRoom>(&op)) {
    ExecOrWarn("UpsertRoom", kUpsertRoom, {upsert->room_id, SurfaceJson(upsert->surface)});
    Touch(upsert->room_id);
  } else if (const auto* set = std::get_if<SetRoomSurface>(&op)) {
    ExecOrWarn("SetRoomSurface", kSetRoomSurface, {set->room_id, SurfaceJson(set->surface)});
    Touch(set->room_id);
  } else if (const auto* set = std::get_if<SetChessPublished>(&op)) {
    ExecOrWarn("SetChessPublished", kSetChessPublished,
               {set->room_id, set->published ? "true" : "false"});
    Touch(set->room_id);
  } else if (const auto* erase = std::get_if<DeleteRoom>(&op)) {
    ExecOrWarn("DeleteRoom", kDeleteRoom, {erase->room_id});
  } else if (const auto* upsert = std::get_if<UpsertMember>(&op)) {
    const MemberRow& row = upsert->row;
    ExecOrWarn("UpsertMember", kUpsertMember,
               {row.room_id, row.player_id, row.connected ? "true" : "false",
                std::to_string(row.games_played), std::to_string(row.games_won),
                std::to_string(row.total_score)});
    Touch(row.room_id);
  } else if (const auto* erase = std::get_if<DeleteMember>(&op)) {
    ExecOrWarn("DeleteMember", kDeleteMember, {erase->room_id, erase->player_id});
    Touch(erase->room_id);
  } else if (const auto* erase = std::get_if<DeleteGame>(&op)) {
    ExecOrWarn("DeleteGame", kDeleteGame, {erase->room_id, erase->game_id});
    Touch(erase->room_id);
  } else if (const auto* notify = std::get_if<Notify>(&op)) {
    ExecOrWarn("Notify", "SELECT pg_notify($1, $2)", {notify->channel, notify->payload});
  } else if (const auto* touch = std::get_if<TouchRooms>(&op)) {
    ExecOrWarn("TouchRooms", kTouchRooms, {RosterJson(touch->room_ids)});
  } else if (const auto* sweep = std::get_if<SweepPublishedChess>(&op)) {
    ExecOrWarn("SweepPublishedChess", kSweepPublishedChess,
               {std::to_string(sweep->older_than.count())});
  } else if (const auto* sweep = std::get_if<SweepRooms>(&op)) {
    const auto swept =
        ExecOrWarn("SweepRooms", kSweepRooms,
                   {std::to_string(sweep->older_than.count()), RoomChannel(""), kSweepWake});
    if (swept.value_or(0) > 0) LOG(INFO) << "swept " << *swept << " stale rooms";
  }
}

absl::StatusOr<bool> PgHubStore::CommitGameSave(const GameRow& row,
                                                const std::string& notify_payload) {
  // The kind rides only the insert: it is fixed at creation, and the
  // update never writes it.
  std::vector<std::string> params = {row.room_id,
                                     row.game_id,
                                     RosterJson(row.roster),
                                     StateJson(row),
                                     std::to_string(row.version),
                                     RoomChannel(row.room_id),
                                     notify_payload};
  // A started row's kind is its state's: the column and the encoding
  // cannot disagree.
  if (row.version == 1) {
    params.emplace_back(GameKindName(row.state.has_value() ? KindOf(*row.state) : row.kind));
    params.push_back(row.event.has_value() ? row.event->event_id : "");
    params.push_back(row.event.has_value() ? std::to_string(row.event->pairing) : "");
  } else {
    for (std::string& param : ArchiveParams(row)) params.push_back(std::move(param));
  }
  auto result = db_->Exec(row.version == 1 ? kCommitInsert : kCommitUpdate, params);
  if (!result.ok()) return result.status();
  Touch(row.room_id);
  return result->rows() == 1;
}

absl::StatusOr<bool> PgHubStore::CommitGameFinish(const GameRow& row,
                                                  const std::vector<StatsDelta>& stats,
                                                  const std::string& notify_payload) {
  std::vector<std::string> params = {row.room_id,
                                     row.game_id,
                                     RosterJson(row.roster),
                                     StateJson(row),
                                     std::to_string(row.version),
                                     StatsJson(stats),
                                     RoomChannel(row.room_id),
                                     notify_payload};
  for (std::string& param : ArchiveParams(row)) params.push_back(std::move(param));
  auto result = db_->Exec(kCommitFinish, params);
  if (!result.ok()) return result.status();
  Touch(row.room_id);
  return result->rows() == 1;
}

absl::StatusOr<bool> PgHubStore::CommitChessEvent(const ChessEventRow& row,
                                                  const std::string& notify_payload) {
  auto result = db_->Exec(row.version == 1 ? kCommitEventInsert : kCommitEventUpdate,
                          {row.room_id, row.event_id, std::to_string(row.version),
                           EventBodyJson(row), RoomChannel(row.room_id), notify_payload});
  if (!result.ok()) return result.status();
  Touch(row.room_id);
  return result->rows() == 1;
}

absl::StatusOr<std::optional<PgHubStore::GameRow>> PgHubStore::LoadGame(
    const std::string& room_id, const std::string& game_id) {
  auto result = db_->Exec(
      "SELECT roster::text, COALESCE(state::text, ''), version, game,"
      " COALESCE(event_id, ''), COALESCE(pairing, 0) FROM games"
      " WHERE room_id = $1 AND game_id = $2",
      {room_id, game_id});
  if (!result.ok()) return result.status();
  if (result->rows() == 0) return std::nullopt;
  auto row = RowFromColumns(
      room_id, game_id, result->Get(0, 0).value_or("[]"), result->Get(0, 1).value_or(""),
      std::atoll(result->Get(0, 2).value_or("0").c_str()), result->Get(0, 3).value_or("golf"),
      TagFromColumns(result->Get(0, 4).value_or(""), result->Get(0, 5).value_or("")));
  if (!row.ok()) {
    LOG(ERROR) << "game " << room_id << "/" << game_id
               << " unreadable, treating as gone: " << row.status();
    return std::nullopt;
  }
  return std::optional<GameRow>(*std::move(row));
}

absl::StatusOr<PgHubStore::RoomRows> PgHubStore::LoadRoom(const std::string& room_id) {
  RoomRows out;
  auto room =
      db_->Exec("SELECT geometry::text, chess_published FROM rooms WHERE room_id = $1", {room_id});
  if (!room.ok()) return room.status();
  out.exists = room->rows() > 0;
  if (!out.exists) return out;
  out.surface = SurfaceFromColumn(room_id, room->Get(0, 0).value_or(""));
  out.chess_published = room->Get(0, 1).value_or("f") == "t";

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
      "SELECT game_id, roster::text, COALESCE(state::text, ''), version, game,"
      " COALESCE(event_id, ''), COALESCE(pairing, 0) FROM games WHERE room_id = $1",
      {room_id});
  if (!games.ok()) return games.status();
  for (int i = 0; i < games->rows(); ++i) {
    const std::string game_id = games->Get(i, 0).value_or("");
    auto row = RowFromColumns(
        room_id, game_id, games->Get(i, 1).value_or("[]"), games->Get(i, 2).value_or(""),
        std::atoll(games->Get(i, 3).value_or("0").c_str()), games->Get(i, 4).value_or("golf"),
        TagFromColumns(games->Get(i, 5).value_or(""), games->Get(i, 6).value_or("")));
    if (!row.ok()) {
      LOG(ERROR) << "dropping game " << room_id << "/" << game_id << ": " << row.status();
      continue;
    }
    out.games.push_back(*std::move(row));
  }

  auto events = db_->Exec(
      "SELECT room_id, event_id, version, body::text FROM chess_events"
      " WHERE room_id = $1 ORDER BY event_id",
      {room_id});
  if (!events.ok()) return events.status();
  auto event_games = db_->Exec(absl::StrCat("SELECT ", kEventGameColumns,
                                            " FROM chess_games"
                                            " WHERE room_id = $1 AND event_id IS NOT NULL"
                                            " ORDER BY archive_id"),
                               {room_id});
  if (!event_games.ok()) return event_games.status();
  out.events = EventsFrom(*events, *event_games);
  return out;
}

absl::StatusOr<PgHubStore::Snapshot> PgHubStore::LoadSnapshot() {
  Snapshot snapshot;
  auto rooms = db_->Exec("SELECT room_id, geometry::text, chess_published FROM rooms");
  if (!rooms.ok()) return rooms.status();
  for (int i = 0; i < rooms->rows(); ++i) {
    const std::string room_id = rooms->Get(i, 0).value_or("");
    snapshot.rooms.push_back({room_id, SurfaceFromColumn(room_id, rooms->Get(i, 1).value_or("")),
                              rooms->Get(i, 2).value_or("f") == "t"});
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
      "SELECT room_id, game_id, roster::text, COALESCE(state::text, ''), version, game,"
      " COALESCE(event_id, ''), COALESCE(pairing, 0) FROM games");
  if (!games.ok()) return games.status();
  for (int i = 0; i < games->rows(); ++i) {
    const std::string room_id = games->Get(i, 0).value_or("");
    const std::string game_id = games->Get(i, 1).value_or("");
    // Undecodable rows cost their game, not the boot — the same blast
    // radius whichever column is bad.
    auto row = RowFromColumns(
        room_id, game_id, games->Get(i, 2).value_or("[]"), games->Get(i, 3).value_or(""),
        std::atoll(games->Get(i, 4).value_or("0").c_str()), games->Get(i, 5).value_or("golf"),
        TagFromColumns(games->Get(i, 6).value_or(""), games->Get(i, 7).value_or("")));
    if (!row.ok()) {
      LOG(ERROR) << "dropping game " << room_id << "/" << game_id << ": " << row.status();
      continue;
    }
    snapshot.games.push_back(*std::move(row));
  }

  auto events = db_->Exec(
      "SELECT room_id, event_id, version, body::text FROM chess_events"
      " ORDER BY room_id, event_id");
  if (!events.ok()) return events.status();
  auto event_games = db_->Exec(absl::StrCat("SELECT ", kEventGameColumns,
                                            " FROM chess_games"
                                            " WHERE event_id IS NOT NULL ORDER BY archive_id"));
  if (!event_games.ok()) return event_games.status();
  snapshot.events = EventsFrom(*events, *event_games);
  return snapshot;
}

absl::StatusOr<PgHubStore::ChessHistory> PgHubStore::LoadChessHistory(const std::string& room_id,
                                                                      int limit) {
  auto room = db_->Exec("SELECT chess_published FROM rooms WHERE room_id = $1", {room_id});
  if (!room.ok()) return room.status();
  if (room->rows() == 0) return absl::NotFoundError("no such room");
  auto games = db_->Exec(absl::StrCat("SELECT ", kChessGameColumns,
                                      " FROM chess_games WHERE room_id = $1"
                                      " ORDER BY archive_id DESC LIMIT $2::integer"),
                         {room_id, std::to_string(limit)});
  if (!games.ok()) return games.status();
  return ChessHistory{room->Get(0, 0).value_or("f") == "t", ChessGamesFrom(room_id, *games)};
}

absl::StatusOr<std::optional<PgHubStore::ChessGameRow>> PgHubStore::LoadChessGame(
    const std::string& room_id, const ChessGameKey& key) {
  auto games =
      key.archive_id.has_value()
          ? db_->Exec(
                absl::StrCat("SELECT ", kChessGameColumns,
                             " FROM chess_games WHERE room_id = $1 AND archive_id = $2::bigint"),
                {room_id, std::to_string(*key.archive_id)})
          : db_->Exec(absl::StrCat("SELECT ", kChessGameColumns,
                                   " FROM chess_games WHERE room_id = $1 AND game_id = $2"
                                   " AND ordinal = $3::integer ORDER BY archive_id DESC LIMIT 1"),
                      {room_id, key.game_id, std::to_string(key.ordinal)});
  if (!games.ok()) return games.status();
  auto rows = ChessGamesFrom(room_id, *games);
  if (rows.empty()) return std::nullopt;
  return std::optional<ChessGameRow>(std::move(rows.front()));
}

absl::StatusOr<std::vector<PgHubStore::PublishedChessGame>> PgHubStore::LoadPublishedChess(
    int64_t after_archive_id, int limit) {
  // A row that no longer restores is dropped, as an undecodable games row
  // is; reading on past it fills the page, so a reader advancing by the
  // last id it got is never handed a page the bad rows emptied.
  std::vector<PublishedChessGame> page;
  int64_t cursor = after_archive_id;
  while (std::ssize(page) < limit) {
    auto games = db_->Exec(
        "SELECT archive_id, game::text, floor(extract(epoch FROM ended_at) * 1000)::bigint"
        " FROM published_chess_games WHERE archive_id > $1::bigint"
        " ORDER BY archive_id LIMIT $2::integer",
        {std::to_string(cursor), std::to_string(limit - std::ssize(page))});
    if (!games.ok()) return games.status();
    if (games->rows() == 0) break;
    for (int i = 0; i < games->rows(); ++i) {
      const std::string archive_id = games->Get(i, 0).value_or("0");
      cursor = std::atoll(archive_id.c_str());
      auto game = chess_play::deserializeGameState(games->Get(i, 1).value_or(""));
      if (!game.ok()) {
        LOG(ERROR) << "dropping published chess game " << archive_id << ": " << game.status();
        continue;
      }
      page.push_back({cursor, *std::move(game), std::atoll(games->Get(i, 2).value_or("0").c_str())});
    }
  }
  return page;
}

absl::StatusOr<PgHubStore::GameRow> PgHubStore::RowFromColumns(
    const std::string& room_id, const std::string& game_id, const std::string& roster_json,
    const std::string& state_json, int64_t version, const std::string& game,
    std::optional<EventTag> event) {
  GameRow row;
  row.room_id = room_id;
  row.game_id = game_id;
  row.version = version;
  row.event = std::move(event);
  const std::optional<GameKind> kind = ParseGameKind(game);
  if (!kind.has_value()) return absl::DataLossError("unknown game kind: " + game);
  row.kind = *kind;
  const json roster = json::parse(roster_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (!roster.is_array()) return absl::DataLossError("roster is not an array of player ids");
  for (const json& entry : roster) {
    if (!entry.is_string()) return absl::DataLossError("roster is not an array of player ids");
    row.roster.push_back(entry.get<std::string>());
  }
  if (!state_json.empty()) {
    if (row.kind == GameKind::kCastle) {
      auto state = castle::deserializeGameState(state_json);
      if (!state.ok()) return state.status();
      row.state.emplace(*std::move(state));
    } else if (row.kind == GameKind::kRummy) {
      auto state = rummy::deserializeTableState(state_json);
      if (!state.ok()) return state.status();
      row.state.emplace(*std::move(state));
    } else if (row.kind == GameKind::kChess) {
      auto terms = TermsFromJson(state_json);
      if (!terms.ok()) return terms.status();
      if (terms->has_value()) {
        row.terms = **terms;
        return row;
      }
      auto state = chess_play::deserializeTable(state_json);
      if (!state.ok()) return state.status();
      row.state.emplace(*std::move(state));
    } else {
      auto state = golf::deserializeGameState(state_json);
      if (!state.ok()) return state.status();
      row.state.emplace(*std::move(state));
    }
  }
  return row;
}

}  // namespace games_hub
