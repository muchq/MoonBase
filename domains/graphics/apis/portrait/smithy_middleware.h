#ifndef CPP_PORTRAIT_SMITHY_MIDDLEWARE_H
#define CPP_PORTRAIT_SMITHY_MIDDLEWARE_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

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
///   - meerkat's access-log line shape, with trace_id carrying the W3C
///     trace id parsed from the request's traceparent (minted or joined at
///     transport ingress, smithy-cpp ADR-0011):
///     [METHOD URI]: X-Forwarded-For=<ip> trace_id=<32hex> status=<code>
///     res.body.bytes=<n> duration_ms=<ms>
smithy::server::Middleware MeerkatParityObservability(std::shared_ptr<HttpMetricsSink> metrics);

/// Sink callback for BeastServerTransport::Options::on_rejected, so the
/// 413/431 rejections the transport writes before any handler chain exists
/// land in the same instruments as everything else (an over-limit flood was
/// previously invisible to metrics — a capability meerkat never had).
std::function<void(const smithy::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics);

/// Log-only observer for BeastServerTransport::Options::on_connection_event
/// (smithy-cpp ADR-0013, kinds in beast_transport.h): each connection the
/// transport terminates without delivering a response gets one WARNING
/// line. Log-only because these are connections, not requests — mapping
/// them into the request-shaped meerkat instruments would distort request
/// counts. Kind counters are a post-soak instrument decision
/// (PORTRAIT_TODO.md).
std::function<void(const smithy::http::BeastServerTransport::ConnectionEvent&)>
ConnectionEventLog();

}  // namespace portrait

#endif
