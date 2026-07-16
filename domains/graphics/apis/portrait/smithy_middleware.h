#ifndef CPP_PORTRAIT_SMITHY_MIDDLEWARE_H
#define CPP_PORTRAIT_SMITHY_MIDDLEWARE_H

#include <chrono>
#include <memory>
#include <string>

#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
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

/// Meerkat-parity observability, composed outermost so health probes and
/// rate-limited requests are observed exactly as meerkat's interceptors saw
/// them:
///   - metrics start/complete with route (path sans query string) and method
///     labels, microsecond durations
///   - x-trace-id propagation: an inbound header is reused, else a random
///     positive long is generated; set on the response unless the handler
///     already set one
///   - meerkat's access-log line, byte-for-byte:
///     [METHOD URI]: X-Forwarded-For=<ip> trace_id=<id> status=<code>
///     res.body.bytes=<n> duration_ms=<ms>
smithy::server::Middleware MeerkatParityObservability(std::shared_ptr<HttpMetricsSink> metrics);

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
