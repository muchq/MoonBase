#include "domains/graphics/apis/portrait/smithy_middleware.h"

#include <climits>
#include <random>

#include "absl/log/log.h"
#include "smithy/http/transport.h"

namespace portrait {
namespace {

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

}  // namespace

smithy::server::Middleware MeerkatParityObservability(std::shared_ptr<HttpMetricsSink> metrics) {
  return [metrics = std::move(metrics)](smithy::http::RequestHandler next) {
    return [metrics, next = std::move(next)](
               const smithy::http::HttpRequest& request) -> smithy::http::HttpResponse {
      const auto start = std::chrono::steady_clock::now();
      const std::string route = RouteOf(request.target);
      metrics->RecordRequestStart(route, request.method);
      const std::string trace_id = TraceIdFor(request);

      smithy::http::HttpResponse response = next(request);

      const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start);
      metrics->RecordRequestComplete(route, request.method, response.status, duration);
      if (!response.headers.Has("x-trace-id")) {
        response.headers.Set("x-trace-id", trace_id);
      }
      LOG(INFO) << "[" << request.method << " " << request.target
                << "]: X-Forwarded-For=" << request.headers.Get("X-Forwarded-For").value_or("")
                << " trace_id=" << trace_id << " status=" << response.status
                << " res.body.bytes=" << response.body.size() << " duration_ms="
                << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
      return response;
    };
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
