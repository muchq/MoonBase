#ifndef CPP_PORTRAIT_SMITHY_MIDDLEWARE_H
#define CPP_PORTRAIT_SMITHY_MIDDLEWARE_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "smithy/http/beast_transport.h"
#include "smithy/server/middleware.h"

namespace meerkat {
class HttpMetricsManager;
}  // namespace meerkat

namespace portrait {

/// The two calls meerkat::HttpMetricsManager exposes, as a virtual seam so
/// tests can observe middleware invocations. MakeMeerkatMetricsSink builds
/// the production implementation.
class HttpMetricsSink {
 public:
  virtual ~HttpMetricsSink() = default;
  virtual void RecordRequestStart(const std::string& route, const std::string& method) = 0;
  virtual void RecordRequestComplete(const std::string& route, const std::string& method,
                                     int status_code, std::chrono::microseconds duration) = 0;
};

/// A sink forwarding to meerkat::HttpMetricsManager, so the exported
/// instrument names and labels (http_server_requests,
/// http_server_requests_active_gauge,
/// http_server_request_duration_microseconds, http_server_requests_success /
/// _failure) stay identical to the meerkat service and existing dashboards
/// keep working across the migration. Defined in the .cc so only the
/// production wiring compiles against meerkat.
std::shared_ptr<HttpMetricsSink> MakeMeerkatMetricsSink(
    std::shared_ptr<meerkat::HttpMetricsManager> metrics);

/// Serving observability, composed outermost so health probes and
/// rate-limited requests are observed exactly as meerkat's interceptors saw
/// them:
///   - metrics start/complete with route (path sans query string) and method
///     labels, microsecond durations
///   - meerkat's access-log line shape, with trace_id now carrying the W3C
///     trace id (the transport guard mints or joins the request's
///     traceparent at ingress per smithy-cpp ADR-0011; no response header —
///     the old custom x-trace-id echo is gone):
///     [METHOD URI]: X-Forwarded-For=<ip> trace_id=<32hex> status=<code>
///     res.body.bytes=<n> duration_ms=<ms>
smithy::server::Middleware MeerkatParityObservability(std::shared_ptr<HttpMetricsSink> metrics);

/// Sink callback for BeastServerTransport::Options::on_rejected, so the
/// 413/431 rejections the transport writes before any handler chain exists
/// land in the same instruments as everything else (an over-limit flood was
/// previously invisible to metrics — a capability meerkat never had).
std::function<void(const smithy::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics);

/// Per-client admission control keyed on X-Forwarded-For (clients without
/// the header share the empty-string bucket, as under meerkat), rejecting
/// with 429 {"error":"Too many requests"} plus Retry-After. Compose inside
/// MeerkatParityObservability (so 429s are counted) and after HealthEndpoint
/// (so probes are never rate limited).
smithy::server::Middleware RateLimitByForwardedFor(
    std::shared_ptr<futility::rate_limiter::SlidingWindowRateLimiter<std::string>> limiter,
    std::chrono::seconds retry_after);

}  // namespace portrait

#endif
