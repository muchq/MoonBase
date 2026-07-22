#ifndef CPP_PORTRAIT_SMITHY_MIDDLEWARE_H
#define CPP_PORTRAIT_SMITHY_MIDDLEWARE_H

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "smithy/http/beast_transport.h"
#include "smithy/http/forwarded.h"
#include "smithy/server/middleware.h"

namespace futility::otel {
class HttpMetricsManager;
}  // namespace futility::otel

namespace portrait {

/// The two calls futility::otel::HttpMetricsManager exposes, as a virtual
/// seam so tests can observe middleware invocations. MakeHttpMetricsSink
/// builds the production implementation.
class HttpMetricsSink {
 public:
  virtual ~HttpMetricsSink() = default;
  virtual void RecordRequestStart(const std::string& route, const std::string& method) = 0;
  virtual void RecordRequestComplete(const std::string& route, const std::string& method,
                                     int status_code, std::chrono::microseconds duration) = 0;
};

/// A sink forwarding to futility::otel::HttpMetricsManager, the shared HTTP
/// serving instruments (http_server_requests,
/// http_server_requests_active_gauge,
/// http_server_request_duration_microseconds, http_server_requests_success /
/// _failure), so the exported names and labels stay identical across
/// services and dashboard history. Defined in the .cc so only the production
/// wiring compiles against the instruments.
std::shared_ptr<HttpMetricsSink> MakeHttpMetricsSink(
    std::shared_ptr<futility::otel::HttpMetricsManager> metrics);

/// Serving observability, composed outermost so health probes and
/// rate-limited requests are observed too:
///   - metrics start/complete with route (path sans query string) and method
///     labels, microsecond durations
///   - one access-log line per request, with trace_id carrying the W3C
///     trace id parsed from the request's traceparent (minted or joined at
///     transport ingress, smithy-cpp ADR-0011):
///     [METHOD URI]: X-Forwarded-For=<ip> trace_id=<32hex> status=<code>
///     res.body.bytes=<n> duration_ms=<ms>
smithy::server::Middleware ServingObservability(std::shared_ptr<HttpMetricsSink> metrics);

/// The production middleware chain around the generated server's handler,
/// shared between main and the middleware tests so they exercise the same
/// wiring. Observability sits outermost; health before the rate-limit guard
/// so probes are never rate limited; the guard keys on the ADR-0012 derived
/// client address (trust boundary from `trusted_proxies`) and answers 429
/// with Retry-After.
struct ChainOptions {
  std::shared_ptr<HttpMetricsSink> metrics;
  std::function<bool(const std::string& client)> allow_request;
  smithy::http::TrustedProxies trusted_proxies = smithy::http::TrustedProxies::None();
  std::chrono::seconds retry_after = std::chrono::seconds(60);
};
smithy::http::RequestHandler ProductionChain(ChainOptions options,
                                             smithy::http::RequestHandler handler);

/// Sink callback for BeastServerTransport::Options::on_rejected, so the
/// 413/431 rejections the transport writes before any handler chain exists
/// land in the same instruments as everything else (an over-limit flood
/// would otherwise be invisible to metrics).
std::function<void(const smithy::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics);

/// Log-only observer for BeastServerTransport::Options::on_connection_event
/// (smithy-cpp ADR-0013, kinds in beast_transport.h): each connection the
/// transport terminates without delivering a response gets one WARNING
/// line. Log-only because these are connections, not requests — mapping
/// them into the request-shaped instruments would distort request counts.
/// Kind counters are a pending instrument decision (PORTRAIT_TODO.md).
std::function<void(const smithy::http::BeastServerTransport::ConnectionEvent&)>
ConnectionEventLog();

}  // namespace portrait

#endif
