#include "domains/games/apis/one_d4_worker/reanalysis_poller.h"

#include <utility>

#include "absl/log/log.h"
#include "absl/random/random.h"
#include "absl/strings/str_cat.h"
#include "domains/games/apis/one_d4_worker/lease_core.h"

namespace one_d4_worker {
namespace {

constexpr char kInternalFailure[] = "Reanalysis failed due to an internal error";

std::string RunToken() {
  thread_local absl::BitGen bitgen;
  return absl::StrCat(absl::Hex(absl::Uniform<uint64_t>(bitgen), absl::kZeroPad16),
                      absl::Hex(absl::Uniform<uint64_t>(bitgen), absl::kZeroPad16));
}

/// The reanalysis queue's two fenced writes, over LeaseCore's bookkeeping.
class PassLease : public ReanalysisLease {
 public:
  PassLease(ReanalysisQueue& queue, std::string id, std::string owner, absl::Duration lease,
            absl::Duration renew_every, absl::Duration max_run)
      : queue_(queue),
        id_(std::move(id)),
        owner_(std::move(owner)),
        lease_(lease),
        core_([this] { return queue_.Heartbeat(id_, owner_, lease_); }, lease, renew_every,
              max_run) {}

  bool Keep() override { return core_.Keep(); }

  bool OutOfTime() override { return core_.OutOfTime(); }

  bool Report(std::string_view cursor, int games_processed, int games_failed) override {
    const std::string at(cursor);
    return core_.Fenced([this, at, games_processed, games_failed] {
      return queue_.Progress(id_, owner_, at, games_processed, games_failed);
    });
  }

  bool lost() const { return core_.lost(); }

 private:
  ReanalysisQueue& queue_;
  const std::string id_;
  const std::string owner_;
  const absl::Duration lease_;
  // Last, because its renewal thread reaches back through the members above.
  LeaseCore core_;
};

}  // namespace

ReanalysisPoller::ReanalysisPoller(ReanalysisQueue& queue, Run run, Options options)
    : queue_(queue), run_(std::move(run)), options_(std::move(options)) {}

absl::StatusOr<std::optional<ReanalysisClaim>> ReanalysisPoller::ClaimOne() {
  ReanalysisClaim claim;
  claim.owner = absl::StrCat(options_.owner, "/", RunToken());
  absl::StatusOr<std::optional<ReanalysisJob>> claimed =
      queue_.ClaimNext(claim.owner, options_.lease);
  if (!claimed.ok()) return claimed.status();
  if (!claimed->has_value()) return std::nullopt;
  claim.job = **claimed;
  LOG(INFO) << "Claimed reanalysis request_id=" << claim.job.id
            << " from=" << (claim.job.cursor_game_url.empty() ? "start" : claim.job.cursor_game_url)
            << " owner=" << claim.owner;
  return claim;
}

absl::StatusOr<RunOutcome> ReanalysisPoller::RunClaimed(const ReanalysisClaim& claim) {
  const ReanalysisJob& job = claim.job;
  const std::string& owner = claim.owner;
  PassLease lease(queue_, job.id, owner, options_.lease, options_.renew_every, options_.max_run);
  const absl::StatusOr<ReanalysisReport> report = run_(claim, lease);

  const auto finished = [&](RunOutcome outcome) {
    if (options_.on_finished) {
      const int processed = report.ok() ? report->games_processed - job.games_processed : 0;
      const int failed = report.ok() ? report->games_failed - job.games_failed : 0;
      options_.on_finished(outcome, processed, failed);
    }
    return outcome;
  };

  // Ahead of the lease check, like the index poller: a pass that stopped
  // at its ceiling gives the row back even if its claim lapsed on the way
  // out.
  if (report.ok() && report->stopped.has_value() && *report->stopped == Stopped::kRunCeiling) {
    // Whether the attempt is spent turns on whether anything moved. The
    // ceiling exists to recover a wedged pass, and a pass that advanced
    // its cursor demonstrably is not one — on a corpus large enough to
    // need several passes, spending an attempt each time would retire the
    // request after three before it ever reached the end.
    const bool advanced = report->cursor != job.cursor_game_url;
    LOG(INFO) << "Reanalysis hit its ceiling request_id=" << job.id << " cursor=" << report->cursor
              << " advanced=" << advanced;
    return Finish(RunOutcome::kInterrupted,
                  advanced ? queue_.HandBack(job.id, owner) : queue_.Release(job.id, owner));
  }

  if (lease.lost() || (report.ok() && report->lease_lost)) {
    return finished(RunOutcome::kLeaseLost);
  }

  if (!report.ok()) {
    // The cause goes to the log, not to the column: error_message is
    // handed back by the API.
    LOG(ERROR) << "Reanalysis failed request_id=" << job.id << " error=" << report.status();
    const absl::StatusOr<RunOutcome> outcome =
        Finish(RunOutcome::kFailed, queue_.Fail(job.id, owner, kInternalFailure));
    if (!outcome.ok()) return outcome;
    return finished(*outcome);
  }

  if (report->stopped.has_value()) {
    // Only kShutdown reaches here. The request did nothing wrong, and its
    // cursor is written, so the attempt is refunded and whoever takes it
    // next carries on.
    const absl::StatusOr<RunOutcome> outcome =
        Finish(RunOutcome::kInterrupted, queue_.HandBack(job.id, owner));
    if (!outcome.ok()) return outcome;
    return finished(*outcome);
  }

  LOG(INFO) << "Finished reanalysis request_id=" << job.id
            << " processed=" << report->games_processed << " failed=" << report->games_failed;
  const absl::StatusOr<RunOutcome> outcome =
      Finish(RunOutcome::kCompleted,
             queue_.Complete(job.id, owner, report->games_processed, report->games_failed));
  if (!outcome.ok()) return outcome;
  return finished(*outcome);
}

absl::StatusOr<bool> ReanalysisPoller::PollOnce() {
  const absl::StatusOr<std::optional<ReanalysisClaim>> claim = ClaimOne();
  if (!claim.ok()) return claim.status();
  if (!claim->has_value()) return false;

  const absl::StatusOr<RunOutcome> outcome = RunClaimed(**claim);
  if (!outcome.ok()) return outcome.status();
  last_outcome_ = *outcome;
  return true;
}

absl::StatusOr<RunOutcome> ReanalysisPoller::Finish(RunOutcome outcome,
                                                    const absl::StatusOr<bool>& written) {
  if (!written.ok()) return written.status();
  return *written ? outcome : RunOutcome::kLeaseLost;
}

void PollReanalysisUntilStopped(const std::function<std::unique_ptr<ReanalysisQueue>()>& make_queue,
                                ReanalysisPoller::Run run, ReanalysisPoller::Options poller,
                                const std::function<bool()>& stopping,
                                const std::function<void(absl::Duration)>& sleep,
                                absl::Duration idle_wait) {
  // Both live exactly as long as this thread, which is what makes the
  // connection its own.
  const std::unique_ptr<ReanalysisQueue> queue = make_queue();
  ReanalysisPoller poller_impl(*queue, std::move(run), std::move(poller));

  while (!stopping()) {
    const absl::StatusOr<bool> ran = poller_impl.PollOnce();
    if (!ran.ok()) {
      // A queue we cannot reach is not a reason to exit: the row is still
      // there and so is the cursor. Wait and ask again.
      LOG(ERROR) << "Reanalysis poll failed: " << ran.status();
      sleep(idle_wait);
      continue;
    }
    // Only when there was nothing to take. A pass that ran may well be
    // resumable right now, and sleeping would idle for no reason.
    if (!*ran) sleep(idle_wait);
  }
}

}  // namespace one_d4_worker
