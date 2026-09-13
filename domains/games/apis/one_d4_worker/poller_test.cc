#include "domains/games/apis/one_d4_worker/poller.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
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
      std::string_view owner, [[maybe_unused]] absl::Duration lease,
      absl::Span<const std::string> at_capacity) override {
    ++claims;
    owners.push_back(std::string(owner));
    excluded.emplace_back(at_capacity.begin(), at_capacity.end());
    if (claim_fails) return absl::UnavailableError("queue is down");
    // The queue's own contract, as Postgres implements it: a row naming an
    // excluded platform is not a candidate.
    while (!queued.empty()) {
      IndexJob job = queued.front();
      queued.erase(queued.begin());
      if (std::find(at_capacity.begin(), at_capacity.end(), job.platform) == at_capacity.end()) {
        return job;
      }
      skipped.push_back(job);
    }
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
  /// What each claim was told not to take.
  std::vector<std::vector<std::string>> excluded;
  /// Rows the queue passed over because their platform was at capacity.
  std::vector<IndexJob> queued;
  std::vector<IndexJob> skipped;
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
  job.platform = "CHESS_COM";
  job.start_month = "2026-01";
  job.end_month = "2026-01";
  return job;
}

/// A job with an id and a platform of its own, for the capacity tests.
IndexJob AJobFor(std::string id, std::string platform) {
  IndexJob job = AJob();
  job.id = std::move(id);
  job.platform = std::move(platform);
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

// The one exception to the fixed sentence, and still a fixed sentence: a
// worker with no lichess token fails every LICHESS request it claims, and
// "internal error" sends the operator reading logs for a bug that is a
// missing environment variable. Says what to do without quoting anything
// upstream said.
TEST(Poller, SaysSoWhenTheRunFailedForWantOfCredentials) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        return absl::UnauthenticatedError("lichess games alice 2026-01: GamesNotFound: ...");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls,
              ElementsAre("fail job-1 This server is not configured to index that platform"));
  EXPECT_EQ(poller.last_outcome(), RunOutcome::kFailed);
  ExpectEveryWriteFencedOnTheClaim(queue);
}

// The other failure whose cause is not ours: a handle that does not exist.
// Verified against both archives rather than assumed — chess.com answers an
// empty month on a real player with 200 and an empty list, and 404s only a
// player it has never heard of; an authenticated Lichess export is the same.
// So NotFound here means the handle, and "internal error" would send someone
// chasing a server bug over their own typo.
TEST(Poller, SaysSoWhenThePlayerWasNotFound) {
  FakeQueue queue;
  queue.next = AJob();
  Poller poller(
      queue,
      [](const Claim&, LeaseKeeper&) -> absl::StatusOr<RunReport> {
        return absl::NotFoundError("chess.com archive alice 2026-01: ArchiveNotFound: ...");
      },
      Options());

  ASSERT_TRUE(poller.PollOnce().ok());
  EXPECT_THAT(queue.calls, ElementsAre("fail job-1 Player was not found on that platform"));
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

// ---- platform capacity ----

// Lichess asks for one request at a time and LichessArchive holds a mutex to
// honour it. A second LICHESS claim would park on that mutex holding a lease
// and two Postgres connections for as long as the export ahead of it takes.
// Not claiming it is the fix (#1527 slice 6).
TEST(PollerPlatformLimits, WillNotClaimASecondRequestForACappedPlatform) {
  FakeQueue queue;
  PlatformAdmission admission({{"LICHESS", 1}});
  Poller::Options options = Options();
  options.admission = &admission;
  // A run that never returns would hang the test; what matters is that the
  // claim is held, so the run completes and ClaimOne is called directly.
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);

  // One LICHESS run in flight, held open — the claim is what holds it.
  queue.queued = {AJobFor("first", "LICHESS")};
  const auto first = poller.ClaimOne();
  ASSERT_TRUE(first->has_value());

  queue.queued = {AJobFor("second", "LICHESS")};
  const auto second = poller.ClaimOne();

  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_FALSE(second->has_value()) << "a second LICHESS row was claimed and would have parked";
  EXPECT_THAT(queue.excluded.back(), ElementsAre("LICHESS"));
}

// The point of excluding at claim time rather than parking after it: work for
// a platform with no such rule keeps moving. This is the mixed queue.
TEST(PollerPlatformLimits, StillClaimsAnotherPlatformWhileTheCappedOneIsBusy) {
  FakeQueue queue;
  PlatformAdmission admission({{"LICHESS", 1}});
  Poller::Options options = Options();
  options.admission = &admission;
  // A run that never returns would hang the test; what matters is that the
  // claim is held, so the run completes and ClaimOne is called directly.
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);

  queue.queued = {AJobFor("lichess-1", "LICHESS")};
  const auto held = poller.ClaimOne();
  ASSERT_TRUE(held->has_value());

  // A LICHESS row at the front, a chess.com row behind it.
  queue.queued = {AJobFor("lichess-2", "LICHESS"), AJobFor("chess-1", "CHESS_COM")};
  const auto next = poller.ClaimOne();

  ASSERT_TRUE(next.ok()) << next.status();
  ASSERT_TRUE(next->has_value()) << "chess.com work starved behind a Lichess export";
  EXPECT_EQ((*next)->job.id, "chess-1");
  EXPECT_EQ((*next)->job.platform, "CHESS_COM");
}

// A cap that never gave a slot back would run one Lichess request and then
// refuse forever.
TEST(PollerPlatformLimits, GivesTheSlotBackWhenTheRunEnds) {
  FakeQueue queue;
  PlatformAdmission admission({{"LICHESS", 1}});
  Poller::Options options = Options();
  options.admission = &admission;
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);

  queue.queued = {AJobFor("first", "LICHESS")};
  ASSERT_TRUE(poller.PollOnce().value());

  queue.queued = {AJobFor("second", "LICHESS")};
  const auto second = poller.ClaimOne();

  ASSERT_TRUE(second.ok()) << second.status();
  ASSERT_TRUE(second->has_value()) << "the cap never released after the first run finished";
  EXPECT_EQ((*second)->job.id, "second");
}

// Every other platform is uncapped, and an empty exclusion list has to mean
// "take anything" rather than "take nothing".
TEST(PollerPlatformLimits, AnUncappedPlatformIsNeverExcluded) {
  FakeQueue queue;
  PlatformAdmission admission({{"LICHESS", 1}});
  Poller::Options options = Options();
  options.admission = &admission;
  // A run that never returns would hang the test; what matters is that the
  // claim is held, so the run completes and ClaimOne is called directly.
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);

  queue.queued = {AJobFor("a", "CHESS_COM")};
  const auto first = poller.ClaimOne();
  ASSERT_TRUE(first->has_value());
  queue.queued = {AJobFor("b", "CHESS_COM")};
  const auto second = poller.ClaimOne();

  ASSERT_TRUE(second->has_value()) << "chess.com capped itself";
  EXPECT_THAT(queue.excluded.back(), IsEmpty());
}

// The bug the single-Poller tests above could not see: IndexPool builds one
// Poller per slot thread from one copy of Options, so a cap that lived in
// the Poller counted a slot and capped nothing. Two Pollers sharing an
// admission are what the pool actually does.
TEST(PollerPlatformLimits, TwoPollersSharingAnAdmissionShareTheCap) {
  FakeQueue queue;
  PlatformAdmission admission({{"LICHESS", 1}});
  Poller::Options options = Options();
  options.admission = &admission;
  Poller slot_one(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);
  Poller slot_two(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);

  queue.queued = {AJobFor("lichess-1", "LICHESS")};
  const auto first = slot_one.ClaimOne();
  ASSERT_TRUE(first->has_value());

  queue.queued = {AJobFor("lichess-2", "LICHESS"), AJobFor("chess-1", "CHESS_COM")};
  const auto second = slot_two.ClaimOne();

  ASSERT_TRUE(second.ok()) << second.status();
  ASSERT_TRUE(second->has_value());
  EXPECT_EQ((*second)->job.platform, "CHESS_COM")
      << "a second slot claimed LICHESS: the cap is per Poller, not per worker";
}

// The other half of the cap's lifecycle, and the half production used:
// IndexPool::Work claims and runs in two calls rather than through
// PollOnce, so a release living in PollOnce never ran. The first LICHESS
// run kept the place forever and every later LICHESS row stayed PENDING
// until the process restarted.
TEST(PollerPlatformLimits, GivesThePlaceBackWhenTheClaimIsRunTheWayThePoolRunsIt) {
  FakeQueue queue;
  PlatformAdmission admission({{"LICHESS", 1}});
  Poller::Options options = Options();
  options.admission = &admission;
  Poller poller(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, options);

  queue.queued = {AJobFor("lichess-1", "LICHESS")};
  {
    const auto first = poller.ClaimOne();
    ASSERT_TRUE(first->has_value());
    ASSERT_TRUE(poller.RunClaimed(**first).ok());
  }

  queue.queued = {AJobFor("lichess-2", "LICHESS")};
  const auto second = poller.ClaimOne();

  ASSERT_TRUE(second->has_value()) << "the platform was never given back";
  EXPECT_EQ((*second)->job.platform, "LICHESS");
}

// Two Pollers with admissions of their own are the bug, stated as a test so
// the distinction is visible rather than implied.
TEST(PollerPlatformLimits, AnUnsharedAdmissionCapsNothingAcrossSlots) {
  FakeQueue queue;
  PlatformAdmission one({{"LICHESS", 1}});
  PlatformAdmission two({{"LICHESS", 1}});
  Poller::Options first = Options();
  first.admission = &one;
  Poller::Options second = Options();
  second.admission = &two;
  Poller slot_one(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, first);
  Poller slot_two(queue, [](const Claim&, LeaseKeeper&) { return RunReport{}; }, second);

  queue.queued = {AJobFor("lichess-1", "LICHESS")};
  const auto held = slot_one.ClaimOne();
  ASSERT_TRUE(held->has_value());
  queue.queued = {AJobFor("lichess-2", "LICHESS")};

  EXPECT_EQ(slot_two.ClaimOne().value().value().job.platform, "LICHESS")
      << "separate admissions are why the pool has to share one";
}

}  // namespace
}  // namespace one_d4_worker
