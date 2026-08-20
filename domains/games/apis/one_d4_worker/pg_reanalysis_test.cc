#include "domains/games/apis/one_d4_worker/pg_reanalysis.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/notification.h"
#include "absl/time/clock.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The keyset walk and the fenced replace, against a real Postgres — both
// are properties of statements rather than of code. Skips without
// PG_TEST_DB_URL.

std::string Url(int n) { return absl::StrFormat("https://chess.com/game/%04d", n); }

/// This suite's own schema. It drops and recreates three tables two other
/// suites also use, and bazel runs them at the same time against one
/// database — see pg_game_sink_test, which hit this first.
constexpr char kSchema[] = "one_d4_pg_reanalysis_test";

/// Not UTC, deliberately: the fence compares lease_expires_at to NOW(), and
/// under a UTC session a TIMESTAMPTZ column would agree with a TIMESTAMP
/// one, so the fixture could drift from production unnoticed.
std::string Conninfo(const std::string& url) {
  return absl::StrCat(url, url.find('?') == std::string::npos ? "?" : "&",
                      "options=-c%20search_path%3D", kSchema,
                      "%20-c%20timezone%3DAmerica/New_York");
}

class PgReanalysisTest : public testing::Test {
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

    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS motif_occurrences").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS game_features").ok());
    ASSERT_TRUE(client_->Exec("DROP TABLE IF EXISTS reanalysis_requests").ok());

    // Only the columns this pass reads or writes — schema_contract_test
    // keeps the shape honest against the Java DDL.
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE game_features (
            id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            game_url      VARCHAR(1024) NOT NULL UNIQUE,
            pgn           TEXT
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE motif_occurrences (
            id            VARCHAR(36) PRIMARY KEY,
            game_url      VARCHAR(1024) NOT NULL,
            motif         VARCHAR(50) NOT NULL,
            ply           INT NOT NULL,
            side          VARCHAR(5) NOT NULL,
            move_number   INT NOT NULL,
            description   TEXT,
            moved_piece   VARCHAR(20),
            attacker      VARCHAR(20),
            target        VARCHAR(20),
            is_discovered BOOLEAN NOT NULL DEFAULT FALSE,
            is_mate       BOOLEAN NOT NULL DEFAULT FALSE,
            pin_type      VARCHAR(8)
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec(R"(
        CREATE TABLE reanalysis_requests (
            id               UUID PRIMARY KEY DEFAULT gen_random_uuid(),
            status           VARCHAR(20) NOT NULL DEFAULT 'PENDING',
            created_at       TIMESTAMP NOT NULL DEFAULT now(),
            updated_at       TIMESTAMP NOT NULL DEFAULT now(),
            owner_id         VARCHAR(128),
            lease_expires_at TIMESTAMP,
            attempts         INT NOT NULL DEFAULT 0,
            error_message    TEXT,
            cursor_game_url  VARCHAR(1024),
            games_processed  INT NOT NULL DEFAULT 0,
            games_failed     INT NOT NULL DEFAULT 0
        ))")
                    .ok());
    ASSERT_TRUE(client_
                    ->Exec("CREATE UNIQUE INDEX idx_reanalysis_requests_single_live ON "
                           "reanalysis_requests ((true)) WHERE status IN ('PENDING', "
                           "'PROCESSING')")
                    .ok());
  }

  void AddGame(const std::string& url, const std::string& pgn) {
    ASSERT_TRUE(
        client_->Exec("INSERT INTO game_features (game_url, pgn) VALUES ($1, $2)", {url, pgn})
            .ok());
  }

  void AddOccurrence(const std::string& url, const std::string& motif) {
    ASSERT_TRUE(client_
                    ->Exec(R"(INSERT INTO motif_occurrences
                                (id, game_url, motif, ply, side, move_number)
                              VALUES (gen_random_uuid()::text, $1, $2, 1, 'WHITE', 1))",
                           {url, motif})
                    .ok());
  }

  int OccurrenceCount(const std::string& url) {
    auto rows = client_->Exec("SELECT count(*) FROM motif_occurrences WHERE game_url = $1", {url});
    EXPECT_TRUE(rows.ok());
    return std::stoi(rows->Get(0, 0).value_or("0"));
  }

  /// A claimed pass, returning its id.
  std::string ClaimedPass(const std::string& owner) {
    auto inserted = client_->Exec(
        R"(INSERT INTO reanalysis_requests (status, owner_id, lease_expires_at)
           VALUES ('PROCESSING', $1, NOW() + INTERVAL '5 minutes') RETURNING id)",
        {owner});
    EXPECT_TRUE(inserted.ok());
    return inserted->Get(0, 0).value_or("");
  }

  std::string conninfo_;
  std::unique_ptr<pg::Client> client_;
};

// Inserted descending, so heap order is the reverse of url order. Without
// ORDER BY, Postgres may return heap order and the keyset walk — whose
// whole premise is a total order — silently pages backwards.
TEST_F(PgReanalysisTest, PagesInUrlOrderWhateverOrderTheRowsWereWrittenIn) {
  for (int i = 9; i >= 0; --i) AddGame(Url(i), "pgn");
  PgGameCorpus corpus(*client_);

  auto page = corpus.After("", 4);
  ASSERT_TRUE(page.ok());
  ASSERT_EQ(page->size(), 4u);
  EXPECT_EQ((*page)[0].url, Url(0));
  EXPECT_EQ((*page)[1].url, Url(1));
  EXPECT_EQ((*page)[2].url, Url(2));
  EXPECT_EQ((*page)[3].url, Url(3));
}

TEST_F(PgReanalysisTest, PagesFromTheBeginningWhenTheCursorIsEmpty) {
  for (int i = 0; i < 5; ++i) AddGame(Url(i), "pgn");
  PgGameCorpus corpus(*client_);

  auto page = corpus.After("", 3);
  ASSERT_TRUE(page.ok());
  ASSERT_EQ(page->size(), 3u);
  EXPECT_EQ((*page)[0].url, Url(0));
  EXPECT_EQ((*page)[2].url, Url(2));
  EXPECT_EQ((*page)[0].pgn, "pgn");
}

TEST_F(PgReanalysisTest, PagesAfterTheCursorExclusively) {
  for (int i = 0; i < 5; ++i) AddGame(Url(i), "pgn");
  PgGameCorpus corpus(*client_);

  auto page = corpus.After(Url(2), 10);
  ASSERT_TRUE(page.ok());
  ASSERT_EQ(page->size(), 2u);
  EXPECT_EQ((*page)[0].url, Url(3)) << "the cursor names a game already done";
}

// The property OFFSET paging could not give. A game inserted behind the
// cursor while the pass is running does not shift the window, so nothing
// ahead of it is skipped.
TEST_F(PgReanalysisTest, AGameInsertedBehindTheCursorDoesNotShiftTheWindow) {
  for (int i = 0; i < 6; ++i) AddGame(Url(i * 2), "pgn");
  PgGameCorpus corpus(*client_);

  auto first = corpus.After("", 3);
  ASSERT_TRUE(first.ok());
  const std::string cursor = first->back().url;

  AddGame(Url(1), "pgn");  // behind the cursor, mid-pass

  auto second = corpus.After(cursor, 3);
  ASSERT_TRUE(second.ok());
  ASSERT_EQ(second->size(), 3u);
  EXPECT_EQ((*second)[0].url, Url(6)) << "an offset would have re-served a row and skipped one";
}

TEST_F(PgReanalysisTest, AnEmptyCorpusPagesToNothing) {
  PgGameCorpus corpus(*client_);
  auto page = corpus.After("", 10);
  ASSERT_TRUE(page.ok());
  EXPECT_THAT(*page, IsEmpty());
}

TEST_F(PgReanalysisTest, ANullPgnComesBackEmptyRatherThanMissing) {
  ASSERT_TRUE(
      client_->Exec("INSERT INTO game_features (game_url, pgn) VALUES ($1, NULL)", {Url(0)}).ok());
  PgGameCorpus corpus(*client_);

  auto page = corpus.After("", 10);
  ASSERT_TRUE(page.ok());
  ASSERT_EQ(page->size(), 1u) << "the row is still a game the pass must account for";
  EXPECT_EQ((*page)[0].pgn, "");
}

TEST_F(PgReanalysisTest, ReplacesOccurrencesRatherThanDoublingThem) {
  AddGame(Url(0), "pgn");
  AddOccurrence(Url(0), "FORK");
  AddOccurrence(Url(0), "PIN");
  ASSERT_EQ(OccurrenceCount(Url(0)), 2);

  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink sink(*client_, id, "worker-a");

  ReanalyzedGame game;
  game.url = Url(0);
  one_d4::MotifOccurrence occurrence;
  occurrence.motif = one_d4::Motif::kFork;
  occurrence.ply = 7;
  occurrence.move_number = 4;
  game.occurrences.push_back(occurrence);

  ASSERT_TRUE(sink.Replace({game}).ok());
  EXPECT_EQ(OccurrenceCount(Url(0)), 1) << "the old rows are gone, not added to";
}

// A pass whose second look finds nothing must still clear what the game had
// — that is the whole promise of the endpoint.
TEST_F(PgReanalysisTest, AGameWithNoMotifsLosesTheRowsItHad) {
  AddGame(Url(0), "pgn");
  AddOccurrence(Url(0), "FORK");

  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink sink(*client_, id, "worker-a");

  ReanalyzedGame game;
  game.url = Url(0);
  ASSERT_TRUE(sink.Replace({game}).ok());
  EXPECT_EQ(OccurrenceCount(Url(0)), 0);
}

TEST_F(PgReanalysisTest, RefusesToWriteForAPassItNoLongerHolds) {
  AddGame(Url(0), "pgn");
  AddOccurrence(Url(0), "FORK");

  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink stranger(*client_, id, "worker-b");

  ReanalyzedGame game;
  game.url = Url(0);
  const absl::Status refused = stranger.Replace({game});
  EXPECT_EQ(refused.code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(OccurrenceCount(Url(0)), 1) << "a pass that does not hold the row rewrote it anyway";
}

TEST_F(PgReanalysisTest, RefusesToWriteOnAnExpiredLease) {
  AddGame(Url(0), "pgn");
  AddOccurrence(Url(0), "FORK");

  const std::string id = ClaimedPass("worker-a");
  ASSERT_TRUE(client_
                  ->Exec("UPDATE reanalysis_requests SET lease_expires_at = NOW() - "
                         "INTERVAL '1 minute' WHERE id = $1",
                         {id})
                  .ok());
  PgOccurrenceSink sink(*client_, id, "worker-a");

  ReanalyzedGame game;
  game.url = Url(0);
  EXPECT_EQ(sink.Replace({game}).code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(OccurrenceCount(Url(0)), 1);
}

/// One FORK for `url`, so a batch carries something the writer must place.
ReanalyzedGame OneMotif(const std::string& url, const std::string& description = "") {
  ReanalyzedGame game;
  game.url = url;
  one_d4::MotifOccurrence occurrence;
  occurrence.motif = one_d4::Motif::kFork;
  occurrence.ply = 1;
  occurrence.move_number = 1;
  occurrence.description = description;
  game.occurrences.push_back(occurrence);
  return game;
}

// The lock ordering the deadlock avoidance rests on. Handed a batch
// backwards, the sink must still touch games in url order — the indexer's
// flush takes the same FOR UPDATE locks in that order, and an inverted
// pair deadlocks the two writers against each other.
TEST_F(PgReanalysisTest, WritesABatchInGameUrlOrder) {
  for (int i = 0; i < 3; ++i) AddGame(Url(i), "pgn");
  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink sink(*client_, id, "worker-a");

  std::vector<ReanalyzedGame> batch;
  for (int i = 2; i >= 0; --i) batch.push_back(OneMotif(Url(i)));
  ASSERT_TRUE(sink.Replace(batch).ok());

  // ctid is physical write order, so this is the order rows were inserted
  // in and not the order they are read back in.
  auto written =
      client_->Exec("SELECT string_agg(game_url, ',' ORDER BY ctid) FROM motif_occurrences");
  ASSERT_TRUE(written.ok());
  EXPECT_EQ(written->Get(0, 0).value_or(""), absl::StrCat(Url(0), ",", Url(1), ",", Url(2)));
}

// One transaction, not one per game. A batch that fails partway must leave
// every game it had already rewritten exactly as it found it — otherwise a
// pass that dies mid-page has cleared occurrences it never replaced.
TEST_F(PgReanalysisTest, WritesABatchAsOneUnitOrNotAtAll) {
  for (int i = 0; i < 3; ++i) {
    AddGame(Url(i), "pgn");
    AddOccurrence(Url(i), "FORK");
  }
  // Refuses the last game's insert, after the first two have been deleted
  // and rewritten inside the same transaction.
  ASSERT_TRUE(client_
                  ->Exec("ALTER TABLE motif_occurrences ADD CONSTRAINT short_description "
                         "CHECK (description IS NULL OR length(description) < 100)")
                  .ok());

  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink sink(*client_, id, "worker-a");

  std::vector<ReanalyzedGame> batch;
  batch.push_back(OneMotif(Url(0)));
  batch.push_back(OneMotif(Url(1)));
  batch.push_back(OneMotif(Url(2), std::string(200, 'x')));

  EXPECT_FALSE(sink.Replace(batch).ok());
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(OccurrenceCount(Url(i)), 1)
        << Url(i) << " lost its rows to a batch that never landed";
  }
}

// The fence is SELECT ... FOR UPDATE, not a bare read. Under READ
// COMMITTED a plain SELECT is a snapshot: a takeover could commit between
// the check and the writes, and the batch would land against a pass this
// worker had already lost. The indexing sink gets accidental cover from
// game_features' foreign key onto its request row; reanalysis_requests has
// no such key, so nothing but this lock orders the write against a
// takeover. Twin of PgGameSinkTest.TakesTheRequestRowRatherThanReadingIt.
TEST_F(PgReanalysisTest, TakesThePassRowRatherThanReadingIt) {
  AddGame(Url(0), "pgn");
  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink sink(*client_, id, "worker-a");

  pg::Client rival(conninfo_);
  absl::Notification held;
  absl::Notification release;
  std::thread holder([&] {
    const absl::Status locked = rival.InTransaction([&](pg::Transaction& tx) {
      const auto row = tx.Exec("SELECT id FROM reanalysis_requests WHERE id = $1 FOR UPDATE", {id});
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
    ReanalyzedGame game;
    game.url = Url(0);
    EXPECT_TRUE(sink.Replace({game}).ok());
    answered = true;
  });
  // Long enough that a fence which only read would have answered by now.
  absl::SleepFor(absl::Milliseconds(250));
  const bool read_through_the_lock = answered;

  release.Notify();
  holder.join();
  asker.join();

  EXPECT_FALSE(read_through_the_lock)
      << "the fence answered while another transaction held the pass row, so it read a"
         " snapshot rather than claiming the row";
}

TEST_F(PgReanalysisTest, AnEmptyBatchIsNotAWrite) {
  const std::string id = ClaimedPass("worker-a");
  PgOccurrenceSink sink(*client_, id, "worker-a");
  EXPECT_TRUE(sink.Replace({}).ok()) << "no games is not a lost lease";
}

// The pair worker_main builds per pass. request_id and owner are two
// strings of the same type handed to the sink in that order — swapped, the
// fence never matches and every pass writes nothing while reporting
// FailedPrecondition, which nothing else here would catch.
TEST_F(PgReanalysisTest, EndsThatOwnTheirConnectionReadAndWriteUnderTheRightClaim) {
  AddGame(Url(0), "pgn");
  AddOccurrence(Url(0), "FORK");
  const std::string id = ClaimedPass("worker-a");

  const ReanalysisEnds ends =
      NewOwnedReanalysisEnds(Conninfo(std::getenv("PG_TEST_DB_URL")), id, "worker-a");

  auto page = ends.corpus->After("", 10);
  ASSERT_TRUE(page.ok());
  ASSERT_EQ(page->size(), 1u);
  EXPECT_EQ((*page)[0].url, Url(0));

  ASSERT_TRUE(ends.sink->Replace({OneMotif(Url(0))}).ok())
      << "the sink was built with its arguments the wrong way round";
  EXPECT_EQ(OccurrenceCount(Url(0)), 1);
}

TEST_F(PgReanalysisTest, EndsBuiltForAnotherOwnerAreStillFenced) {
  AddGame(Url(0), "pgn");
  AddOccurrence(Url(0), "FORK");
  const std::string id = ClaimedPass("worker-a");

  const ReanalysisEnds ends =
      NewOwnedReanalysisEnds(Conninfo(std::getenv("PG_TEST_DB_URL")), id, "worker-b");

  EXPECT_EQ(ends.sink->Replace({OneMotif(Url(0))}).code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(OccurrenceCount(Url(0)), 1);
}

}  // namespace
}  // namespace one_d4_worker
