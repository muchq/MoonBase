#include "domains/games/apis/one_d4_worker/pg_game_sink.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"
#include "domains/platform/libs/pg/pg.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;

// What the sink promises is about concurrent SQL: the batch is one unit,
// the fence is inside the transaction that writes, and a reindex replaces
// a game's occurrences rather than doubling them. None of that can be
// checked without a server, so these skip without PG_TEST_DB_URL — which
// the build-and-test job supplies.

constexpr char kRequest[] = "00000000-0000-4000-8000-000000000001";
constexpr char kOwner[] = "worker-1";

/// This suite's own schema, so its DDL cannot race pg_queue_test's: both
/// drop and recreate indexing_requests, and bazel runs them at the same
/// time against one database.
constexpr char kSchema[] = "one_d4_game_sink_test";

/// The connection options carried in the conninfo rather than set with a
/// statement, because pg::Client reconnects lazily and a reconnect drops
/// anything a session-level SET established — silently, and the timezone
/// would go back to the one value that makes half these assertions blind.
///
/// Not UTC, deliberately: under a UTC session `AT TIME ZONE 'UTC'` and a
/// bare cast agree, so the convention could be deleted and nothing here
/// would notice.
std::string Conninfo(const std::string& url) {
  return absl::StrCat(url, url.find('?') == std::string::npos ? "?" : "&",
                      "options=-c%20search_path%3D", kSchema,
                      "%20-c%20timezone%3DAmerica/New_York");
}

class PgGameSinkTest : public testing::Test {
 protected:
  void SetUp() override {
    const char* url = std::getenv("PG_TEST_DB_URL");
    if (url == nullptr || *url == '\0') GTEST_SKIP() << "PG_TEST_DB_URL unset";
    {
      pg::Client bootstrap(url);
      ASSERT_TRUE(bootstrap.Exec(absl::StrCat("CREATE SCHEMA IF NOT EXISTS ", kSchema)).ok());
    }
    conninfo_ = Conninfo(url);
    client_ = std::make_unique<pg::Client>(conninfo_);

    // Column for column what PostgresSqlDialect creates and Migration adds.
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS indexed_periods").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS motif_occurrences").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS game_features").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS indexing_requests").ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE indexing_requests (
            id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            player         VARCHAR(255) NOT NULL,
            platform       VARCHAR(50) NOT NULL,
            start_month    VARCHAR(7) NOT NULL,
            end_month      VARCHAR(7) NOT NULL,
            status         VARCHAR(20) NOT NULL DEFAULT 'PENDING',
            created_at     TIMESTAMP NOT NULL DEFAULT now(),
            updated_at     TIMESTAMP NOT NULL DEFAULT now(),
            owner_id       VARCHAR(128),
            lease_expires_at TIMESTAMP
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE game_features (
            id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            request_id    UUID NOT NULL REFERENCES indexing_requests(id),
            game_url      VARCHAR(1024) NOT NULL UNIQUE,
            platform      VARCHAR(50) NOT NULL,
            white_username VARCHAR(255),
            black_username VARCHAR(255),
            white_elo     INT,
            black_elo     INT,
            white_title   VARCHAR(10),
            black_title   VARCHAR(10),
            time_class    VARCHAR(50),
            eco           VARCHAR(10),
            opening_name  VARCHAR(255),
            opening_family VARCHAR(255),
            result        VARCHAR(20),
            played_at     TIMESTAMP,
            num_moves     INT,
            indexed_at    TIMESTAMP NOT NULL DEFAULT now(),
            pgn           TEXT
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE motif_occurrences (
            id           VARCHAR(36) NOT NULL PRIMARY KEY,
            game_url     VARCHAR(1024) NOT NULL
                         REFERENCES game_features(game_url) ON DELETE CASCADE,
            motif        VARCHAR(50) NOT NULL,
            ply          INT NOT NULL,
            side         VARCHAR(5) NOT NULL,
            move_number  INT NOT NULL,
            description  TEXT,
            moved_piece  VARCHAR(20),
            attacker     VARCHAR(20),
            target       VARCHAR(20),
            is_discovered BOOLEAN NOT NULL DEFAULT FALSE,
            is_mate       BOOLEAN NOT NULL DEFAULT FALSE,
            pin_type      VARCHAR(8)
        ))")
                    .ok());

    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE indexed_periods (
            id             UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            player         VARCHAR(255) NOT NULL,
            platform       VARCHAR(50) NOT NULL,
            year_month     VARCHAR(7) NOT NULL,
            fetched_at     TIMESTAMP NOT NULL,
            is_complete    BOOLEAN NOT NULL,
            games_count    INT NOT NULL,
            exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE,
            CONSTRAINT indexed_periods_unique
                UNIQUE (player, platform, year_month, exclude_bullet)
        ))")
                    .ok());

    ASSERT_TRUE(client_
                    ->Exec("INSERT INTO indexing_requests (id, player, platform, start_month,"
                           " end_month, owner_id, lease_expires_at) VALUES ($1, 'alice',"
                           " 'chess.com', '2026-01', '2026-01', $2, NOW() + INTERVAL '5 minutes')",
                           {kRequest, kOwner})
                    .ok());
    sink_ = std::make_unique<PgGameSink>(*client_, kRequest, kOwner);
  }

  std::string One(const std::string& sql, const std::vector<std::string>& params = {}) {
    const auto result = client_->Exec(sql, params);
    EXPECT_TRUE(result.ok()) << result.status();
    if (!result.ok() || result->rows() == 0) return "(none)";
    return result->Get(0, 0).value_or("(null)");
  }

  /// The control for the two assertions above: under a UTC session the
  /// conversion and a bare cast agree, so they would hold with `AT TIME
  /// ZONE 'UTC'` deleted. pg::Client reconnects lazily, and a reconnect
  /// drops the session setting — silently, and back to exactly that.
  void ExpectSessionIsNotUtc() {
    EXPECT_EQ(One("SHOW TimeZone"), "America/New_York")
        << "the session went back to UTC, so the assertion below proves nothing";
  }

  std::string conninfo_;
  std::unique_ptr<pg::Client> client_;
  std::unique_ptr<PgGameSink> sink_;
};

IndexedGame AGame(std::string_view url = "https://chess.com/game/1") {
  IndexedGame game;
  game.url = std::string(url);
  game.platform = "chess.com";
  game.white_username = "alice";
  game.black_username = "bob";
  game.white_elo = 2800;
  game.black_elo = 2700;
  game.white_title = "GM";
  game.time_class = "blitz";
  game.eco = "C20";
  game.opening_name = "Kings Pawn Opening";
  game.opening_family = "Kings Pawn Opening";
  game.result = "1-0";
  game.played_at = 1'700'000'000;
  game.num_moves = 4;
  game.indexed_at = 1'700'000'100;
  game.pgn = "1. e4 e5";
  return game;
}

one_d4::MotifOccurrence AnOccurrence(one_d4::Motif motif, int ply) {
  one_d4::MotifOccurrence occurrence;
  occurrence.motif = motif;
  occurrence.ply = ply;
  occurrence.move_number = (ply + 1) / 2;
  occurrence.side = ply % 2 == 1 ? chess_cpp::Side::kWhite : chess_cpp::Side::kBlack;
  occurrence.description = "something happened";
  occurrence.attacker = "Qh5";
  occurrence.target = "f7";
  return occurrence;
}

TEST_F(PgGameSinkTest, WritesTheRowTheJavaWorkerWouldHaveWritten) {
  IndexedGame game = AGame();
  game.occurrences = {AnOccurrence(one_d4::Motif::kCheck, 7)};

  ASSERT_TRUE(sink_->Write({&game, 1}).ok());

  EXPECT_EQ(One("SELECT white_username FROM game_features"), "alice");
  EXPECT_EQ(One("SELECT white_elo FROM game_features"), "2800");
  EXPECT_EQ(One("SELECT white_title FROM game_features"), "GM");
  EXPECT_EQ(One("SELECT eco FROM game_features"), "C20");
  EXPECT_EQ(One("SELECT opening_family FROM game_features"), "Kings Pawn Opening");
  EXPECT_EQ(One("SELECT result FROM game_features"), "1-0");
  EXPECT_EQ(One("SELECT num_moves FROM game_features"), "4");
  ExpectSessionIsNotUtc();
  EXPECT_EQ(One("SELECT to_char(indexed_at, 'YYYY-MM-DD HH24:MI:SS') FROM game_features"),
            "2023-11-14 22:15:00")
      << "stamped from the worker's clock, which is the one retention compares against";
  EXPECT_EQ(One("SELECT request_id::text FROM game_features"), kRequest);
  // The column is a wall clock and the convention is UTC — the same one
  // GameFeatureDao writes on, and the one ChessQL's month filters read.
  ExpectSessionIsNotUtc();
  EXPECT_EQ(One("SELECT to_char(played_at, 'YYYY-MM-DD HH24:MI:SS') FROM game_features"),
            "2023-11-14 22:13:20");
  EXPECT_EQ(One("SELECT motif FROM motif_occurrences"), "CHECK");
  EXPECT_EQ(One("SELECT ply FROM motif_occurrences"), "7");
  EXPECT_EQ(One("SELECT side FROM motif_occurrences"), "white");
  EXPECT_EQ(One("SELECT attacker FROM motif_occurrences"), "Qh5");
  EXPECT_EQ(One("SELECT length(id)::text FROM motif_occurrences"), "36");
}

TEST_F(PgGameSinkTest, LeavesUnknownTextNullRatherThanBlank) {
  // A blank username and a missing one read the same to a query only if
  // one of them is stored wrong.
  IndexedGame game = AGame();
  game.white_title = "";
  game.black_title = "";
  game.opening_name = "";
  game.played_at = 0;
  game.occurrences = {AnOccurrence(one_d4::Motif::kAttack, 3)};
  game.occurrences[0].moved_piece = std::nullopt;

  ASSERT_TRUE(sink_->Write({&game, 1}).ok());

  EXPECT_EQ(One("SELECT count(*)::text FROM game_features WHERE white_title IS NULL"), "1");
  EXPECT_EQ(One("SELECT count(*)::text FROM game_features WHERE opening_name IS NULL"), "1");
  EXPECT_EQ(One("SELECT count(*)::text FROM game_features WHERE played_at IS NULL"), "1")
      << "the epoch is not a date a game was played on";
  EXPECT_EQ(One("SELECT count(*)::text FROM motif_occurrences WHERE moved_piece IS NULL"), "1");
}

TEST_F(PgGameSinkTest, WritesTheNumbersTheJavaWorkerWritesEvenWhenTheyAreZero) {
  // Java binds these as numbers, so an unrated game is white_elo = 0 and
  // an unplayable one is num_moves = 0. Stored NULL instead, the same game
  // answers a `white_elo < 1000` filter differently depending on which
  // worker indexed it.
  IndexedGame game = AGame();
  game.white_elo = 0;
  game.black_elo = 0;
  game.num_moves = 0;

  ASSERT_TRUE(sink_->Write({&game, 1}).ok());

  EXPECT_EQ(One("SELECT white_elo::text FROM game_features"), "0");
  EXPECT_EQ(One("SELECT black_elo::text FROM game_features"), "0");
  EXPECT_EQ(One("SELECT num_moves::text FROM game_features"), "0");
}

TEST_F(PgGameSinkTest, ReindexingAGameReplacesItsMotifsRatherThanDoublingThem) {
  IndexedGame game = AGame();
  game.occurrences = {AnOccurrence(one_d4::Motif::kCheck, 7), AnOccurrence(one_d4::Motif::kPin, 9)};
  ASSERT_TRUE(sink_->Write({&game, 1}).ok());

  // The second look finds one motif where the first found two. Both the
  // dropped row and the doubled row are wrong.
  game.occurrences = {AnOccurrence(one_d4::Motif::kCheck, 7)};
  ASSERT_TRUE(sink_->Write({&game, 1}).ok());

  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "1");
  EXPECT_EQ(One("SELECT count(*)::text FROM motif_occurrences"), "1");
  EXPECT_EQ(One("SELECT motif FROM motif_occurrences"), "CHECK");
}

TEST_F(PgGameSinkTest, RefusesToWriteForARequestItNoLongerOwns) {
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET owner_id = 'worker-2' WHERE id = $1"
                         " RETURNING id",
                         {kRequest})
                  .ok());
  const IndexedGame game = AGame();

  const absl::Status written = sink_->Write({&game, 1});

  EXPECT_EQ(written.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "0");
}

TEST_F(PgGameSinkTest, RefusesToWriteOnAnExpiredLease) {
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET lease_expires_at = NOW() -"
                         " INTERVAL '1 minute' WHERE id = $1 RETURNING id",
                         {kRequest})
                  .ok());
  const IndexedGame game = AGame();

  EXPECT_EQ(sink_->Write({&game, 1}).code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "0");
}

TEST_F(PgGameSinkTest, RefusesToWriteForARequestAlreadyFinished) {
  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET status = 'FAILED' WHERE id = $1"
                         " RETURNING id",
                         {kRequest})
                  .ok());
  const IndexedGame game = AGame();

  EXPECT_EQ(sink_->Write({&game, 1}).code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "0");
}

TEST_F(PgGameSinkTest, WritesABatchAsOneUnitOrNotAtAll) {
  // The second game is unwritable — the url exceeds the column. A batch
  // that lands the first one anyway leaves a game with a stamp and no
  // motifs, which nothing downstream can tell from a quiet game.
  IndexedGame good = AGame("https://chess.com/game/1");
  good.occurrences = {AnOccurrence(one_d4::Motif::kCheck, 7)};
  IndexedGame bad = AGame(std::string(2000, 'u'));

  const IndexedGame batch[] = {good, bad};
  EXPECT_FALSE(sink_->Write(batch).ok());

  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "0");
  EXPECT_EQ(One("SELECT count(*)::text FROM motif_occurrences"), "0");
}

TEST_F(PgGameSinkTest, WritesABatchInGameUrlOrder) {
  // Both phases ordered the same way, so two flushes over an overlapping
  // set queue behind each other rather than deadlocking half way through.
  // Only sorting one phase is worthless: the upsert takes an index-tuple
  // lock per game_url, so an inverted pair deadlocks there first.
  IndexedGame first = AGame("https://chess.com/game/1");
  IndexedGame second = AGame("https://chess.com/game/2");
  IndexedGame third = AGame("https://chess.com/game/3");

  const IndexedGame batch[] = {third, first, second};
  ASSERT_TRUE(sink_->Write(batch).ok());

  EXPECT_EQ(One("SELECT string_agg(game_url, ',' ORDER BY ctid) FROM game_features"),
            "https://chess.com/game/1,https://chess.com/game/2,https://chess.com/game/3");
}

TEST_F(PgGameSinkTest, WritesEveryGameOfABatch) {
  IndexedGame first = AGame("https://chess.com/game/1");
  IndexedGame second = AGame("https://chess.com/game/2");
  first.occurrences = {AnOccurrence(one_d4::Motif::kCheck, 7)};
  second.occurrences = {AnOccurrence(one_d4::Motif::kPin, 3), AnOccurrence(one_d4::Motif::kPin, 5)};

  const IndexedGame batch[] = {second, first};
  ASSERT_TRUE(sink_->Write(batch).ok());

  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "2");
  EXPECT_EQ(One("SELECT count(*)::text FROM motif_occurrences"), "3");
}

IndexedMonth AMonth() {
  IndexedMonth month;
  month.player = "alice";
  month.platform = "chess.com";
  month.month = "2026-01";
  month.fetched_at = 1'700'000'000;
  month.games = 12;
  return month;
}

TEST_F(PgGameSinkTest, RecordsTheMonthAsThePeriodCacheReadsIt) {
  ASSERT_TRUE(sink_->RecordMonth(AMonth()).ok());

  EXPECT_EQ(One("SELECT games_count::text FROM indexed_periods"), "12");
  EXPECT_EQ(One("SELECT is_complete::text FROM indexed_periods"), "true");
  EXPECT_EQ(One("SELECT exclude_bullet::text FROM indexed_periods"), "false");
  ExpectSessionIsNotUtc();
  EXPECT_EQ(One("SELECT to_char(fetched_at, 'YYYY-MM-DD HH24:MI:SS') FROM indexed_periods"),
            "2023-11-14 22:13:20");
}

TEST_F(PgGameSinkTest, RereadingAMonthUpdatesItsPeriodRatherThanAddingOne) {
  ASSERT_TRUE(sink_->RecordMonth(AMonth()).ok());
  IndexedMonth again = AMonth();
  again.games = 15;
  again.complete = false;
  ASSERT_TRUE(sink_->RecordMonth(again).ok());

  EXPECT_EQ(One("SELECT count(*)::text FROM indexed_periods"), "1");
  EXPECT_EQ(One("SELECT games_count::text FROM indexed_periods"), "15");
  EXPECT_EQ(One("SELECT is_complete::text FROM indexed_periods"), "false");
}

TEST_F(PgGameSinkTest, FindsAMonthItAlreadyIndexed) {
  ASSERT_TRUE(sink_->RecordMonth(AMonth()).ok());

  const auto found = sink_->MonthAlreadyIndexed(AMonth());
  ASSERT_TRUE(found.ok()) << found.status();
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(**found, 12);
}

TEST_F(PgGameSinkTest, DoesNotFindAMonthItHasNotIndexed) {
  IndexedMonth other = AMonth();
  other.month = "2026-02";
  ASSERT_TRUE(sink_->RecordMonth(other).ok());

  const auto found = sink_->MonthAlreadyIndexed(AMonth());
  ASSERT_TRUE(found.ok()) << found.status();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PgGameSinkTest, DoesNotFindAnIncompleteMonth) {
  // Incomplete means the row is missing something it should carry, or the
  // month is not over. Either way it is exactly the month to refetch.
  IndexedMonth partial = AMonth();
  partial.complete = false;
  ASSERT_TRUE(sink_->RecordMonth(partial).ok());

  const auto found = sink_->MonthAlreadyIndexed(AMonth());
  ASSERT_TRUE(found.ok()) << found.status();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PgGameSinkTest, DoesNotFindAMonthIndexedUnderTheOtherFilter) {
  // A month indexed with bullet games answers nothing about the same month
  // indexed without them.
  ASSERT_TRUE(sink_->RecordMonth(AMonth()).ok());

  IndexedMonth without_bullet = AMonth();
  without_bullet.exclude_bullet = true;
  const auto found = sink_->MonthAlreadyIndexed(without_bullet);
  ASSERT_TRUE(found.ok()) << found.status();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PgGameSinkTest, DoesNotFindAnotherPlayersMonth) {
  ASSERT_TRUE(sink_->RecordMonth(AMonth()).ok());

  IndexedMonth someone_else = AMonth();
  someone_else.player = "bob";
  const auto found = sink_->MonthAlreadyIndexed(someone_else);
  ASSERT_TRUE(found.ok()) << found.status();
  EXPECT_FALSE(found->has_value());
}

TEST_F(PgGameSinkTest, AMonthWithAndWithoutBulletAreTwoPeriods) {
  // The cache is keyed by the filter too: a range indexed without bullet
  // games answers nothing about the same month indexed with them.
  IndexedMonth with_bullet = AMonth();
  IndexedMonth without_bullet = AMonth();
  without_bullet.exclude_bullet = true;
  without_bullet.games = 7;
  ASSERT_TRUE(sink_->RecordMonth(with_bullet).ok());
  ASSERT_TRUE(sink_->RecordMonth(without_bullet).ok());

  EXPECT_EQ(One("SELECT count(*)::text FROM indexed_periods"), "2");
  EXPECT_EQ(One("SELECT games_count::text FROM indexed_periods WHERE exclude_bullet"), "7");
}

TEST_F(PgGameSinkTest, AnEmptyBatchStillAsksWhoOwnsTheRequest) {
  // The caller uses an empty write purely to ask the question, because the
  // period row that follows it carries no fence of its own.
  EXPECT_TRUE(sink_->Write({}).ok());
  EXPECT_EQ(One("SELECT count(*)::text FROM game_features"), "0");

  ASSERT_TRUE(client_
                  ->Exec("UPDATE indexing_requests SET owner_id = 'worker-2' WHERE id = $1"
                         " RETURNING id",
                         {kRequest})
                  .ok());
  EXPECT_EQ(sink_->Write({}).code(), absl::StatusCode::kFailedPrecondition);
}

TEST_F(PgGameSinkTest, TakesTheRequestRowRatherThanReadingIt) {
  // The fence is SELECT ... FOR UPDATE, not a bare read. Nothing sets an
  // isolation level, so this runs READ COMMITTED, where a plain SELECT is a
  // snapshot rather than a claim — a rival takeover could commit between
  // the check and the writes, and the batch would land against a request
  // this worker had already lost.
  //
  // Asked with an empty batch, which is the case that needs the lock and
  // the only one that can see it. A batch with games in it inserts into
  // game_features, whose foreign key takes a KEY SHARE on the same request
  // row, so it waits behind a rival whether the fence locks or not. The
  // empty write takes no such lock — and it is the write whose answer
  // gates the unfenced period row.
  pg::Client rival(conninfo_);

  absl::Notification held;
  absl::Notification release;
  std::thread holder([&] {
    const absl::Status locked = rival.InTransaction([&](pg::Transaction& tx) {
      const auto row =
          tx.Exec("SELECT id FROM indexing_requests WHERE id = $1 FOR UPDATE", {kRequest});
      EXPECT_TRUE(row.ok()) << row.status();
      held.Notify();
      release.WaitForNotification();
      return absl::OkStatus();
    });
    EXPECT_TRUE(locked.ok()) << locked;
  });

  held.WaitForNotification();
  std::atomic<bool> answered{false};
  std::thread asker([&] {
    EXPECT_TRUE(sink_->Write({}).ok());
    answered = true;
  });
  // Long enough that a fence which only read would have answered by now.
  absl::SleepFor(absl::Milliseconds(250));
  const bool read_through_the_lock = answered;

  release.Notify();
  holder.join();
  asker.join();

  EXPECT_FALSE(read_through_the_lock)
      << "the fence answered while another transaction held the request row, so it read a"
         " snapshot rather than claiming the row";
}

}  // namespace
}  // namespace one_d4_worker
