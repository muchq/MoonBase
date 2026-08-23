#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "domains/games/apis/one_d4_worker/claim_ref.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/queue.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

// The ceiling, through the real lease.
//
// index_run_test drives IndexRun against a LeaseKeeper that answers as
// told, which is right for the run's own rules and blind to this one: the
// ceiling is a relationship between the run's checkpoints and the lease's
// renewal, and a fake that decides both cannot show it. The first version
// of this policy passed every unit test while aborting the month in hand
// the moment the ceiling passed, because Keep() reported "stopped
// renewing" as "no longer owned".

class FakeQueue : public IndexQueue {
 public:
  absl::StatusOr<std::optional<IndexJob>> ClaimNext(
      [[maybe_unused]] std::string_view owner, [[maybe_unused]] absl::Duration lease) override {
    if (!next.has_value()) return std::nullopt;
    IndexJob job = *next;
    next.reset();
    return job;
  }

  absl::StatusOr<bool> Heartbeat(ClaimRef, [[maybe_unused]] absl::Duration lease) override {
    ++heartbeats;
    return held.load();
  }

  absl::StatusOr<bool> Progress(ClaimRef, [[maybe_unused]] int games_indexed) override {
    return held.load();
  }

  absl::StatusOr<bool> Complete(ClaimRef, int games_indexed) override {
    calls.push_back(absl::StrCat("complete ", games_indexed));
    return true;
  }

  absl::StatusOr<bool> Fail(ClaimRef, [[maybe_unused]] std::string_view message) override {
    calls.push_back("fail");
    return true;
  }

  absl::StatusOr<bool> HandBack(ClaimRef) override {
    calls.push_back("hand back");
    return true;
  }

  absl::StatusOr<bool> Release(ClaimRef) override {
    calls.push_back("release");
    return true;
  }

  std::optional<IndexJob> next;
  std::atomic<int> heartbeats{0};
  std::atomic<bool> held{true};
  std::vector<std::string> calls;
};

constexpr char kPgn[] =
    "[White \"alice\"]\n[Black \"bob\"]\n[ECO \"C20\"]\n\n"
    "1. e4 e5 2. Bc4 Nc6 3. Qh5 Nf6 4. Qxf7# 1-0\n";

ArchivedGame AGame(std::string_view url) {
  ArchivedGame game;
  game.url = std::string(url);
  game.pgn = kPgn;
  game.time_class = "blitz";
  game.white_username = "alice";
  game.black_username = "bob";
  game.white_result = "win";
  game.black_result = "checkmated";
  return game;
}

class SlowArchive : public ArchiveSource {
 public:
  absl::StatusOr<std::vector<ArchivedGame>> FetchMonth([[maybe_unused]] std::string_view player,
                                                       YearMonth month) override {
    asked.push_back(month.ToString());
    absl::SleepFor(per_month);
    const auto found = months.find(month.ToString());
    if (found == months.end()) return absl::NotFoundError("no archive");
    return found->second;
  }

  std::map<std::string, std::vector<ArchivedGame>> months;
  std::vector<std::string> asked;
  absl::Duration per_month = absl::ZeroDuration();
};

class RecordingSink : public GameSink {
 public:
  absl::Status Write(absl::Span<const IndexedGame> games) override {
    for (const IndexedGame& game : games) written.push_back(game.url);
    return absl::OkStatus();
  }

  absl::StatusOr<std::optional<int>> MonthAlreadyIndexed(
      [[maybe_unused]] const IndexedMonth& month) override {
    return std::nullopt;
  }

  absl::Status RecordMonth(const IndexedMonth& month) override {
    periods.push_back(month.month);
    return absl::OkStatus();
  }

  std::vector<std::string> written;
  std::vector<std::string> periods;
};

IndexJob AJob(std::string start, std::string end) {
  IndexJob job;
  job.id = "job-1";
  job.player = "alice";
  job.platform = "chess.com";
  job.start_month = std::move(start);
  job.end_month = std::move(end);
  job.skip_cache = true;
  return job;
}

/// A poller wired to a real IndexRun, as worker_main wires it.
Poller::Run RunOver(ArchiveSource& archive, GameSink& sink) {
  return [&archive, &sink](const Claim& claim, LeaseKeeper& lease) {
    IndexRun::Options options;
    options.batch_size = 1;
    return IndexRun(archive, sink, options).Execute(claim.job, lease);
  };
}

TEST(RunCeiling, FinishesTheMonthInHandAndThenGivesTheRangeBack) {
  // The policy in one test: the month being worked when the ceiling
  // passes is completed, the next one is never started, and the range
  // goes back with the attempt spent.
  SlowArchive archive;
  archive.months["2026-01"] = {AGame("g1"), AGame("g2"), AGame("g3")};
  archive.months["2026-02"] = {AGame("g4")};
  archive.per_month = absl::Milliseconds(150);
  RecordingSink sink;

  FakeQueue queue;
  queue.next = AJob("2026-01", "2026-02");
  Poller::Options options;
  options.owner = "worker-1";
  options.lease = absl::Seconds(30);
  options.renew_every = absl::Milliseconds(20);
  // Passes while the first month is still being fetched.
  options.max_run = absl::Milliseconds(50);

  Poller poller(queue, RunOver(archive, sink), options);
  ASSERT_TRUE(poller.PollOnce().ok());

  EXPECT_THAT(archive.asked, ElementsAre("2026-01")) << "it started a month past the ceiling";
  EXPECT_THAT(sink.written, ElementsAre("g1", "g2", "g3"))
      << "the month in hand was abandoned when the ceiling passed";
  EXPECT_THAT(sink.periods, ElementsAre("2026-01"));
  EXPECT_THAT(queue.calls, ElementsAre("release"))
      << "a ceiling stop must spend the attempt, or a repeatable wedge loops forever";
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
}

TEST(RunCeiling, StopsAskingTheQueueToExtendTheClaim) {
  SlowArchive archive;
  archive.months["2026-01"] = {AGame("g1")};
  archive.per_month = absl::Milliseconds(200);
  RecordingSink sink;

  FakeQueue queue;
  queue.next = AJob("2026-01", "2026-01");
  Poller::Options options;
  options.owner = "worker-1";
  options.lease = absl::Seconds(30);
  options.renew_every = absl::Milliseconds(20);
  options.max_run = absl::ZeroDuration();

  Poller poller(queue, RunOver(archive, sink), options);
  ASSERT_TRUE(poller.PollOnce().ok());

  EXPECT_EQ(queue.heartbeats.load(), 0)
      << "the claim was extended past the ceiling, so a wedged run keeps its range forever";
}

TEST(RunCeiling, GivesUpTheRangeWhenTheLastLeaseRunsOutMidMonthPastTheCeiling) {
  // The month in hand outlives the lease this run last proved, so it is
  // cut off at a checkpoint *inside* the month rather than at the check
  // between months. That refusal is this run's own clock running out, and
  // reporting it as a takeover would leave the row to expire still naming
  // this owner — which costs no attempt, so the wedge would repeat.
  //
  // Release is fenced, so making the claim here is safe: it lands, or it
  // is refused because the range really did change hands.
  SlowArchive archive;
  archive.months["2026-01"] = {AGame("g1"), AGame("g2"), AGame("g3")};
  // The month starts inside the ceiling and the fetch outlives both it
  // and the lease.
  archive.per_month = absl::Milliseconds(200);
  RecordingSink sink;

  FakeQueue queue;
  queue.next = AJob("2026-01", "2026-01");
  Poller::Options options;
  options.owner = "worker-1";
  options.renew_every = absl::Milliseconds(500);
  options.max_run = absl::Milliseconds(50);
  options.lease = absl::Milliseconds(60);

  Poller poller(queue, RunOver(archive, sink), options);
  ASSERT_TRUE(poller.PollOnce().ok());

  EXPECT_THAT(archive.asked, ElementsAre("2026-01")) << "the month never started";
  EXPECT_THAT(sink.periods, IsEmpty()) << "a month cut short is not a month indexed";
  EXPECT_THAT(queue.calls, ElementsAre("release"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
}

TEST(RunCeiling, ATakeoverInsideTheCeilingStillReportsNothing) {
  // The other side of the same branch: a refused checkpoint before the
  // ceiling is a range that changed hands, and this run owns none of its
  // outcome.
  SlowArchive archive;
  archive.months["2026-01"] = {AGame("g1"), AGame("g2")};
  RecordingSink sink;

  FakeQueue queue;
  queue.next = AJob("2026-01", "2026-01");
  queue.held = false;
  Poller::Options options;
  options.owner = "worker-1";
  options.lease = absl::Seconds(30);
  options.renew_every = absl::Milliseconds(20);
  options.max_run = absl::Hours(6);

  Poller poller(queue, RunOver(archive, sink), options);
  ASSERT_TRUE(poller.PollOnce().ok());

  EXPECT_THAT(queue.calls, IsEmpty());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

}  // namespace
}  // namespace one_d4_worker
