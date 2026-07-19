// The portrait server (https://github.com/muchq/MoonBase/issues/1168): the
// generated Smithy Portrait API on the Beast transport with meerkat-parity
// middleware. Rollback is the pre-cutover image tag; the meerkat binary
// stays buildable as :portrait_meerkat as the pre-cutover reference during
// the soak.
//
//   bazel run //domains/graphics/apis/portrait
//   curl localhost:8080/portrait/v1/trace -H 'content-type: application/json' -d @scene.json
//   curl localhost:8080/health
//   kill -TERM <pid>   # drains in-flight requests, then exits 0

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "domains/graphics/apis/portrait/smithy_handler.h"
#include "domains/graphics/apis/portrait/smithy_middleware.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "domains/platform/libs/meerkat/metrics_manager.h"
#include "moonbase/portrait/server.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/forwarded.h"
#include "smithy/server/middleware.h"

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // Same OTel setup as the meerkat binary: OTLP push, service "portrait".
  futility::otel::OtelConfig otel_config{.service_name = "portrait", .service_version = "1.0.0"};
  futility::otel::OtelProvider otel_provider(otel_config);

  // Block shutdown signals before the transport spawns its thread pool, so
  // they only reach the sigwait below.
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

  moonbase::portrait::PortraitServer server(std::make_shared<portrait::SmithyTracerHandler>());

  auto metrics =
      portrait::MakeMeerkatMetricsSink(std::make_shared<meerkat::HttpMetricsManager>("portrait"));

  // Same limiter config as the meerkat Main.cc.
  futility::rate_limiter::SlidingWindowRateLimiterConfig limiter_config{
      .max_requests_per_key = 20,
      .window_size = std::chrono::seconds(60),
      .ttl = std::chrono::minutes(5),
      .cleanup_interval = std::chrono::seconds(30),
      .max_keys = 1000};
  auto rate_limiter =
      std::make_shared<futility::rate_limiter::SlidingWindowRateLimiter<std::string>>(
          limiter_config);

  // The reverse-proxy trust boundary (smithy-cpp ADR-0012, contract in
  // smithy/http/forwarded.h): deploy/consolidated/compose.yaml pins Caddy's
  // address and passes it here. Unset is the deliberate direct-connect
  // statement (TrustedProxies::None()); set-but-empty or malformed fails
  // startup rather than silently collapsing proxied traffic onto one key.
  smithy::http::TrustedProxies trusted_proxies = smithy::http::TrustedProxies::None();
  if (std::getenv("TRUSTED_PROXY_CIDRS") != nullptr) {
    const std::vector<std::string> cidrs = futility::env::ReadList("TRUSTED_PROXY_CIDRS");
    if (cidrs.empty()) {
      LOG(ERROR) << "TRUSTED_PROXY_CIDRS is set but empty; unset it to serve direct-connect";
      return 1;
    }
    try {
      trusted_proxies = smithy::http::TrustedProxies(cidrs);
    } catch (const std::invalid_argument& error) {
      LOG(ERROR) << "Invalid TRUSTED_PROXY_CIDRS: " << error.what();
      return 1;
    }
  }

  // Observability outermost: health probes and 429s are counted and logged,
  // as they were under meerkat's interceptors. Health sits before the guard
  // so probes are never rate limited — the one deliberate ordering change
  // from meerkat, where /health shared the empty X-Forwarded-For bucket.
  auto handler = smithy::server::Chain(
      {portrait::MeerkatParityObservability(metrics), smithy::server::HealthEndpoint("/health"),
       smithy::server::PerClientRateLimit(
           [rate_limiter](const std::string& client) { return rate_limiter->allow(client); },
           std::move(trusted_proxies), std::chrono::seconds(60))},
      server.Handler());

  smithy::http::BeastServerTransport::Options options;
  options.address = "0.0.0.0";
  options.port = futility::env::ReadPort(8080);
  // Trace scenes are small JSON (at most 10 spheres); the 64 MiB transport
  // default is far more than this service ever needs.
  options.max_body_bytes = std::size_t{1} * 1024 * 1024;
  // 413/431s the transport writes itself land in the same instruments.
  options.on_rejected = portrait::RejectionMetrics(metrics);
  smithy::http::BeastServerTransport transport(options);

  smithy::Outcome<smithy::Unit> started = transport.Start(handler);
  if (!started.ok()) {
    LOG(ERROR) << "Failed to start server on " << options.address << ":" << options.port << ": "
               << started.error().message();
    return 1;
  }

  LOG(INFO) << "Portrait (smithy) running on http://" << options.address << ":" << transport.port();
  LOG(INFO) << "Serving:";
  LOG(INFO) << "  GET  http://localhost:" << transport.port() << "/health";
  LOG(INFO) << "  POST http://localhost:" << transport.port() << "/portrait/v1/trace";

  int signal_number = 0;
  sigwait(&shutdown_signals, &signal_number);
  LOG(INFO) << "Signal " << signal_number << " received; draining";
  transport.Stop();
  return 0;
}
