#include "domains/platform/libs/aura/middleware.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/http_metrics.h"
#include "opal/http/transport.h"
#include "opal/server/access_log.h"

namespace aura {
namespace {

class OtelHttpMetricsSink final : public HttpMetricsSink {
 public:
  explicit OtelHttpMetricsSink(std::shared_ptr<futility::otel::HttpMetricsManager> metrics)
      : metrics_(std::move(metrics)) {}

  void RecordRequestStart(const std::string& method) override {
    metrics_->RecordRequestStart(method);
  }
  void RecordRequestComplete(const std::string& route, const std::string& method, int status_code,
                             std::chrono::microseconds duration) override {
    metrics_->RecordRequestComplete(route, method, status_code, duration);
  }

 private:
  std::shared_ptr<futility::otel::HttpMetricsManager> metrics_;
};

std::string PathOf(const std::string& target) { return target.substr(0, target.find('?')); }

// The bounded route label (#1305). The generated router stamps the matched
// operation name onto the response for exactly this purpose, and that name —
// not the URI pattern, which smithy doesn't expose to middleware — is the
// bounded vocabulary this rail speaks. The health endpoint is middleware, not
// a routed operation, so it is recognized by its path and keeps the literal
// prom_proxy's probeFilter subtracts — any method, on this rail: a POST that
// 405s is still health-path traffic, excluded from Serving like the probes.
// (yodel differs on that edge — no template is stamped on a 405, so a
// wrong-method /health lands under its sentinel and stays in Serving; its
// filter test pins that. Probes are GET, so the rails agree where it
// matters.) Everything
// else shares one sentinel: 404s, 405s, rate-limited requests, transport
// rejections, scanner noise, and the one server-side case — a handler that
// throws (rather than returning an error response) completes as a 500 with
// no operation annotation, so it lands here too, separable by
// error_type="server_error". The raw target minted a Prometheus series per
// distinct path a scanner tried, which is the unbounded-cardinality shape
// this replaces.
std::string RouteLabelOf(const std::string& operation, const std::string& target) {
  if (!operation.empty()) return operation;
  if (PathOf(target) == kHealthRoute) return kHealthRoute;
  return kUnmatchedRoute;
}

// The bounded method label: the nine RFC 9110 methods pass through verbatim,
// anything else collapses to "CUSTOM" (yodel's spelling for the same rule —
// Micronaut's HttpMethod enum does it for the Java rail). Beast hands the
// chain the raw wire token (`wire.method_string()`), so without this the
// method is a client-controlled label on every instrument, the gauge
// included, and a scanner spraying invented verbs mints a series per token —
// the same unbounded shape the route sentinel exists to prevent (#1305).
// Case-sensitive on purpose: methods are case-sensitive, so "get" is an
// invented token, not GET.
std::string MethodLabelOf(const std::string& method) {
  static constexpr std::string_view kKnown[] = {"GET",     "HEAD",    "POST",  "PUT",  "DELETE",
                                                "CONNECT", "OPTIONS", "TRACE", "PATCH"};
  for (const std::string_view known : kKnown) {
    if (method == known) return method;
  }
  return "CUSTOM";
}

std::string KindName(opal::http::BeastServerTransport::ConnectionEvent::Kind kind) {
  using Kind = opal::http::BeastServerTransport::ConnectionEvent::Kind;
  switch (kind) {
    case Kind::kTlsHandshakeFailure:
      return "tls_handshake_failure";
    case Kind::kFramingError:
      return "framing_error";
    case Kind::kReadTimeout:
      return "read_timeout";
    case Kind::kDropped:
      return "dropped";
    case Kind::kUpgradeFailure:
      return "upgrade_failure";
  }
  // Reached only when a pin bump adds a Kind this mapping doesn't know yet
  // (-Wswitch flags the missing case, but it isn't an error here): surface
  // the numeric value so the log line stays diagnosable — then add the case.
  return "unknown(" + std::to_string(static_cast<int>(kind)) + ")";
}

// absl truncates a LOG message at its 15000-byte buffer, and a truncated
// record is unparseable JSON - so the one unbounded, caller-controlled
// field is capped well under it. 2KB of target is more than any legitimate
// route needs and enough of a hostile one to be diagnosable. A cut inside a
// multi-byte sequence is fine: the formatter replaces the stray lead with
// U+FFFD.
constexpr size_t kMaxLoggedTarget = 2048;

// The log's identity, from the compose contract (OTEL_SERVICE_NAME).
// Note the C++ metrics resource does NOT read this variable - each
// service compiles its name into OtelConfig - so the two agree by
// convention, not construction.
const std::string& ServiceNameFromEnv() {
  static const std::string name = []() {
    const char* value = std::getenv("OTEL_SERVICE_NAME");
    return std::string(value == nullptr ? "" : value);
  }();
  return name;
}

// One access-log line per request, except health probes: the runtime's
// FormatAccessLog owns the record (field order, escaping, the derived
// client and its provenance), aura adds the identity every dashboard
// selects on (service_name, from the compose contract) and "event", which
// server_pal's line carries under the same spelling. Probes stay in the
// metrics (prom_proxy subtracts exactly that route) but out of the log: a
// probe every few seconds per replica would otherwise be most of every
// service's log volume.
//
// The runtime formats target verbatim; the cap keeps a hostile URI under
// absl's buffer so the record stays one parseable object.
void LogAccess(const opal::server::RequestObservation& observation) {
  if (RouteLabelOf(observation.operation, observation.target) == kHealthRoute) return;
  if (observation.target.size() <= kMaxLoggedTarget) {
    LOG(INFO) << opal::server::FormatAccessLog(
        observation, {{"event", "access"}, {"service_name", ServiceNameFromEnv()}});
    return;
  }
  opal::server::RequestObservation capped = observation;
  capped.target.resize(kMaxLoggedTarget);
  LOG(INFO) << opal::server::FormatAccessLog(
      capped, {{"event", "access"}, {"service_name", ServiceNameFromEnv()}});
}

}  // namespace

std::shared_ptr<HttpMetricsSink> MakeHttpMetricsSink(
    std::shared_ptr<futility::otel::HttpMetricsManager> metrics) {
  return std::make_shared<OtelHttpMetricsSink>(std::move(metrics));
}

opal::server::Middleware ServingObservability(std::shared_ptr<HttpMetricsSink> metrics,
                                              opal::http::TrustedProxies trusted_proxies) {
  // Metrics and the access line both ride the runtime's Observe: microsecond
  // durations and start/complete guaranteed to pair even when dispatch
  // throws. The completion carries the observation's operation — the matched
  // handler the router annotated — which RouteLabelOf turns into the bounded
  // route label (#1305). The trust boundary is what lets the observation
  // derive the ADR-0012 client the log line reports.
  return opal::server::Observe(
      [metrics](const opal::server::RequestObservation& observation) {
        metrics->RecordRequestComplete(RouteLabelOf(observation.operation, observation.target),
                                       MethodLabelOf(observation.method), observation.status,
                                       observation.duration);
        LogAccess(observation);
      },
      [metrics](const opal::server::RequestStart& start) {
        metrics->RecordRequestStart(MethodLabelOf(start.method));
      },
      /*now=*/nullptr, std::move(trusted_proxies));
}

opal::http::RequestHandler ProductionChain(ChainOptions options,
                                           opal::http::RequestHandler handler) {
  std::vector<opal::server::Middleware> chain = {
      ServingObservability(std::move(options.metrics), options.trusted_proxies),
      opal::server::HealthEndpoint(kHealthRoute)};
  if (options.allow_request) {
    chain.push_back(opal::server::PerClientRateLimit(
        std::move(options.allow_request), std::move(options.trusted_proxies), options.retry_after));
  }
  return opal::server::Chain(std::move(chain), std::move(handler));
}

std::function<void(const opal::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics) {
  return [metrics = std::move(metrics)](
             const opal::http::BeastServerTransport::RejectedRequest& rejected) {
    // A rejection fires before any routing, so the route is always the
    // sentinel — a 413 flood against distinct paths must not mint a series
    // per path (#1305), and the method is bounded like everywhere else. The
    // method may also be empty when the request never parsed that far (a 431
    // can fire mid-headers); keep those series on a stable label rather than
    // an empty string dashboards would drop or misgroup.
    const std::string method =
        rejected.method.empty() ? "(unparsed)" : MethodLabelOf(rejected.method);
    // Start + complete keeps the active gauge symmetric; the rejection
    // happens at parse time, so zero duration is accurate.
    metrics->RecordRequestStart(method);
    metrics->RecordRequestComplete(kUnmatchedRoute, method, rejected.status,
                                   std::chrono::microseconds{0});
  };
}

std::function<void(const opal::http::BeastServerTransport::ConnectionEvent&)> ConnectionEventLog() {
  return [](const opal::http::BeastServerTransport::ConnectionEvent& event) {
    // One line, no locks beyond the logger's own.
    LOG(WARNING) << "connection_event kind=" << KindName(event.kind)
                 << " peer=" << event.peer_address << " detail=" << event.detail << " elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(event.elapsed).count();
  };
}

std::optional<opal::http::TrustedProxies> TrustedProxiesFromEnv() {
  if (std::getenv("TRUSTED_PROXY_CIDRS") == nullptr) {
    return opal::http::TrustedProxies::None();
  }
  const std::vector<std::string> cidrs = futility::env::ReadList("TRUSTED_PROXY_CIDRS");
  if (cidrs.empty()) {
    LOG(ERROR) << "TRUSTED_PROXY_CIDRS is set but empty; unset it to serve direct-connect";
    return std::nullopt;
  }
  auto parsed = opal::http::TrustedProxies::Parse(cidrs);
  if (!parsed.ok()) {
    LOG(ERROR) << "Invalid TRUSTED_PROXY_CIDRS: " << parsed.error().message();
    return std::nullopt;
  }
  return std::move(parsed).value();
}

}  // namespace aura
