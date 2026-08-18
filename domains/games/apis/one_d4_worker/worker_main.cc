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

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "domains/games/apis/one_d4_worker/chess_com_archive.h"
#include "domains/games/apis/one_d4_worker/index_run.h"
#include "domains/games/apis/one_d4_worker/metrics.h"
#include "domains/games/apis/one_d4_worker/pg_game_sink.h"
#include "domains/games/apis/one_d4_worker/pg_queue.h"
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/libs/chess_com_cpp/production_client.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "domains/platform/libs/pg/pg.h"

namespace {

volatile std::sig_atomic_t g_stopping = 0;

void RequestShutdown(int /*signal*/) { g_stopping = 1; }

std::string Env(const char* name, std::string fallback = "") {
  const char* value = std::getenv(name);
  return value != nullptr && *value != '\0' ? std::string(value) : std::move(fallback);
}

/// Names this process as the holder of a lease: unique across the workers
/// competing for the table, and stable for the life of the process, which
/// is what makes it usable as a fencing token. The host and pid are for
/// whoever reads the column while debugging a stuck range.
std::string OwnerId() {
  char host[256] = {};
  if (gethostname(host, sizeof(host) - 1) != 0) std::snprintf(host, sizeof(host), "unknown-host");
  return absl::StrCat("cpp/", host, "/", getpid(), "/", absl::ToUnixNanos(absl::Now()));
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

  const absl::Duration lease = absl::Seconds(std::atoi(Env("ONE_D4_LEASE_SECONDS", "300").c_str()));
  const absl::Duration idle_wait =
      absl::Seconds(std::atoi(Env("ONE_D4_POLL_SECONDS", "5").c_str()));
  // RetentionPolicy.MAX_RUN. Past it a run is treated as wedged: it stops
  // renewing, hands the range back with the attempt spent, and a
  // replacement picks it up.
  // Defaulted from the option rather than a literal, so the value the
  // contract test pins against RetentionPolicy.MAX_RUN is the value this
  // process actually runs with.
  const one_d4_worker::Poller::Options defaults;
  const std::string max_run_seconds =
      Env("ONE_D4_MAX_RUN_SECONDS", std::to_string(absl::ToInt64Seconds(defaults.max_run)));
  const absl::Duration max_run = absl::Seconds(std::atoi(max_run_seconds.c_str()));

  smithy::Outcome<chess_com::Client> client = chess_com::CreateProductionClient();
  if (!client.ok()) {
    LOG(ERROR) << "Could not build the chess.com client: " << client.error().message();
    return 1;
  }
  one_d4_worker::ChessComArchive archive(*client);

  pg::Client db(db_url);
  one_d4_worker::PgQueue queue(db);

  // The sink gets its own connection: a flush is a long transaction, and
  // the heartbeat that keeps its lease alive must not queue behind it.
  pg::Client sink_db(db_url);

  const std::string owner = OwnerId();
  LOG(INFO) << "Polling indexing_requests as " << owner;

  std::signal(SIGINT, RequestShutdown);
  std::signal(SIGTERM, RequestShutdown);

  one_d4_worker::Poller poller(
      queue,
      [&](const one_d4_worker::IndexJob& job,
          one_d4_worker::LeaseKeeper& keeper) -> absl::StatusOr<one_d4_worker::RunReport> {
        one_d4_worker::PgGameSink sink(sink_db, job.id, owner);
        one_d4_worker::IndexRun::Options options;
        options.observer = &metrics;
        options.stopping = [] { return g_stopping != 0; };
        one_d4_worker::IndexRun run(archive, sink, options);
        return run.Execute(job, keeper);
      },
      one_d4_worker::Poller::Options{
          .owner = owner, .lease = lease, .renew_every = lease / 4, .max_run = max_run});

  while (g_stopping == 0) {
    const absl::Time started = absl::Now();
    const absl::StatusOr<bool> ran = poller.PollOnce();
    if (!ran.ok()) {
      // A queue we cannot reach is a reason to wait and try again, not to
      // exit: the supervisor would only restart us into the same outage.
      LOG(ERROR) << "Poll failed: " << ran.status();
      absl::SleepFor(idle_wait);
      continue;
    }
    if (!*ran) {
      absl::SleepFor(idle_wait);
      continue;
    }
    metrics.RunFinished(poller.last_outcome(), absl::Now() - started);
    LOG(INFO) << "Run finished: " << one_d4_worker::ToString(poller.last_outcome());
  }

  LOG(INFO) << "Shutting down";
  return 0;
}
