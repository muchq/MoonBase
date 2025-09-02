#include <functional>
#include <memory>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "cpp/futility/otel/otel_provider.h"
#include "cpp/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "cpp/meerkat/meerkat.h"
#include "cpp/portrait/types.h"
#include "tracer_service.h"

using namespace meerkat;
using namespace portrait;

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // Initialize OpenTelemetry
  futility::otel::OtelConfig otel_config{.service_name = "portrait",
                                         .service_version = "1.0.0",
                                         .otlp_endpoint = "http://localhost:4318/v1/metrics"};
  futility::otel::OtelProvider::Initialize(otel_config);

  LOG(INFO) << "Starting Portrait Server with OpenTelemetry metrics...";

  HttpServer server;
  TracerService tracer_service;
  futility::rate_limiter::SlidingWindowRateLimiterConfig config{
      .max_requests_per_key = 20,
      .window_size = std::chrono::seconds(60),
      .ttl = std::chrono::minutes(5),
      .cleanup_interval = std::chrono::seconds(30),
      .max_keys = 1000};
  auto rate_limiter =
      std::make_shared<futility::rate_limiter::SlidingWindowRateLimiter<std::string>>(config);

  // ray tracing endpoint
  server.post("/v1/trace", wrap<TraceRequest, TraceResponse>([&tracer_service](TraceRequest& req) {
                return tracer_service.trace(req);
              }));

  server.enable_health_checks();
  server.enable_tracing();
  server.use_response_interceptor(interceptors::response::logging());
  server.use_request_interceptor(interceptors::request::rate_limiter(rate_limiter));

  const std::string host = "0.0.0.0";
  const int port = read_port(8080);

  if (server.listen(host, port)) {
    LOG(INFO) << "Portrait Server running on http://" << host << ":" << port;
    LOG(INFO) << "Serving:";
    LOG(INFO) << "  GET  http://localhost:8080/health";
    LOG(INFO) << "  POST http://localhost:8080/v1/trace";
    LOG(INFO) << "Press Ctrl+C to stop the server";

    server.run();
  } else {
    LOG(ERROR) << "Failed to start server on " << host << ":" << port;
    futility::otel::OtelProvider::Shutdown();
    return 1;
  }

  futility::otel::OtelProvider::Shutdown();
  return 0;
}