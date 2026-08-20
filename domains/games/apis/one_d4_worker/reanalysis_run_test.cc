#include "domains/games/apis/one_d4_worker/reanalysis_run.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace one_d4_worker {
namespace {

using ::testing::IsEmpty;
using ::testing::Not;

constexpr char kScholarsMate[] =
    "[Event \"Live Chess\"]\n[White \"alice\"]\n[Black \"bob\"]\n[ECO \"C20\"]\n\n"
    "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";

/// Urls are zero-padded so lexical order — the order the keyset walk pages
/// in — matches the order they were made in.
std::string Url(int n) { return absl::StrFormat("https://chess.com/game/%04d", n); }

/// A corpus that pages by url, like the real one.
class FakeCorpus : public GameCorpus {
 public:
  void Add(const std::string& url, const std::string& pgn) { games_.push_back({url, pgn}); }

  absl::StatusOr<std::vector<StoredGame>> After(std::string_view after, int limit) override {
    if (!status_.ok()) return status_;
    ++pages_;
    std::vector<StoredGame> page;
    for (const StoredGame& game : games_) {
      if (!after.empty() && game.url <= after) continue;
      page.push_back(game);
      if (static_cast<int>(page.size()) == limit) break;
    }
    return page;
  }

  void FailWith(absl::Status status) { status_ = std::move(status); }

  std::vector<StoredGame> games_;
  absl::Status status_;
  int pages_ = 0;
};

class FakeSink : public OccurrenceSink {
 public:
  absl::Status Replace(const std::vector<ReanalyzedGame>& games) override {
    if (!status_.ok()) return status_;
    ++batches_;
    for (const ReanalyzedGame& game : games) replaced_.push_back(game);
    return absl::OkStatus();
  }

  std::vector<ReanalyzedGame> replaced_;
  absl::Status status_;
  int batches_ = 0;
};

/// A lease that is always ours and never out of time, recording every
/// checkpoint.
class FakeLease : public ReanalysisLease {
 public:
  bool Keep() override { return keep_; }
  // Separate from keep_, deliberately: the common way a pass loses its row
  // is mid-page, where Keep() already said yes and the checkpoint is what
  // refuses. Tying the two together leaves that path untested.
  bool Report(std::string_view cursor, int processed, int failed) override {
    checkpoints_.push_back({std::string(cursor), processed, failed});
    return reports_accepted_;
  }
  bool OutOfTime() override { return out_of_time_; }

  struct Checkpoint {
    std::string cursor;
    int processed;
    int failed;
  };
  std::vector<Checkpoint> checkpoints_;
  bool keep_ = true;
  bool reports_accepted_ = true;
  bool out_of_time_ = false;
};

ReanalysisRun::Options Options(int batch_size = 100) {
  ReanalysisRun::Options options;
  options.batch_size = batch_size;
  return options;
}

TEST(ReanalysisRunTest, AnEmptyCorpusFinishesImmediately) {
  FakeCorpus corpus;
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options());

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  EXPECT_EQ(report->games_processed, 0);
  EXPECT_THAT(sink.replaced_, IsEmpty());
}

TEST(ReanalysisRunTest, ReplacesEveryGameAndCountsIt) {
  FakeCorpus corpus;
  for (int i = 0; i < 3; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options());

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  EXPECT_EQ(report->games_processed, 3);
  EXPECT_EQ(report->games_failed, 0);
  EXPECT_EQ(sink.replaced_.size(), 3u);
  EXPECT_THAT(sink.replaced_[0].occurrences, Not(IsEmpty()))
      << "scholar's mate has motifs; an empty replace would delete every row it has";
}

// The whole reason for keyset paging. Each page asks for what comes after
// the last url it saw, so a row inserted behind the cursor cannot shift the
// window and hide a game the way OFFSET does.
TEST(ReanalysisRunTest, PagesThroughTheCorpusByUrl) {
  FakeCorpus corpus;
  for (int i = 0; i < 7; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options(/*batch_size=*/3));

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  EXPECT_EQ(report->games_processed, 7);
  EXPECT_EQ(sink.batches_, 3) << "3 + 3 + 1";
  EXPECT_EQ(corpus.pages_, 3)
      << "a short page is the end of the corpus; asking again costs a round trip to be "
         "told the same thing";

  std::vector<std::string> urls;
  for (const ReanalyzedGame& game : sink.replaced_) urls.push_back(game.url);
  EXPECT_EQ(urls.size(), 7u);
  EXPECT_EQ(urls.front(), Url(0));
  EXPECT_EQ(urls.back(), Url(6));
  EXPECT_EQ(report->cursor, Url(6));
}

TEST(ReanalysisRunTest, ResumesFromTheJobsCursorInsteadOfStartingOver) {
  FakeCorpus corpus;
  for (int i = 0; i < 5; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options());

  ReanalysisJob resumed;
  resumed.cursor_game_url = Url(2);
  resumed.games_processed = 3;
  resumed.games_failed = 1;

  const auto report = run.Execute(resumed, lease);
  ASSERT_TRUE(report.ok());

  std::vector<std::string> urls;
  for (const ReanalyzedGame& game : sink.replaced_) urls.push_back(game.url);
  EXPECT_EQ(urls.size(), 2u) << "games 3 and 4, not the whole corpus again";
  EXPECT_EQ(urls.front(), Url(3));

  EXPECT_EQ(report->games_processed, 5) << "the earlier pass's 3 plus this one's 2";
  EXPECT_EQ(report->games_failed, 1) << "carried, so the total is the corpus and not this run";
}

// A game whose PGN will not replay still has to be written, with nothing.
// Skipping it would leave its old occurrences in place forever — and the
// endpoint's whole promise is that the corpus matches the current detectors.
TEST(ReanalysisRunTest, AGameThatWillNotReplayIsCountedFailedAndStillCleared) {
  FakeCorpus corpus;
  corpus.Add(Url(0), kScholarsMate);
  corpus.Add(Url(1), "this is not a pgn at all");
  corpus.Add(Url(2), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options());

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  EXPECT_EQ(report->games_processed, 2);
  EXPECT_EQ(report->games_failed, 1);
  EXPECT_EQ(sink.replaced_.size(), 3u) << "the unreadable game is written too, with no motifs";

  for (const ReanalyzedGame& game : sink.replaced_) {
    if (game.url == Url(1)) {
      EXPECT_THAT(game.occurrences, IsEmpty());
    }
  }
}

TEST(ReanalysisRunTest, CheckpointsPositionAndCountsAfterEveryPage) {
  FakeCorpus corpus;
  for (int i = 0; i < 4; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options(/*batch_size=*/2));

  ASSERT_TRUE(run.Execute(ReanalysisJob{}, lease).ok());

  ASSERT_EQ(lease.checkpoints_.size(), 2u);
  EXPECT_EQ(lease.checkpoints_[0].cursor, Url(1));
  EXPECT_EQ(lease.checkpoints_[0].processed, 2);
  EXPECT_EQ(lease.checkpoints_[1].cursor, Url(3));
  EXPECT_EQ(lease.checkpoints_[1].processed, 4);
}

TEST(ReanalysisRunTest, StopsOnShutdownAndSaysSo) {
  FakeCorpus corpus;
  for (int i = 0; i < 4; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;

  ReanalysisRun::Options options = Options(/*batch_size=*/2);
  // Shutdown arrives after the first page is written and checkpointed, not
  // before the pass starts — otherwise this would pass without ever showing
  // that a partial pass keeps what it did.
  int checks = 0;
  options.stopping = [&] { return checks++ > 0; };
  ReanalysisRun run(corpus, sink, options);

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  ASSERT_TRUE(report->stopped.has_value());
  EXPECT_EQ(*report->stopped, Stopped::kShutdown);
  EXPECT_EQ(report->cursor, Url(1)) << "whoever resumes picks up after the page that landed";
}

TEST(ReanalysisRunTest, StopsAtTheRunCeiling) {
  FakeCorpus corpus;
  for (int i = 0; i < 4; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  lease.out_of_time_ = true;
  ReanalysisRun run(corpus, sink, Options());

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  ASSERT_TRUE(report->stopped.has_value());
  EXPECT_EQ(*report->stopped, Stopped::kRunCeiling)
      << "the ceiling keeps a wedged pass from holding the row forever";
}

TEST(ReanalysisRunTest, ALostLeaseStopsThePassAndIsReported) {
  FakeCorpus corpus;
  for (int i = 0; i < 4; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  lease.keep_ = false;
  ReanalysisRun run(corpus, sink, Options());

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  EXPECT_TRUE(report->lease_lost);
  EXPECT_THAT(sink.replaced_, IsEmpty()) << "a pass that no longer holds the row writes nothing";
}

// A takeover between pages: Keep() passed, the page was written, and the
// checkpoint is what the fence refuses. The pass must stop there rather
// than walk the rest of a corpus it no longer owns.
TEST(ReanalysisRunTest, ARefusedCheckpointStopsThePass) {
  FakeCorpus corpus;
  for (int i = 0; i < 6; ++i) corpus.Add(Url(i), kScholarsMate);
  FakeSink sink;
  FakeLease lease;
  lease.reports_accepted_ = false;
  ReanalysisRun run(corpus, sink, Options(/*batch_size=*/2));

  const auto report = run.Execute(ReanalysisJob{}, lease);
  ASSERT_TRUE(report.ok());
  EXPECT_TRUE(report->lease_lost);
  EXPECT_EQ(sink.batches_, 1) << "it kept paging after the fence said the row was not ours";
}

TEST(ReanalysisRunTest, TheSinksFailureIsThePassesFailure) {
  FakeCorpus corpus;
  corpus.Add(Url(0), kScholarsMate);
  FakeSink sink;
  sink.status_ = absl::UnavailableError("pg went away");
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options());

  const auto report = run.Execute(ReanalysisJob{}, lease);
  EXPECT_FALSE(report.ok());
  EXPECT_EQ(report.status().code(), absl::StatusCode::kUnavailable);
}

TEST(ReanalysisRunTest, TheCorpusFailureIsThePassesFailureToo) {
  FakeCorpus corpus;
  corpus.FailWith(absl::UnavailableError("pg went away"));
  FakeSink sink;
  FakeLease lease;
  ReanalysisRun run(corpus, sink, Options());

  EXPECT_FALSE(run.Execute(ReanalysisJob{}, lease).ok());
}

}  // namespace
}  // namespace one_d4_worker
