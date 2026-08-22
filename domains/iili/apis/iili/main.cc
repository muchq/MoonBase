// The iili server (#1359): the generated Iili API on the Beast
// transport, with observability, health, and per-client rate limiting.
//
//   IILI_DB_URL=postgresql://... bazel run //domains/iili/apis/iili
//   curl localhost:8091/iili/v1/shorten -H 'content-type: application/json' \
//     -d '{"longUrl":"https://example.com/some/where"}'

#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/time/clock.h"
#include "domains/iili/apis/iili/migrations.h"
#include "domains/iili/apis/iili/pg_url_store.h"
#include "domains/iili/apis/iili/shortener.h"
#include "domains/iili/apis/iili/smithy_handler.h"
#include "domains/platform/libs/aura/cache.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/http_metrics.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "domains/platform/libs/pg/pg.h"
#include "moonbase/iili/server.h"
#include "smithy/http/beast_transport.h"

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  futility::otel::OtelConfig otel_config{.service_name = "iili", .service_version = "1.0.0"};
  futility::otel::OtelProvider otel_provider(otel_config);

  // No fallback: anything this could start on unattended loses links
  // silently on restart.
  const char* db_url = std::getenv("IILI_DB_URL");
  if (db_url == nullptr || *db_url == '\0') {
    LOG(ERROR) << "IILI_DB_URL is not set. iili needs a libpq URL "
               << "(postgresql://user:pass@host:5432/db); there is no in-memory fallback.";
    return 1;
  }

  // Two clients so redirects never queue behind shortens.
  auto read_db = std::make_shared<pg::Client>(db_url);
  auto write_db = std::make_shared<pg::Client>(db_url);
  if (absl::Status migrated = iili::RunMigrations(*write_db); !migrated.ok()) {
    LOG(ERROR) << "Migrations failed; refusing to serve against an unknown schema: " << migrated;
    return 1;
  }

  auto metrics_recorder = std::make_shared<futility::otel::MetricsRecorder>("iili");
  auto shortener = std::make_shared<iili::Shortener>(
      std::make_shared<iili::PgUrlStore>(read_db, write_db),
      std::make_shared<iili::Shortener::Cache>("url_cache", 1000, metrics_recorder),
      [] { return absl::Now(); });

  moonbase::iili::IiliServer server(
      std::make_shared<iili::SmithyShortenerHandler>(std::move(shortener)));

  auto metrics =
      aura::MakeHttpMetricsSink(std::make_shared<futility::otel::HttpMetricsManager>("iili"));

  // Generous because redirects burst; still per client, keys bounded.
  futility::rate_limiter::SlidingWindowRateLimiterConfig limiter_config{
      .max_requests_per_key = 120,
      .window_size = std::chrono::seconds(60),
      .ttl = std::chrono::minutes(5),
      .cleanup_interval = std::chrono::seconds(30),
      .max_keys = 10000};
  auto rate_limiter =
      std::make_shared<futility::rate_limiter::SlidingWindowRateLimiter<std::string>>(
          limiter_config);

  auto trusted_proxies = aura::TrustedProxiesFromEnv();
  if (!trusted_proxies.has_value()) return 1;

  auto handler = aura::ProductionChain(
      aura::ChainOptions{
          .metrics = metrics,
          .allow_request =
              [rate_limiter](const std::string& client) { return rate_limiter->allow(client); },
          .trusted_proxies = std::move(*trusted_proxies),
          .retry_after = std::chrono::seconds(60)},
      iili::WithHeadAsGet(server.Handler()));

  // Block shutdown signals before the transport spawns its thread pool —
  // and only now, so a hung DB connection during boot is still killable.
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

  smithy::http::BeastServerTransport::Options options;
  options.address = "0.0.0.0";
  options.port = futility::env::ReadPort(8091);
  // A shorten body tops out near 1100 bytes; 16KB is headroom.
  options.max_body_bytes = std::size_t{16} * 1024;
  options.on_rejected = aura::RejectionMetrics(metrics);
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);

  smithy::Outcome<smithy::Unit> started = transport.Start(handler);
  if (!started.ok()) {
    LOG(ERROR) << "Failed to start server on " << options.address << ":" << options.port << ": "
               << started.error().message();
    return 1;
  }

  LOG(INFO) << "iili running on http://" << options.address << ":" << transport.port();
  LOG(INFO) << "Serving:";
  LOG(INFO) << "  GET  http://localhost:" << transport.port() << "/health";
  LOG(INFO) << "  POST http://localhost:" << transport.port() << "/iili/v1/shorten";
  LOG(INFO) << "  GET  http://localhost:" << transport.port() << "/iili/v1/r/{slug}";

  int signal_number = 0;
  sigwait(&shutdown_signals, &signal_number);
  LOG(INFO) << "Signal " << signal_number << " received; draining";
  transport.Stop();
  return 0;
}
