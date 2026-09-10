#ifndef DOMAINS_PLATFORM_LIBS_AURA_MIDDLEWARE_H
#define DOMAINS_PLATFORM_LIBS_AURA_MIDDLEWARE_H

// aura: the serving chain for opal-cpp services — observability (the
// shared http_server_* instruments plus an access log), health, and
// optional per-client rate limiting, composed the same way in production
// and in tests.

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "opal/http/beast_transport.h"
#include "opal/http/forwarded.h"
#include "opal/server/middleware.h"

namespace futility::otel {
class HttpMetricsManager;
}  // namespace futility::otel

namespace aura {

/// The health path every part of this deployment agrees on: ProductionChain
/// serves it, the compose healthchecks probe it, and prom_proxy subtracts
/// exactly route="/health" from every Serving number (and selects it on the
/// Probes tiles). Requests to it are labeled with this literal rather than
/// the sentinel below, so the subtraction keeps working (#1303, #1307).
inline constexpr char kHealthRoute[] = "/health";

/// The route label for a request the router never matched — 404s, 405s,
/// transport rejections, rate-limited requests, scanner noise. A fixed
/// sentinel rather than the raw path, so scanners cannot mint unbounded
/// Prometheus series (#1305); same spelling as yodel's UNMATCHED_ROUTE and
/// server_pal's UNMATCHED_ROUTE.
inline constexpr char kUnmatchedRoute[] = "unmatched";

/// The two calls futility::otel::HttpMetricsManager exposes, as a virtual
/// seam so tests can observe middleware invocations. MakeHttpMetricsSink
/// builds the production implementation.
///
/// Start carries no route: it runs before dispatch, where nothing bounded is
/// known about the path, so only the in-flight gauge can move. The route —
/// the matched Smithy operation, kHealthRoute, or kUnmatchedRoute, never the
/// raw target — arrives at completion, which is where the counters and the
/// histogram record (#1305). The method label is bounded too: the nine RFC
/// 9110 methods verbatim, every other wire token collapsed to "CUSTOM" —
/// plus "(unparsed)" from RejectionMetrics for requests the transport
/// rejected before a method existed at all (a 431 can fire mid-headers),
/// kept distinct because "never parsed" and "invented verb" are different
/// diagnoses.
class HttpMetricsSink {
 public:
  virtual ~HttpMetricsSink() = default;
  virtual void RecordRequestStart(const std::string& method) = 0;
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
///   - metrics: the gauge at start (method label only); the counters and
///     histogram at completion, labeled with the bounded route — the matched
///     Smithy operation name from the generated router, kHealthRoute for the
///     endpoint ProductionChain composes, kUnmatchedRoute for everything else
///   - one access-log line per request except health probes, which are
///     metered but not logged: the runtime's FormatAccessLog record
///     (opal/server/access_log.h — http_method, target, route, status,
///     duration_us, request_bytes, response_bytes, client, client_source,
///     handler_threw, trace_id) plus "event":"access" and service_name.
///     client is the ADR-0012 derived address the rate limiter keys on,
///     never the raw x-forwarded-for; `trusted_proxies` is the boundary it
///     derives against. Field spelling is pinned cross-rail by
///     //domains/platform/libs/otel_contract.
opal::server::Middleware ServingObservability(std::shared_ptr<HttpMetricsSink> metrics,
                                              opal::http::TrustedProxies trusted_proxies);

/// The production middleware chain around a generated server's handler,
/// shared between service mains and their middleware tests so both exercise
/// the same wiring. Observability sits outermost; health before the
/// rate-limit guard so probes are never rate limited; the guard keys on the
/// ADR-0012 derived client address (trust boundary from `trusted_proxies`)
/// and answers 429 with Retry-After. Leave `allow_request` unset for
/// services without a rate limiter — the guard is skipped entirely.
struct ChainOptions {
  std::shared_ptr<HttpMetricsSink> metrics;
  std::function<bool(const std::string& client)> allow_request = nullptr;
  opal::http::TrustedProxies trusted_proxies = opal::http::TrustedProxies::None();
  std::chrono::seconds retry_after = std::chrono::seconds(60);
};
opal::http::RequestHandler ProductionChain(ChainOptions options,
                                           opal::http::RequestHandler handler);

/// ChainOptions::trusted_proxies from TRUSTED_PROXY_CIDRS, single-sourced
/// because every service behind the same Caddy must read the trust
/// boundary identically (opal-cpp ADR-0012; the deployment pins Caddy's
/// address in deploy/consolidated/compose.yaml). Unset is the deliberate
/// direct-connect statement (TrustedProxies::None()); set-but-empty or
/// malformed logs the refusal and returns nullopt — fail startup rather
/// than silently collapsing proxied traffic onto one client key.
std::optional<opal::http::TrustedProxies> TrustedProxiesFromEnv();

/// Sink callback for BeastServerTransport::Options::on_rejected, so the
/// 413/431 rejections the transport writes before any handler chain exists
/// land in the same instruments as everything else (an over-limit flood
/// would otherwise be invisible to metrics).
std::function<void(const opal::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics);

/// Log-only observer for BeastServerTransport::Options::on_connection_event
/// (opal-cpp ADR-0013, kinds in beast_transport.h): each connection the
/// transport terminates without delivering a response gets one WARNING
/// line. Log-only because these are connections, not requests — mapping
/// them into the request-shaped instruments would distort request counts.
std::function<void(const opal::http::BeastServerTransport::ConnectionEvent&)> ConnectionEventLog();

}  // namespace aura

#endif
