#include "domains/games/apis/one_d4_worker/reanalysis_poller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "absl/status/status.h"

namespace one_d4_worker {
namespace {

using ::testing::HasSubstr;

/// A queue that answers as told and records what it was asked.
class FakeQueue : public ReanalysisQueue {
 public:
  absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view owner,
                                                         absl::Duration) override {
    if (!claimable_) return std::nullopt;
    claimed_owner_ = std::string(owner);
    claimable_ = false;
    return job_;
  }

  absl::StatusOr<bool> Heartbeat(std::string_view, std::string_view, absl::Duration) override {
    return held_;
  }

  absl::StatusOr<bool> Progress(std::string_view, std::string_view, std::string_view cursor, int p,
                                int f) override {
    progress_.push_back({std::string(cursor), p, f});
    if (!progress_status_.ok()) return progress_status_;
    return held_;
  }

  absl::StatusOr<bool> Complete(std::string_view, std::string_view, int p, int f) override {
    completed_ = {p, f};
    return true;
  }

  absl::StatusOr<bool> Fail(std::string_view, std::string_view, std::string_view message) override {
    failed_ = std::string(message);
    return true;
  }

  absl::StatusOr<bool> HandBack(std::string_view, std::string_view) override {
    ++handed_back_;
    return true;
  }

  absl::StatusOr<bool> Release(std::string_view, std::string_view) override {
    ++released_;
    return true;
  }

  struct Checkpoint {
    std::string cursor;
    int processed;
    int failed;
  };

  ReanalysisJob job_;
  bool claimable_ = true;
  bool held_ = true;
  std::string claimed_owner_;
  absl::Status progress_status_;
  std::vector<Checkpoint> progress_;
  std::optional<std::pair<int, int>> completed_;
  std::optional<std::string> failed_;
  int handed_back_ = 0;
  int released_ = 0;
};

/// Lets a test keep its fake while the loop owns "a" queue.
class PassThrough : public ReanalysisQueue {
 public:
  explicit PassThrough(ReanalysisQueue& target) : target_(target) {}
  absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view owner,
                                                         absl::Duration lease) override {
    return target_.ClaimNext(owner, lease);
  }
  absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                 absl::Duration lease) override {
    return target_.Heartbeat(id, owner, lease);
  }
  absl::StatusOr<bool> Progress(std::string_view id, std::string_view owner,
                                std::string_view cursor, int p, int f) override {
    return target_.Progress(id, owner, cursor, p, f);
  }
  absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner, int p,
                                int f) override {
    return target_.Complete(id, owner, p, f);
  }
  absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                            std::string_view message) override {
    return target_.Fail(id, owner, message);
  }
  absl::StatusOr<bool> HandBack(std::string_view id, std::string_view owner) override {
    return target_.HandBack(id, owner);
  }
  absl::StatusOr<bool> Release(std::string_view id, std::string_view owner) override {
    return target_.Release(id, owner);
  }

 private:
  ReanalysisQueue& target_;
};

ReanalysisPoller::Options Options() {
  ReanalysisPoller::Options options;
  options.owner = "cpp/host/1";
  options.lease = absl::Seconds(30);
  options.renew_every = absl::Seconds(30);
  return options;
}

/// A run that answers with a fixed report.
ReanalysisPoller::Run Reporting(ReanalysisReport report) {
  return [report](const ReanalysisClaim&, ReanalysisLease&) -> absl::StatusOr<ReanalysisReport> {
    return report;
  };
}

TEST(ReanalysisPollerTest, DoesNothingWhenTheQueueIsEmpty) {
  FakeQueue queue;
  queue.claimable_ = false;
  ReanalysisPoller poller(queue, Reporting({}), Options());

  const auto ran = poller.PollOnce();
  ASSERT_TRUE(ran.ok());
  EXPECT_FALSE(*ran);
}

TEST(ReanalysisPollerTest, GivesEveryPassItsOwnOwnerId) {
  FakeQueue queue;
  ReanalysisPoller poller(queue, Reporting({}), Options());
  ASSERT_TRUE(poller.PollOnce().ok());
  const std::string first = queue.claimed_owner_;

  queue.claimable_ = true;
  ASSERT_TRUE(poller.PollOnce().ok());

  EXPECT_THAT(first, HasSubstr("cpp/host/1/"));
  EXPECT_NE(first, queue.claimed_owner_)
      << "reclaiming under the id already on a row spends no attempt, which is wrong when a "
         "different run of this process is still wedged on it";
}

TEST(ReanalysisPollerTest, CompletesAPassItRan) {
  FakeQueue queue;
  ReanalysisReport report;
  report.games_processed = 120;
  report.games_failed = 3;
  ReanalysisPoller poller(queue, Reporting(report), Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kCompleted);
  ASSERT_TRUE(queue.completed_.has_value());
  EXPECT_EQ(queue.completed_->first, 120);
  EXPECT_EQ(queue.completed_->second, 3);
}

TEST(ReanalysisPollerTest, FailsAPassThatRaisedWithoutLeakingTheCause) {
  FakeQueue queue;
  ReanalysisPoller poller(
      queue,
      [](const ReanalysisClaim&, ReanalysisLease&) -> absl::StatusOr<ReanalysisReport> {
        return absl::UnavailableError("could not connect to host=db.internal user=indexer");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kFailed);
  ASSERT_TRUE(queue.failed_.has_value());
  EXPECT_THAT(*queue.failed_, Not(HasSubstr("db.internal")))
      << "error_message is handed back by the API; the cause belongs in the log";
}

TEST(ReanalysisPollerTest, WritesNothingWhenTheLeaseIsLost) {
  FakeQueue queue;
  ReanalysisReport report;
  report.lease_lost = true;
  ReanalysisPoller poller(queue, Reporting(report), Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
  EXPECT_FALSE(queue.completed_.has_value());
  EXPECT_FALSE(queue.failed_.has_value());
  EXPECT_EQ(queue.handed_back_, 0);
}

TEST(ReanalysisPollerTest, HandsBackAPassItWasShutDownDuring) {
  FakeQueue queue;
  ReanalysisReport report;
  report.stopped = Stopped::kShutdown;
  report.cursor = "https://chess.com/game/0100";
  ReanalysisPoller poller(queue, Reporting(report), Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
  EXPECT_EQ(queue.handed_back_, 1) << "a shutdown is not the request's fault";
  EXPECT_EQ(queue.released_, 0);
}

// The ceiling means something different here than it does for an index
// run, and the cursor is why. A pass that advanced is making progress on a
// corpus too big for one lease, not wedged — and spending an attempt each
// time would retire the request after three, before it ever reached the
// end.
TEST(ReanalysisPollerTest, ACeilingThatAdvancedTheCursorRefundsTheAttempt) {
  FakeQueue queue;
  queue.job_.cursor_game_url = "https://chess.com/game/0100";
  ReanalysisReport report;
  report.stopped = Stopped::kRunCeiling;
  report.cursor = "https://chess.com/game/0900";
  ReanalysisPoller poller(queue, Reporting(report), Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
  EXPECT_EQ(queue.handed_back_, 1);
  EXPECT_EQ(queue.released_, 0);
}

TEST(ReanalysisPollerTest, ACeilingThatMovedNothingSpendsTheAttempt) {
  FakeQueue queue;
  queue.job_.cursor_game_url = "https://chess.com/game/0100";
  ReanalysisReport report;
  report.stopped = Stopped::kRunCeiling;
  report.cursor = "https://chess.com/game/0100";
  ReanalysisPoller poller(queue, Reporting(report), Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(queue.released_, 1)
      << "six hours without moving one game is a wedge, and refunding it retries forever";
  EXPECT_EQ(queue.handed_back_, 0);
}

TEST(ReanalysisPollerTest, ResumesFromWhereTheRowSaysItGotTo) {
  FakeQueue queue;
  queue.job_.cursor_game_url = "https://chess.com/game/0500";
  queue.job_.games_processed = 500;

  std::string seen_cursor;
  ReanalysisPoller poller(
      queue,
      [&](const ReanalysisClaim& claim, ReanalysisLease&) -> absl::StatusOr<ReanalysisReport> {
        seen_cursor = claim.job.cursor_game_url;
        return ReanalysisReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(seen_cursor, "https://chess.com/game/0500")
      << "the run is handed the claim, not just the id, because where to resume is on the row";
}

// What the observer is handed is this owner's share, not the row's totals:
// a resumed pass finishing under a new owner reports totals the earlier
// owner already counted, and a dashboard summing both would double them.
TEST(ReanalysisPollerTest, ReportsTheOutcomeAndThisOwnersShareOfTheGames) {
  FakeQueue queue;
  queue.job_.cursor_game_url = "https://chess.com/game/0500";
  queue.job_.games_processed = 500;
  queue.job_.games_failed = 10;

  ReanalysisReport report;
  report.games_processed = 620;
  report.games_failed = 13;

  ReanalysisPoller::Options options = Options();
  std::vector<std::tuple<RunOutcome, int, int>> finished;
  options.on_finished = [&](RunOutcome outcome, int processed, int failed) {
    finished.push_back({outcome, processed, failed});
  };
  ReanalysisPoller poller(queue, Reporting(report), options);

  ASSERT_TRUE(poller.PollOnce().ok());
  ASSERT_EQ(finished.size(), 1u);
  EXPECT_EQ(std::get<0>(finished[0]), RunOutcome::kCompleted);
  EXPECT_EQ(std::get<1>(finished[0]), 120) << "620 total minus the 500 an earlier owner counted";
  EXPECT_EQ(std::get<2>(finished[0]), 3);
}

// The branch a large corpus is guaranteed to hit. A ceiling stint that is
// not observed loses its games from the series forever: the next owner's
// carried baseline already includes them, so its delta excludes them, and
// this owner never exported them.
TEST(ReanalysisPollerTest, ACeilingStintReportsItsOutcomeAndItsShare) {
  FakeQueue queue;
  queue.job_.cursor_game_url = "https://chess.com/game/0100";
  queue.job_.games_processed = 100;

  ReanalysisReport report;
  report.stopped = Stopped::kRunCeiling;
  report.cursor = "https://chess.com/game/0900";
  report.games_processed = 900;

  ReanalysisPoller::Options options = Options();
  std::vector<std::tuple<RunOutcome, int, int>> finished;
  options.on_finished = [&](RunOutcome outcome, int processed, int failed) {
    finished.push_back({outcome, processed, failed});
  };
  ReanalysisPoller poller(queue, Reporting(report), options);

  ASSERT_TRUE(poller.PollOnce().ok());
  ASSERT_EQ(finished.size(), 1u);
  EXPECT_EQ(std::get<0>(finished[0]), RunOutcome::kInterrupted);
  EXPECT_EQ(std::get<1>(finished[0]), 800);
}

TEST(ReanalysisPollerTest, AShutdownHandBackReportsItsShareToo) {
  FakeQueue queue;
  queue.job_.games_processed = 100;
  ReanalysisReport report;
  report.stopped = Stopped::kShutdown;
  report.games_processed = 150;

  ReanalysisPoller::Options options = Options();
  std::vector<std::tuple<RunOutcome, int, int>> finished;
  options.on_finished = [&](RunOutcome outcome, int processed, int failed) {
    finished.push_back({outcome, processed, failed});
  };
  ReanalysisPoller poller(queue, Reporting(report), options);

  ASSERT_TRUE(poller.PollOnce().ok());
  ASSERT_EQ(finished.size(), 1u);
  EXPECT_EQ(std::get<0>(finished[0]), RunOutcome::kInterrupted);
  EXPECT_EQ(std::get<1>(finished[0]), 50);
}

TEST(ReanalysisPollerTest, ALostLeaseStillReportsItsOutcome) {
  FakeQueue queue;
  ReanalysisReport report;
  report.lease_lost = true;
  ReanalysisPoller::Options options = Options();
  std::vector<RunOutcome> outcomes;
  options.on_finished = [&](RunOutcome outcome, int, int) { outcomes.push_back(outcome); };
  ReanalysisPoller poller(queue, Reporting(report), options);

  ASSERT_TRUE(poller.PollOnce().ok());
  ASSERT_EQ(outcomes.size(), 1u);
  EXPECT_EQ(outcomes[0], RunOutcome::kLeaseLost);
}

// A terminal write the queue could not answer is not an outcome — the row
// expires and somebody retries, and whoever finishes it then reports it.
// Emitting here too would count that pass twice.
TEST(ReanalysisPollerTest, AQueueErrorOnTheTerminalWriteEmitsNothing) {
  class BrokenComplete : public FakeQueue {
   public:
    absl::StatusOr<bool> Complete(std::string_view, std::string_view, int, int) override {
      return absl::UnavailableError("pg went away");
    }
  };
  BrokenComplete queue;
  ReanalysisPoller::Options options = Options();
  int calls = 0;
  options.on_finished = [&](RunOutcome, int, int) { ++calls; };
  ReanalysisPoller poller(queue, Reporting({}), options);

  EXPECT_FALSE(poller.PollOnce().ok());
  EXPECT_EQ(calls, 0);
}

TEST(ReanalysisPollerTest, AFailedPassStillReportsItsOutcome) {
  FakeQueue queue;
  ReanalysisPoller::Options options = Options();
  std::vector<RunOutcome> outcomes;
  options.on_finished = [&](RunOutcome outcome, int, int) { outcomes.push_back(outcome); };
  ReanalysisPoller poller(
      queue,
      [](const ReanalysisClaim&, ReanalysisLease&) -> absl::StatusOr<ReanalysisReport> {
        return absl::UnavailableError("boom");
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  ASSERT_EQ(outcomes.size(), 1u);
  EXPECT_EQ(outcomes[0], RunOutcome::kFailed);
}

TEST(ReanalysisPollerTest, ARefusedCompleteMeansTheLeaseWentSomewhereElse) {
  class RefusingQueue : public FakeQueue {
   public:
    absl::StatusOr<bool> Complete(std::string_view, std::string_view, int, int) override {
      return false;
    }
  };
  RefusingQueue queue;
  ReanalysisPoller poller(queue, Reporting({}), Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(ReanalysisPollerTest, ReportsAQueueThatWillNotAnswer) {
  class BrokenQueue : public FakeQueue {
   public:
    absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view,
                                                           absl::Duration) override {
      return absl::UnavailableError("pg went away");
    }
  };
  BrokenQueue queue;
  ReanalysisPoller poller(queue, Reporting({}), Options());

  EXPECT_FALSE(poller.PollOnce().ok());
}

TEST(ReanalysisPollerTest, PassesTheRunsProgressStraightToTheQueue) {
  FakeQueue queue;
  ReanalysisPoller poller(
      queue,
      [](const ReanalysisClaim&, ReanalysisLease& lease) -> absl::StatusOr<ReanalysisReport> {
        EXPECT_TRUE(lease.Report("https://chess.com/game/0007", 7, 1));
        return ReanalysisReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  ASSERT_EQ(queue.progress_.size(), 1u);
  EXPECT_EQ(queue.progress_[0].cursor, "https://chess.com/game/0007");
  EXPECT_EQ(queue.progress_[0].processed, 7);
  EXPECT_EQ(queue.progress_[0].failed, 1);
}

// A queue that cannot be reached is not proof the claim was lost — the
// rule LeaseCore keeps for renewals, and until now pinned for renewals
// only. Both pollers share that code, so this covers the index one too.
TEST(ReanalysisPollerTest, ACheckpointAgainstAQueueItCannotReachIsNotALostClaim) {
  FakeQueue queue;
  queue.progress_status_ = absl::UnavailableError("pg went away");
  bool still_ours = false;
  ReanalysisPoller poller(
      queue,
      [&](const ReanalysisClaim&, ReanalysisLease& lease) -> absl::StatusOr<ReanalysisReport> {
        still_ours = lease.Report("https://chess.com/game/0001", 1, 0);
        return ReanalysisReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_TRUE(still_ours)
      << "one blip abandons a pass nobody else wants, mid-corpus; the benefit of the doubt "
         "runs out when the lease we last proved would have expired, not on the first error";
}

TEST(ReanalysisPollerTest, ARefusedProgressWriteIsALostClaim) {
  FakeQueue queue;
  queue.held_ = false;
  ReanalysisPoller poller(
      queue,
      [](const ReanalysisClaim&, ReanalysisLease& lease) -> absl::StatusOr<ReanalysisReport> {
        ReanalysisReport report;
        report.lease_lost = !lease.Report("https://chess.com/game/0001", 1, 0);
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
  EXPECT_FALSE(queue.completed_.has_value());
}

// The loop around the poller. Its own thread in production, so what
// matters here is that it stops when told, survives a queue it cannot
// reach, and does not sleep between passes it could be running.

TEST(ReanalysisLoopTest, StopsWhenTold) {
  FakeQueue queue;
  bool stop = false;
  int polls = 0;
  PollReanalysisUntilStopped(
      [&] { return std::unique_ptr<ReanalysisQueue>(new PassThrough(queue)); },
      [&](const ReanalysisClaim&, ReanalysisLease&) -> absl::StatusOr<ReanalysisReport> {
        ++polls;
        stop = true;
        return ReanalysisReport{};
      },
      Options(), [&] { return stop; }, [](absl::Duration) {}, absl::Seconds(0));
  EXPECT_EQ(polls, 1);
}

TEST(ReanalysisLoopTest, SleepsOnlyWhenThereWasNothingToTake) {
  FakeQueue queue;
  queue.claimable_ = false;
  int slept = 0;
  bool stop = false;
  PollReanalysisUntilStopped(
      [&] { return std::unique_ptr<ReanalysisQueue>(new PassThrough(queue)); }, Reporting({}),
      Options(), [&] { return stop; },
      [&](absl::Duration) {
        ++slept;
        stop = true;
      },
      absl::Seconds(5));
  EXPECT_EQ(slept, 1);
}

// The other half of "only". A pass that ran may be resumable right now —
// sleeping after one would idle a worker for no reason.
TEST(ReanalysisLoopTest, DoesNotSleepAfterAPassItActuallyRan) {
  FakeQueue queue;
  int slept = 0;
  bool stop = false;
  PollReanalysisUntilStopped(
      [&] { return std::unique_ptr<ReanalysisQueue>(new PassThrough(queue)); },
      [&](const ReanalysisClaim&, ReanalysisLease&) -> absl::StatusOr<ReanalysisReport> {
        stop = true;
        return ReanalysisReport{};
      },
      Options(), [&] { return stop; }, [&](absl::Duration) { ++slept; }, absl::Seconds(5));
  EXPECT_EQ(slept, 0);
}

TEST(ReanalysisLoopTest, KeepsGoingThroughAQueueItCannotReach) {
  class BrokenQueue : public FakeQueue {
   public:
    absl::StatusOr<std::optional<ReanalysisJob>> ClaimNext(std::string_view,
                                                           absl::Duration) override {
      ++asked_;
      return absl::UnavailableError("pg went away");
    }
    int asked_ = 0;
  };
  BrokenQueue queue;
  bool stop = false;
  PollReanalysisUntilStopped(
      [&] { return std::unique_ptr<ReanalysisQueue>(new PassThrough(queue)); }, Reporting({}),
      Options(), [&] { return stop; },
      [&](absl::Duration) {
        if (queue.asked_ >= 3) stop = true;
      },
      absl::Seconds(0));
  EXPECT_GE(queue.asked_, 3) << "the row and its cursor are still there; a blip is not an exit";
}

}  // namespace
}  // namespace one_d4_worker
