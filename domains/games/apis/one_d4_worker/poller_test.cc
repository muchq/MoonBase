#include "domains/games/apis/one_d4_worker/poller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <string>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "domains/games/apis/one_d4_worker/claim_ref.h"
#include "domains/games/apis/one_d4_worker/poller_options.h"
#include "domains/games/apis/one_d4_worker/queue.h"

namespace one_d4_worker {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

/// Records what the poller asked of the queue, and answers as told.
class FakeQueue : public IndexQueue {
 public:
  absl::StatusOr<std::optional<IndexJob>> ClaimNext(
      std::string_view owner, [[maybe_unused]] absl::Duration lease) override {
    ++claims;
    owners.push_back(std::string(owner));
    if (claim_fails) return absl::UnavailableError("queue is down");
    if (!next.has_value()) return std::nullopt;
    IndexJob job = *next;
    next.reset();
    return job;
  }

  absl::StatusOr<bool> Heartbeat(ClaimRef claim, [[maybe_unused]] absl::Duration lease) override {
    ++heartbeats;
    {
      const absl::MutexLock lock(fence_mu);
      fenced_on.push_back(std::string(claim.owner));
    }
    if (heartbeat_fails) return absl::UnavailableError("queue is down");
    return lease_held.load();
  }

  absl::StatusOr<bool> Progress(ClaimRef claim, int games_indexed) override {
    progress.push_back(games_indexed);
    {
      const absl::MutexLock lock(fence_mu);
      fenced_on.push_back(std::string(claim.owner));
    }
    return progress_accepted.load();
  }

  absl::StatusOr<bool> Complete(ClaimRef claim, int games_indexed) override {
    calls.push_back(absl::StrCat("complete ", claim.id, " ", games_indexed));
    {
      const absl::MutexLock lock(fence_mu);
      fenced_on.push_back(std::string(claim.owner));
    }
    return terminal_write_wins;
  }

  absl::StatusOr<bool> Fail(ClaimRef claim, std::string_view message) override {
    calls.push_back(absl::StrCat("fail ", claim.id, " ", message));
    {
      const absl::MutexLock lock(fence_mu);
      fenced_on.push_back(std::string(claim.owner));
    }
    return terminal_write_wins;
  }

  absl::StatusOr<bool> HandBack(ClaimRef claim) override {
    calls.push_back(absl::StrCat("hand back ", claim.id));
    {
      const absl::MutexLock lock(fence_mu);
      fenced_on.push_back(std::string(claim.owner));
    }
    return true;
  }

  absl::StatusOr<bool> Release(ClaimRef claim) override {
    calls.push_back(absl::StrCat("release ", claim.id));
    {
      const absl::MutexLock lock(fence_mu);
      fenced_on.push_back(std::string(claim.owner));
    }
    return true;
  }

  std::optional<IndexJob> next;
  std::atomic<bool> lease_held{true};
  bool claim_fails = false;
  std::atomic<bool> heartbeat_fails{false};
  std::vector<int> progress;
  std::atomic<bool> progress_accepted{true};
  bool terminal_write_wins = true;
  int claims = 0;
  std::atomic<int> heartbeats{0};
  std::vector<std::string> calls;
  std::vector<std::string> owners;

  /// Every id a fenced write was made under, from the run thread and the
  /// renewer alike.
  std::vector<std::string> FencedOn() const {
    const absl::MutexLock lock(fence_mu);
    return fenced_on;
  }
  mutable absl::Mutex fence_mu;
  std::vector<std::string> fenced_on ABSL_GUARDED_BY(fence_mu);
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

/// The startup wiring, which lives in a function precisely so that a target
/// compiles it: main has none. Every window distinct and none of them the
/// defaults, so a mapping that crossed two fields — or dropped them and left
/// the defaults standing — cannot satisfy this by coincidence.
TEST(PollerOptionsFromTest, TakesEachWindowFromItsOwnFieldOfThePolicy) {
  RetentionPolicy policy;
  policy.period = absl::Seconds(1);
  policy.request = absl::Seconds(2);
  policy.stale_request = absl::Seconds(3);
  policy.lease = absl::Seconds(11);
  policy.lease_renewal = absl::Seconds(22);
  policy.max_run = absl::Seconds(33);
  policy.statement_timeout = absl::Seconds(44);

  const Poller::Options options = PollerOptionsFrom(policy, "worker-9");

  EXPECT_EQ(options.lease, absl::Seconds(11));
  EXPECT_EQ(options.renew_every, absl::Seconds(22));
  EXPECT_EQ(options.max_run, absl::Seconds(33));
  EXPECT_EQ(options.owner, "worker-9");
}

Poller::Options Options() {
  Poller::Options options;
  options.owner = "worker-1";
  options.lease = absl::Minutes(5);
  return options;
}

/// Every fenced write went out under the id the run claimed with. A
/// write fenced on anything else is refused by the database, and the run
/// is told it lost a lease it never lost.
void ExpectEveryWriteFencedOnTheClaim(const FakeQueue& queue) {
  ASSERT_EQ(queue.owners.size(), 1u);
  const std::vector<std::string> fenced = queue.FencedOn();
  ASSERT_THAT(fenced, ::testing::Not(IsEmpty()));
  for (const std::string& one : fenced) EXPECT_EQ(one, queue.owners[0]);
}

TEST(Poller, GivesEveryRunItsOwnOwnerId) {
  // Two runs of one process must not share one. Reclaiming a row under
  // the id that holds it spends no attempt — right when the run holding
  // it has ended, wrong when a second run is still wedged on it, and with
  // a pool that is the ordinary case. A request that wedges every run it
  // touches would never reach kMaxAttempts and nothing would retire it.
  FakeQueue queue;
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, Options());

  queue.next = AJob();
  ASSERT_TRUE(poller.PollOnce().ok());
  queue.next = AJob();
  ASSERT_TRUE(poller.PollOnce().ok());

  ASSERT_EQ(queue.owners.size(), 2u);
  EXPECT_NE(queue.owners[0], queue.owners[1]);
  EXPECT_THAT(queue.owners[0], ::testing::StartsWith("worker-1/"))
      << "the process is still named, so a stuck row says which one held it";
}

TEST(Poller, FencesTheLeaseOnTheIdItClaimedWithToo) {
  // The renewal is the one that would be silent. Fenced on the process
  // name instead, every heartbeat is refused at the database, every run
  // reports a lost lease, and the worker indexes nothing while looking
  // like it is losing races.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.renew_every = absl::Milliseconds(20);
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) {
        absl::SleepFor(absl::Milliseconds(120));
        lease.Report(1);
        return RunReport{};
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());

  ASSERT_EQ(queue.owners.size(), 1u);
  ASSERT_GT(queue.heartbeats.load(), 0) << "no renewal happened, so nothing was fenced";
  for (const std::string& fenced : queue.FencedOn()) {
    EXPECT_EQ(fenced, queue.owners[0]);
  }
}

TEST(Poller, FencesARunsWritesOnTheIdItClaimedWith) {
  // An id that changed mid-run would fence the run out of its own row.
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, Options());

  ASSERT_TRUE(poller.PollOnce().ok());

  ASSERT_EQ(queue.owners.size(), 1u);
  EXPECT_THAT(queue.FencedOn(), ElementsAre(queue.owners[0]));
}

TEST(Poller, ClaimsWithoutRunning) {
  // The two halves separate because the pool claims on one thread and
  // runs on another. A claim is not work started.
  FakeQueue queue;
  queue.next = AJob();
  int runs = 0;
  Poller poller(
      queue,
      [&runs](const Claim&, LeaseKeeper&) {
        ++runs;
        return RunReport{};
      },
      Options());

  const absl::StatusOr<std::optional<Claim>> claim = poller.ClaimOne();

  ASSERT_TRUE(claim.ok()) << claim.status();
  ASSERT_TRUE(claim->has_value());
  EXPECT_EQ((*claim)->job.id, "job-1");
  EXPECT_EQ(runs, 0);
  EXPECT_THAT(queue.calls, IsEmpty()) << "a claim on its own writes no outcome";
}

TEST(Poller, RunsAClaimAndWritesItsOutcome) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper&) {
        RunReport report;
        report.games_indexed = 3;
        return report;
      },
      Options());
  const absl::StatusOr<std::optional<Claim>> claim = poller.ClaimOne();
  ASSERT_TRUE(claim.ok() && claim->has_value()) << claim.status();

  const absl::StatusOr<RunOutcome> outcome = poller.RunClaimed(**claim);

  ASSERT_TRUE(outcome.ok()) << outcome.status();
  EXPECT_EQ(*outcome, RunOutcome::kCompleted);
  EXPECT_THAT(queue.calls, ElementsAre("complete job-1 3"));
}

TEST(Poller, TellsTheRunWhichIdItMustFenceOn) {
  // A run's sink fences its writes on the owner id, and the id is minted
  // per claim — so a run told the process's name instead would have
  // every write refused and index nothing.
  FakeQueue queue;
  queue.next = AJob();
  std::string seen;
  Poller poller(
      queue,
      [&seen](const Claim& claim, LeaseKeeper&) {
        seen = claim.owner;
        return RunReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());

  ASSERT_EQ(queue.owners.size(), 1u);
  EXPECT_EQ(seen, queue.owners[0]);
}

TEST(Poller, DoesNothingWhenTheQueueIsEmpty) {
  FakeQueue queue;
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, Options());

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
      []([[maybe_unused]] const Claim& claim, LeaseKeeper&) {
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
      [](const Claim&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        return absl::InternalError("chess.com said no");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  // The cause is logged, not stored: error_message is handed back by the
  // API, so a chess.com body or a libpq diagnostic in there is an
  // internal detail told to whoever asked for the index.
  EXPECT_THAT(queue.calls, ElementsAre("fail job-1 Indexing failed due to an internal error"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kFailed);
  ExpectEveryWriteFencedOnTheClaim(queue);
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
      [](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
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
      [](const Claim&, LeaseKeeper&) {
        RunReport report;
        report.stopped = Stopped::kShutdown;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("hand back job-1"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
  ExpectEveryWriteFencedOnTheClaim(queue);
}

TEST(Poller, ReleasesAJobThatRanOutOfTime) {
  // The run hit its own ceiling rather than being told to stop, so the
  // attempt stays spent: something about this range takes too long, and
  // refunding it would retry forever.
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper&) {
        RunReport report;
        report.stopped = Stopped::kRunCeiling;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("release job-1"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
  ExpectEveryWriteFencedOnTheClaim(queue);
}

TEST(Poller, DoesNotFailARunThatLostItsLeaseBeforeItRaised) {
  // The rule is "a run which lost its lease reports nothing", and an error
  // on the way out is still nothing to report: the row is somebody else's.
  FakeQueue queue;
  queue.next = AJob();
  queue.lease_held = false;
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
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
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, ARefusedFailMeansTheLeaseWentSomewhereElseToo) {
  FakeQueue queue;
  queue.next = AJob();
  queue.terminal_write_wins = false;
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        return absl::InternalError("chess.com said no");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, ReportsAQueueThatWillNotAnswer) {
  FakeQueue queue;
  queue.claim_fails = true;
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, Options());

  EXPECT_EQ(poller.PollOnce().status().code(), absl::StatusCode::kUnavailable);
}

TEST(Poller, KeepsTheLeaseWhileTheRunWorks) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) {
        for (int i = 0; i < 3; ++i) EXPECT_TRUE(lease.Keep());
        return RunReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(queue.heartbeats, 3);
}

TEST(Poller, RenewsTheLeaseWithoutBeingAsked) {
  // The gaps between a run's checkpoints are longer than a lease. A month
  // of four hundred games is four archive calls, eight hundred profile
  // lookups and four hundred extractions between them.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.lease = absl::Milliseconds(400);
  options.renew_every = absl::Milliseconds(50);
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper&) {
        // Works, and never asks.
        absl::SleepFor(absl::Milliseconds(300));
        return RunReport{};
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_GE(queue.heartbeats.load(), 3) << "the lease was never renewed on its own";
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kCompleted);
}

TEST(Poller, NoticesALeaseTakenWhileItWasWorking) {
  // The renewal is also how a takeover is heard about between checkpoints.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.lease = absl::Milliseconds(400);
  options.renew_every = absl::Milliseconds(50);
  Poller poller(
      queue,
      [&](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        queue.lease_held = false;
        absl::SleepFor(absl::Milliseconds(200));
        EXPECT_FALSE(lease.Keep());
        RunReport report;
        report.lease_lost = true;
        return report;
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, IsEmpty());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, KeepsWorkingThroughAQueueItCannotReachForAMoment) {
  // A blip is not proof the claim is gone, and giving up on the first one
  // abandons a run nobody else wants, mid-way. Every write is fenced on
  // the row itself, so carrying on is safe.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.lease = absl::Seconds(30);
  options.renew_every = absl::Milliseconds(50);
  Poller poller(
      queue,
      [&](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        queue.heartbeat_fails = true;
        absl::SleepFor(absl::Milliseconds(200));
        EXPECT_TRUE(lease.Keep()) << "one unreachable moment ended the run";
        queue.heartbeat_fails = false;
        return RunReport{};
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kCompleted);
}

TEST(Poller, GivesUpOnceTheLeaseItLastProvedWouldHaveExpired) {
  // The benefit of the doubt runs out. Past that point the claim cannot be
  // shown to be ours, and another worker is entitled to it.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.lease = absl::Milliseconds(150);
  options.renew_every = absl::Milliseconds(25);
  Poller poller(
      queue,
      [&](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        queue.heartbeat_fails = true;
        absl::SleepFor(absl::Milliseconds(400));
        EXPECT_FALSE(lease.Keep());
        RunReport report;
        report.lease_lost = true;
        return report;
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, IsEmpty());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, PassesTheRunsProgressStraightToTheQueue) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) {
        EXPECT_TRUE(lease.Report(12));
        EXPECT_TRUE(lease.Report(31));
        RunReport report;
        report.games_indexed = 31;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.progress, ElementsAre(12, 31));
  EXPECT_THAT(queue.calls, ElementsAre("complete job-1 31"));
}

TEST(Poller, ARefusedProgressWriteIsALostClaim) {
  // Fenced on the same terms as everything else, so a refusal means the
  // row is somebody else's — and the run must not report an outcome for it.
  FakeQueue queue;
  queue.next = AJob();
  queue.progress_accepted = false;
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        EXPECT_FALSE(lease.Report(12));
        EXPECT_FALSE(lease.Keep()) << "the claim stays lost once it is lost";
        RunReport report;
        report.lease_lost = true;
        return report;
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, IsEmpty());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kLeaseLost);
}

TEST(Poller, GivesTheRangeBackWhenARunHitsItsCeiling) {
  // The attempt stays spent, unlike a shutdown. A run that has been going
  // longer than any legitimate run is evidence of a fault, and refunding
  // it would retry that fault forever.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.max_run = absl::ZeroDuration();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) {
        EXPECT_TRUE(lease.OutOfTime());
        RunReport report;
        report.stopped = Stopped::kRunCeiling;
        return report;
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("release job-1"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kInterrupted);
}

TEST(Poller, StopsRenewingARunThatIsPastItsCeilingWithoutDisowningIt) {
  // Two things at once, because conflating them is the bug this replaced:
  // past the ceiling the claim is not extended — so a run wedged inside
  // one month loses its range to the lease lapsing — but it is still
  // *held* until that lease runs out, and the month in hand may finish
  // inside it.
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.lease = absl::Seconds(30);
  options.renew_every = absl::Milliseconds(25);
  options.max_run = absl::ZeroDuration();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) {
        absl::SleepFor(absl::Milliseconds(150));
        EXPECT_TRUE(lease.Keep()) << "the month in hand was cut off at the ceiling";
        EXPECT_TRUE(lease.OutOfTime());
        RunReport report;
        report.stopped = Stopped::kRunCeiling;
        return report;
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(queue.heartbeats.load(), 0) << "the queue was asked to renew past the ceiling";
  EXPECT_THAT(queue.calls, ElementsAre("release job-1"));
}

TEST(Poller, DisownsARunWhoseLastLeaseRanOutPastTheCeiling) {
  FakeQueue queue;
  queue.next = AJob();
  Poller::Options options = Options();
  options.lease = absl::ZeroDuration();
  options.renew_every = absl::Milliseconds(25);
  options.max_run = absl::ZeroDuration();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) -> absl::StatusOr<RunReport> {
        EXPECT_FALSE(lease.Keep());
        RunReport report;
        report.stopped = Stopped::kRunCeiling;
        return report;
      },
      options);

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("release job-1"));
}

TEST(Poller, LeavesARunInsideItsCeilingAlone) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper& lease) {
        EXPECT_FALSE(lease.OutOfTime());
        return RunReport{};
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kCompleted);
}

}  // namespace
}  // namespace one_d4_worker
