// The C++ index worker (#1389 phase 4): claims a range off
// `indexing_requests`, indexes it, writes game_features and
// motif_occurrences, and reports the outcome.
//
//   ONE_D4_DB_URL=postgresql://... bazel run //domains/games/apis/one_d4_worker
//   kill -TERM <pid>   # hands the current request back and exits 0
//
// The table is the queue (#1279), so this runs beside the Java worker
// rather than instead of it: same claims, same leases, same fences. It
// creates no schema — the Java service owns the migrations.

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/chess_com_archive.h"
#include "domains/games/apis/one_d4_worker/db_options.h"
#include "domains/games/apis/one_d4_worker/index_pool.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/games/apis/one_d4_worker/pg_game_sink.h"
#include "domains/games/apis/one_d4_worker/pg_queue.h"
#include "domains/games/apis/one_d4_worker/pg_reanalysis.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/reanalysis_poller.h"
#include "domains/games/apis/one_d4_worker/reanalysis_queue.h"
#include "domains/games/apis/one_d4_worker/reanalysis_run.h"
#include "domains/games/apis/one_d4_worker/title_roster.h"
#include "domains/games/apis/one_d4_worker/worker.h"
#include "domains/games/libs/chess_com_cpp/production_client.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "domains/platform/libs/pg/pg.h"

namespace {

/// Read by the claim thread, every worker thread and the roster, and
/// written by a signal handler — so std::atomic rather than
/// sig_atomic_t, which is only defined for the single-thread case. The
/// static_assert is what makes the handler's store legal.
std::atomic<bool> g_stopping{false};
static_assert(std::atomic<bool>::is_always_lock_free,
              "a signal handler may only store to a lock-free atomic");

void RequestShutdown(int /*signal*/) { g_stopping.store(true, std::memory_order_relaxed); }

std::string Env(const char* name, std::string fallback = "") {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? std::string(value) : std::move(fallback);
}

/// This container's name, or a stand-in when the kernel will not say.
std::string Hostname() {
  char host[256] = {};
  if (gethostname(host, sizeof(host) - 1) != 0) return "unknown-host";
  return host;
}

}  // namespace

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  const std::string db_url = Env("ONE_D4_DB_URL");
  if (db_url.empty()) {
    LOG(ERROR) << "ONE_D4_DB_URL is unset; there is no queue to poll";
    return 1;
  }

  futility::otel::OtelConfig otel_config{
      .service_name = "one_d4_worker",
      .service_version = "1.0.0",
      // An index run takes minutes and a month holds hundreds of games.
      // On the SDK defaults both histograms are one overflow bucket.
      .histogram_bounds = one_d4_worker::WorkerMetrics::HistogramBounds()};
  futility::otel::OtelProvider otel_provider(otel_config);
  futility::otel::MetricsRecorder recorder("one_d4_worker");
  one_d4_worker::WorkerMetrics metrics(recorder);
  // Before the first poll, so every series exports a zero baseline rather
  // than springing into existence carrying its first event's value.
  metrics.Declare();

  // The lease, its renewal interval and the run ceiling are not
  // configured here, deliberately. They are protocol constants of the
  // queue rather than deployment knobs: two pollers that disagree about
  // them misbehave against each other, and schema_contract_test holds the
  // C++ values equal to RetentionPolicy's. An environment variable would
  // put a third value in play that no test can see, at the one moment
  // nobody is looking. Changing one is a code change with a rationale.
  one_d4_worker::Poller::Options poller_options;
  poller_options.owner = one_d4_worker::OwnerId(Hostname(), getpid());

  // How often to ask an empty queue is local: it costs one round trip and
  // affects nobody else.
  const absl::Duration idle_wait =
      absl::Seconds(futility::env::ReadPositiveIntOr("ONE_D4_POLL_SECONDS", 5));

  smithy::Outcome<chess_com::Client> client = chess_com::CreateProductionClient();
  if (!client.ok()) {
    LOG(ERROR) << "Could not build the chess.com client: " << client.error().message();
    return 1;
  }
  one_d4_worker::ChessComArchive archive(*client);
  // Ten documents for the whole titled population of the site, held for
  // the life of the process. See title_roster.h.
  one_d4_worker::TitleRoster::Options title_options;
  title_options.stopping = [] { return g_stopping.load(std::memory_order_relaxed); };
  one_d4_worker::TitleRoster titles(archive, std::move(title_options));

  // Bounded, because nothing else bounds them and the run ceiling cannot:
  // a thread inside libpq never reaches a checkpoint to be told its time
  // is up. Cancelling a run that is already blocked is a separate job
  // (#1400).
  const std::string bounded_db_url = one_d4_worker::WithExecutionBounds(db_url);
  LOG(INFO) << "Polling indexing_requests as " << poller_options.owner;

  std::signal(SIGINT, RequestShutdown);
  std::signal(SIGTERM, RequestShutdown);

  const auto stopping = [] { return g_stopping.load(std::memory_order_relaxed); };
  const one_d4_worker::Poller::Run run = one_d4_worker::MakeRun(
      archive, titles,
      // A connection per run: one pg::Client is one connection serialised
      // by a mutex, so runs sharing one would queue every flush behind
      // every other run's and leave nothing to overlap.
      [&bounded_db_url](const one_d4_worker::Claim& claim) {
        return one_d4_worker::NewOwnedPgGameSink(bounded_db_url, claim.job.id, claim.owner);
      },
      metrics, stopping);

  one_d4_worker::IndexPool::Options pool_options;
  // Local capacity, not a queue protocol constant — two workers may
  // disagree about it without misbehaving against each other — so unlike
  // the lease and the ceiling this is a knob. See index_pool.h.
  //
  // Capped, because a slot is a thread, two Postgres connections out of a
  // shared budget, and a concurrent chess.com request. A typo in a
  // deployment variable should not exhaust a database two other services
  // are using.
  pool_options.slots = std::min(futility::env::ReadPositiveIntOr("ONE_D4_INDEX_SLOTS", 4), 16);
  pool_options.idle_wait = idle_wait;
  // A queue connection per thread as well as per run. See index_pool.h.
  one_d4_worker::IndexPool pool(
      [&bounded_db_url] { return one_d4_worker::NewOwnedPgQueue(bounded_db_url); }, run,
      poller_options, metrics, pool_options);
  LOG(INFO) << "Indexing up to " << pool_options.slots << " requests at once";

  const auto sleep = [](absl::Duration wait) { absl::SleepFor(wait); };

  // A thread of its own, not a slot in the pool (#1389 phase 5). A pass
  // walks the whole corpus and runs for hours; a slot spent on one is a
  // slot not indexing, and the queue hands out at most one pass anyway.
  //
  // Its connections are built on that thread for the same reason the
  // pool's are: a pg::Client is one connection serialised by a mutex, and
  // a pass's progress writes would otherwise queue in front of every
  // indexing claim.
  one_d4_worker::ReanalysisPoller::Options reanalysis_options;
  reanalysis_options.owner = poller_options.owner;
  std::thread reanalysis([&] {
    one_d4_worker::PollReanalysisUntilStopped(
        [&bounded_db_url] { return one_d4_worker::NewOwnedReanalysisQueue(bounded_db_url); },
        [&bounded_db_url, &stopping](const one_d4_worker::ReanalysisClaim& claim,
                                     one_d4_worker::ReanalysisLease& lease)
            -> absl::StatusOr<one_d4_worker::ReanalysisReport> {
          const auto ends =
              one_d4_worker::NewOwnedReanalysisEnds(bounded_db_url, claim.job.id, claim.owner);
          one_d4_worker::ReanalysisRun::Options options;
          options.stopping = stopping;
          one_d4_worker::ReanalysisRun run(*ends.corpus, *ends.sink, options);
          return run.Execute(claim.job, lease);
        },
        reanalysis_options, stopping, sleep, idle_wait);
  });
  LOG(INFO) << "Polling reanalysis_requests as " << reanalysis_options.owner;

  pool.Run(stopping, sleep);
  reanalysis.join();

  LOG(INFO) << "Shutting down";
  return 0;
}
