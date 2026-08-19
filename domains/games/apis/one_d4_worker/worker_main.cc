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
#include <optional>
#include <string>

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
#include "domains/games/apis/one_d4_worker/poller.h"
#include "domains/games/apis/one_d4_worker/title_roster.h"
#include "domains/games/apis/one_d4_worker/worker.h"
#include "domains/games/libs/chess_com_cpp/production_client.h"
#include "domains/platform/libs/futility/env/env.h"
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

  // The lease, its renewal interval and the run ceiling are not
  // configured here, deliberately. They are protocol constants of the
  // queue rather than deployment knobs: two pollers that disagree about
  // them misbehave against each other, and schema_contract_test holds the
  // C++ values equal to RetentionPolicy's. An environment variable would
  // put a third value in play that no test can see, at the one moment
  // nobody is looking. Changing one is a code change with a rationale.
  one_d4_worker::Poller::Options poller_options;
  poller_options.owner = OwnerId();

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
  title_options.stopping = [] { return g_stopping != 0; };
  one_d4_worker::TitleRoster titles(archive, std::move(title_options));

  // Bounded, because nothing else bounds them and the run ceiling cannot:
  // a thread inside libpq never reaches a checkpoint to be told its time
  // is up. Cancelling a run that is already blocked is a separate job
  // (#1400).
  const std::string bounded_db_url = one_d4_worker::WithExecutionBounds(db_url);
  pg::Client db(bounded_db_url);
  one_d4_worker::PgQueue queue(db);

  LOG(INFO) << "Polling indexing_requests as " << poller_options.owner;

  std::signal(SIGINT, RequestShutdown);
  std::signal(SIGTERM, RequestShutdown);

  const auto stopping = [] { return g_stopping != 0; };
  one_d4_worker::Poller poller(queue,
                               one_d4_worker::MakeRun(
                                   archive, titles,
                                   // A connection per run: one pg::Client is one connection
                                   // serialised by a mutex, so runs sharing one would queue every
                                   // flush behind every other run's and leave the pool nothing to
                                   // overlap.
                                   [&bounded_db_url](const one_d4_worker::Claim& claim) {
                                     return one_d4_worker::NewOwnedPgGameSink(
                                         bounded_db_url, claim.job.id, claim.owner);
                                   },
                                   metrics, stopping),
                               poller_options);

  one_d4_worker::IndexPool::Options pool_options;
  // Local capacity, not a queue protocol constant — two workers may
  // disagree about it without misbehaving against each other — so unlike
  // the lease and the ceiling this is a knob. See index_pool.h.
  pool_options.slots = futility::env::ReadPositiveIntOr("ONE_D4_INDEX_SLOTS", 4);
  pool_options.idle_wait = idle_wait;
  one_d4_worker::IndexPool pool(poller, metrics, pool_options);
  LOG(INFO) << "Indexing up to " << pool_options.slots << " requests at once";

  pool.Run(stopping, [](absl::Duration wait) { absl::SleepFor(wait); });

  LOG(INFO) << "Shutting down";
  return 0;
}
