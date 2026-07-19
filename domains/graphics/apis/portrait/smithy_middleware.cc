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

std::string KindName(smithy::http::BeastServerTransport::ConnectionEvent::Kind kind) {
  using Kind = smithy::http::BeastServerTransport::ConnectionEvent::Kind;
  switch (kind) {
    case Kind::kTlsHandshakeFailure:
      return "tls_handshake_failure";
    case Kind::kFramingError:
      return "framing_error";
    case Kind::kReadTimeout:
      return "read_timeout";
    case Kind::kDropped:
      return "dropped";
  }
  // Reached only when a pin bump adds a Kind this mapping doesn't know yet
  // (-Wswitch flags the missing case, but it isn't an error here): surface
  // the numeric value so the log line stays diagnosable — then add the case.
  return "unknown(" + std::to_string(static_cast<int>(kind)) + ")";
}

// Meerkat's access-log line shape. Kept separate from Observe because the
// log line needs X-Forwarded-For and the response body size, which
// RequestObservation doesn't carry; it measures its own duration for the
// log line only. The trace_id= field is the W3C trace id parsed from the
// request's traceparent — the transport guard mints or joins it at ingress
// (smithy-cpp ADR-0011), so on transport-served requests it always parses.
// Empty only for hand-driven handler chains in tests.
//
// X-Forwarded-For= is deliberately the raw header (the line shape meerkat's
// dashboards parse), which since ADR-0012 is NOT the identity the rate
// limiter keys on — a 429's actual bucket (the derived client address) is
// not on this line. Logging the derived client is a post-soak TODO once the
// meerkat line-shape constraint lifts (PORTRAIT_TODO.md).
smithy::server::Middleware AccessLog() {
  return [](smithy::http::RequestHandler next) {
    return [next = std::move(next)](
               const smithy::http::HttpRequest& request) -> smithy::http::HttpResponse {
      const auto start = std::chrono::steady_clock::now();

      smithy::http::HttpResponse response = next(request);

      const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      const std::string trace_id =
          smithy::http::ParseTraceparent(request.headers.Get("traceparent").value_or(""))
              .value_or(smithy::http::TraceContext{})
              .trace_id;
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
    // method/target may be empty when the request never parsed that far (a
    // 431 can fire mid-headers); keep those series on a stable label rather
    // than an empty string dashboards would drop or misgroup.
    const std::string route = rejected.target.empty() ? "(unparsed)" : RouteOf(rejected.target);
    const std::string method = rejected.method.empty() ? "(unparsed)" : rejected.method;
    // Start + complete keeps the request counter and active gauge symmetric;
    // the rejection happens at parse time, so zero duration is accurate.
    metrics->RecordRequestStart(route, method);
    metrics->RecordRequestComplete(route, method, rejected.status, std::chrono::microseconds{0});
  };
}

std::function<void(const smithy::http::BeastServerTransport::ConnectionEvent&)>
ConnectionEventLog() {
  return [](const smithy::http::BeastServerTransport::ConnectionEvent& event) {
    // One line, no locks beyond the logger's own.
    LOG(WARNING) << "connection_event kind=" << KindName(event.kind)
                 << " peer=" << event.peer_address << " detail=" << event.detail << " elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(event.elapsed).count();
  };
}

}  // namespace portrait
