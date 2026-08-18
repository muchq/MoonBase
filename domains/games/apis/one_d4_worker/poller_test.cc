#include "domains/games/apis/one_d4_worker/poller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/queue.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

/// Records what the poller asked of the queue, and answers as told.
class FakeQueue : public IndexQueue {
 public:
  absl::StatusOr<std::optional<IndexJob>> ClaimNext(std::string_view owner,
                                                    absl::Duration lease) override {
    ++claims;
    if (claim_fails) return absl::UnavailableError("queue is down");
    if (!next.has_value()) return std::nullopt;
    IndexJob job = *next;
    next.reset();
    return job;
  }

  absl::StatusOr<bool> Heartbeat(std::string_view id, std::string_view owner,
                                 absl::Duration lease) override {
    ++heartbeats;
    return lease_held;
  }

  absl::StatusOr<bool> Complete(std::string_view id, std::string_view owner,
                                int games_indexed) override {
    calls.push_back(absl::StrCat("complete ", id, " ", games_indexed));
    return terminal_write_wins;
  }

  absl::StatusOr<bool> Fail(std::string_view id, std::string_view owner,
                            std::string_view message) override {
    calls.push_back(absl::StrCat("fail ", id, " ", message));
    return terminal_write_wins;
  }

  absl::StatusOr<bool> HandBack(std::string_view id, std::string_view owner) override {
    calls.push_back(absl::StrCat("hand back ", id));
    return true;
  }

  absl::StatusOr<bool> Release(std::string_view id, std::string_view owner) override {
    calls.push_back(absl::StrCat("release ", id));
    return true;
  }

  std::optional<IndexJob> next;
  bool lease_held = true;
  bool claim_fails = false;
  bool terminal_write_wins = true;
  int claims = 0;
  int heartbeats = 0;
  std::vector<std::string> calls;
};

IndexJob AJob() {
  IndexJob job;
  job.id = "job-1";
  job.player = "hikaru";
  job.platform = "chess.com";
  job.start_month = "2026-01";
  job.end_month = "2026-01";
  return job;
}

Poller::Options Options() {
  Poller::Options options;
  options.owner = "worker-1";
  options.lease = absl::Minutes(5);
  return options;
}

TEST(Poller, DoesNothingWhenTheQueueIsEmpty) {
  FakeQueue queue;
  Poller poller(queue, [](const IndexJob&, LeaseKeeper&) { return RunReport{}; }, Options());

  const absl::StatusOr<bool> worked = poller.PollOnce();
  ASSERT_TRUE(worked.ok()) << worked.status();
  EXPECT_FALSE(*worked);
  EXPECT_THAT(queue.calls, IsEmpty());
}

TEST(Poller, CompletesAJobItRan) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const IndexJob& job, LeaseKeeper&) {
        RunReport report;
        report.games_indexed = 42;
        return report;
      },
      Options());

  const absl::StatusOr<bool> worked = poller.PollOnce();
  ASSERT_TRUE(worked.ok()) << worked.status();
  EXPECT_TRUE(*worked);
  EXPECT_THAT(queue.calls, ElementsAre("complete job-1 42"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kCompleted);
}

TEST(Poller, FailsAJobThatRaised) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        return absl::InternalError("chess.com said no");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("fail job-1 chess.com said no"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kFailed);
}

TEST(Poller, WritesNothingWhenTheLeaseIsLost) {
  // The row belongs to whoever holds the lease now, and they own its
  // outcome. Reporting ours would overwrite theirs — this is the whole
  // point of fencing every terminal write on ownership.
  FakeQueue queue;
  queue.next = AJob();
  queue.lease_held = false;
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        EXPECT_FALSE(lease.Keep());
        RunReport report;
        report.lease_lost = true;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, IsEmpty());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, HandsBackAJobItWasShutDownDuring) {
  // Stopped, not failed. Handing the row back frees it immediately instead
  // of stranding the range until the lease expires, and refunds the attempt
  // — a shutdown is not the request's fault.
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper&) {
        RunReport report;
        report.stopped = Stopped::kShutdown;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("hand back job-1"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
}

TEST(Poller, ReleasesAJobThatRanOutOfTime) {
  // The run hit its own ceiling rather than being told to stop, so the
  // attempt stays spent: something about this range takes too long, and
  // refunding it would retry forever.
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper&) {
        RunReport report;
        report.stopped = Stopped::kRunCeiling;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("release job-1"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
}

TEST(Poller, DoesNotFailARunThatLostItsLeaseBeforeItRaised) {
  // The rule is "a run which lost its lease reports nothing", and an error
  // on the way out is still nothing to report: the row is somebody else's.
  FakeQueue queue;
  queue.next = AJob();
  queue.lease_held = false;
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        EXPECT_FALSE(lease.Keep());
        return absl::InternalError("and then it fell over");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, IsEmpty());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, ARefusedCompleteMeansTheLeaseWentSomewhereElse) {
  // The fence answered no, so somebody else holds the row and has already
  // written, or will. Calling this run completed would claim credit for an
  // outcome we did not write.
  FakeQueue queue;
  queue.next = AJob();
  queue.terminal_write_wins = false;
  Poller poller(queue, [](const IndexJob&, LeaseKeeper&) { return RunReport{}; }, Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, ARefusedFailMeansTheLeaseWentSomewhereElseToo) {
  FakeQueue queue;
  queue.next = AJob();
  queue.terminal_write_wins = false;
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        return absl::InternalError("chess.com said no");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, ReportsAQueueThatWillNotAnswer) {
  FakeQueue queue;
  queue.claim_fails = true;
  Poller poller(queue, [](const IndexJob&, LeaseKeeper&) { return RunReport{}; }, Options());

  EXPECT_EQ(poller.PollOnce().status().code(), absl::StatusCode::kUnavailable);
}

TEST(Poller, KeepsTheLeaseWhileTheRunWorks) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const IndexJob&, LeaseKeeper& lease) {
        for (int i = 0; i < 3; ++i) EXPECT_TRUE(lease.Keep());
        return RunReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(queue.heartbeats, 3);
}

}  // namespace
}  // namespace one_d4_worker
