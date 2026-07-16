#include "domains/graphics/apis/portrait/smithy_middleware.h"

#include <chrono>
#include <climits>
#include <random>
#include <utility>

#include "absl/log/log.h"
#include "domains/platform/libs/meerkat/metrics_manager.h"
#include "smithy/http/transport.h"

namespace portrait {
namespace {

class MeerkatMetricsSink final : public HttpMetricsSink {
 public:
  explicit MeerkatMetricsSink(std::shared_ptr<meerkat::HttpMetricsManager> metrics)
      : metrics_(std::move(metrics)) {}

  void RecordRequestStart(const std::string& route, const std::string& method) override {
    metrics_->RecordRequestStart(route, method);
  }
  void RecordRequestComplete(const std::string& route, const std::string& method, int status_code,
                             std::chrono::microseconds duration) override {
    metrics_->RecordRequestComplete(route, method, status_code, duration);
  }

 private:
  std::shared_ptr<meerkat::HttpMetricsManager> metrics_;
};

std::string RouteOf(const std::string& target) { return target.substr(0, target.find('?')); }

std::string TraceIdFor(const smithy::http::HttpRequest& request) {
  if (const auto inbound = request.headers.Get("x-trace-id");
      inbound.has_value() && !inbound->empty()) {
    return *inbound;
  }
  // Same id shape as meerkat's interceptors::request::trace_id().
  static std::random_device rd;
  thread_local std::mt19937_64 gen(rd());
  std::uniform_int_distribution<long> dis(1, LONG_MAX);
  return std::to_string(dis(gen));
}

// x-trace-id propagation and meerkat's access-log line. Kept separate from
// Observe because the log needs raw header access (X-Forwarded-For, inbound
// x-trace-id), which Observe deliberately doesn't expose; it measures its
// own duration for the log line only.
smithy::server::Middleware TraceIdAndAccessLog() {
  return [](smithy::http::RequestHandler next) {
    return [next = std::move(next)](
               const smithy::http::HttpRequest& request) -> smithy::http::HttpResponse {
      const auto start = std::chrono::steady_clock::now();
      const std::string trace_id = TraceIdFor(request);

      smithy::http::HttpResponse response = next(request);

      const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (!response.headers.Has("x-trace-id")) {
        response.headers.Set("x-trace-id", trace_id);
      }
      LOG(INFO) << "[" << request.method << " " << request.target
                << "]: X-Forwarded-For=" << request.headers.Get("X-Forwarded-For").value_or("")
                << " trace_id=" << trace_id << " status=" << response.status
                << " res.body.bytes=" << response.body.size()
                << " duration_ms=" << duration_ms.count();
      return response;
    };
  };
}

}  // namespace

std::shared_ptr<HttpMetricsSink> MakeMeerkatMetricsSink(
    std::shared_ptr<meerkat::HttpMetricsManager> metrics) {
  return std::make_shared<MeerkatMetricsSink>(std::move(metrics));
}

smithy::server::Middleware MeerkatParityObservability(std::shared_ptr<HttpMetricsSink> metrics) {
  return [metrics = std::move(metrics)](smithy::http::RequestHandler next) {
    // Metrics ride the runtime's Observe: microsecond durations (as of
    // smithy-cpp cfd8299) and start/complete guaranteed to pair even when
    // dispatch throws.
    smithy::server::Middleware observe = smithy::server::Observe(
        [metrics](const smithy::server::RequestObservation& observation) {
          metrics->RecordRequestComplete(RouteOf(observation.target), observation.method,
                                         observation.status, observation.duration);
        },
        [metrics](const smithy::server::RequestStart& start) {
          metrics->RecordRequestStart(RouteOf(start.target), start.method);
        });
    return observe(TraceIdAndAccessLog()(std::move(next)));
  };
}

smithy::server::Middleware RateLimitByForwardedFor(
    std::shared_ptr<futility::rate_limiter::SlidingWindowRateLimiter<std::string>> limiter,
    std::chrono::seconds retry_after) {
  return smithy::server::Guard(
      [limiter = std::move(limiter)](const smithy::http::HttpRequest& request) {
        return limiter->allow(request.headers.Get("X-Forwarded-For").value_or(""));
      },
      smithy::server::TooManyRequests(retry_after));
}

}  // namespace portrait
