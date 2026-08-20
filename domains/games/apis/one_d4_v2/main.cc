// The one_d4 v2 server: the generated Smithy OneD4V2 API on the Beast
// transport, with observability, health, and per-client rate limiting
// (#1389 phase 6).
//
//   bazel run //domains/games/apis/one_d4_v2
//   curl localhost:8090/1d4/v2/analyze -H 'content-type: application/json' -d '{"pgn":"..."}'

#include <pthread.h>
#include <signal.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "domains/games/apis/one_d4_v2/smithy_handler.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/http_metrics.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "moonbase/one_d4/server.h"
#include "smithy/http/beast_transport.h"

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  futility::otel::OtelConfig otel_config{.service_name = "one_d4_v2", .service_version = "1.0.0"};
  futility::otel::OtelProvider otel_provider(otel_config);

  // Block shutdown signals before the transport spawns its thread pool, so
  // they only reach the sigwait below.
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

  moonbase::one_d4::OneD4V2Server server(std::make_shared<one_d4_v2::SmithyAnalyzeHandler>());

  auto metrics =
      aura::MakeHttpMetricsSink(std::make_shared<futility::otel::HttpMetricsManager>("one_d4_v2"));

  // Portrait's numbers: analysis is cheaper than a render, so a limit that
  // protects that CPU protects this one.
  futility::rate_limiter::SlidingWindowRateLimiterConfig limiter_config{
      .max_requests_per_key = 20,
      .window_size = std::chrono::seconds(60),
      .ttl = std::chrono::minutes(5),
      .cleanup_interval = std::chrono::seconds(30),
      .max_keys = 1000};
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
      server.Handler());

  smithy::http::BeastServerTransport::Options options;
  options.address = "0.0.0.0";
  options.port = futility::env::ReadPort(8090);
  // The analyze handler refuses PGNs past 256KB; a body meaningfully past
  // that is not a request this service answers, so the transport need not
  // read it first.
  options.max_body_bytes = std::size_t{1} * 1024 * 1024;
  options.on_rejected = aura::RejectionMetrics(metrics);
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);

  smithy::Outcome<smithy::Unit> started = transport.Start(handler);
  if (!started.ok()) {
    LOG(ERROR) << "Failed to start server on " << options.address << ":" << options.port << ": "
               << started.error().message();
    return 1;
  }

  LOG(INFO) << "one_d4_v2 running on http://" << options.address << ":" << transport.port();
  LOG(INFO) << "Serving:";
  LOG(INFO) << "  GET  http://localhost:" << transport.port() << "/health";
  LOG(INFO) << "  POST http://localhost:" << transport.port() << "/1d4/v2/analyze";

  int signal_number = 0;
  sigwait(&shutdown_signals, &signal_number);
  LOG(INFO) << "Signal " << signal_number << " received; draining";
  transport.Stop();
  return 0;
}
