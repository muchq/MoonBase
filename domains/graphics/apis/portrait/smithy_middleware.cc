#include "domains/graphics/apis/portrait/smithy_middleware.h"

#include <chrono>
#include <utility>

#include "absl/log/log.h"
#include "domains/platform/libs/meerkat/metrics_manager.h"
#include "smithy/http/trace_context.h"
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

// Meerkat's access-log line shape. Kept separate from Observe because the
// log needs raw header access (X-Forwarded-For, traceparent), which Observe
// deliberately doesn't expose; it measures its own duration for the log line
// only. The trace_id= field carries the W3C trace id: the transport guard
// mints or joins the request's traceparent at ingress (smithy-cpp ADR-0011),
// so on transport-served requests the header always parses. Empty only for
// hand-driven handler chains in tests.
smithy::server::Middleware AccessLog() {
  return [](smithy::http::RequestHandler next) {
    return [next = std::move(next)](
               const smithy::http::HttpRequest& request) -> smithy::http::HttpResponse {
      const auto start = std::chrono::steady_clock::now();

      smithy::http::HttpResponse response = next(request);

      const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      std::string trace_id;
      if (const auto context =
              smithy::http::ParseTraceparent(request.headers.Get("traceparent").value_or(""));
          context.has_value()) {
        trace_id = context->trace_id;
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
    return observe(AccessLog()(std::move(next)));
  };
}

std::function<void(const smithy::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics) {
  return [metrics = std::move(metrics)](
             const smithy::http::BeastServerTransport::RejectedRequest& rejected) {
    const std::string route = RouteOf(rejected.target);
    // Start + complete keeps the request counter and active gauge symmetric;
    // the rejection happens at parse time, so zero duration is accurate.
    metrics->RecordRequestStart(route, rejected.method);
    metrics->RecordRequestComplete(route, rejected.method, rejected.status,
                                   std::chrono::microseconds{0});
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
