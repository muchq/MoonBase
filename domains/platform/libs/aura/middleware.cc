#include "domains/platform/libs/aura/middleware.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/http_metrics.h"
#include "smithy/http/trace_context.h"
#include "smithy/http/transport.h"

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
    case Kind::kUpgradeFailure:
      return "upgrade_failure";
  }
  // Reached only when a pin bump adds a Kind this mapping doesn't know yet
  // (-Wswitch flags the missing case, but it isn't an error here): surface
  // the numeric value so the log line stays diagnosable — then add the case.
  return "unknown(" + std::to_string(static_cast<int>(kind)) + ")";
}

// JSON string escaping for the access-log line. Every byte below 0x20 is
// escaped (\uXXXX, or the short form for \n \r \t) and invalid UTF-8 is
// replaced with U+FFFD, because the target reaches the line verbatim and is
// attacker-controlled - a raw control byte or an unescaped quote terminates
// the record early and lets the rest of the URI masquerade as its own log
// entry (smithy-cpp #203), and a stray non-UTF-8 byte (legal in a request
// target per Beast's parser) would make the one record describing that
// request the record strict JSON parsers reject.
void AppendJsonEscaped(std::string& out, std::string_view value) {
  static constexpr char kHex[] = "0123456789abcdef";
  static constexpr char kReplacement[] = "\xEF\xBF\xBD";  // U+FFFD
  for (size_t i = 0; i < value.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(value[i]);
    switch (c) {
      case '"':
        out += "\\\"";
        continue;
      case '\\':
        out += "\\\\";
        continue;
      case '\n':
        out += "\\n";
        continue;
      case '\r':
        out += "\\r";
        continue;
      case '\t':
        out += "\\t";
        continue;
    }
    if (c < 0x20) {
      out += "\\u00";
      out += kHex[(c >> 4) & 0xF];
      out += kHex[c & 0xF];
      continue;
    }
    if (c < 0x80) {
      out += static_cast<char>(c);
      continue;
    }
    // Multi-byte lead: accept a well-formed sequence whole, replace anything
    // else. Truncated sequences and stray continuation bytes both land here.
    const int continuations = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : -1;
    bool valid = continuations > 0 && i + continuations < value.size();
    for (int k = 1; valid && k <= continuations; ++k) {
      valid = (static_cast<unsigned char>(value[i + k]) & 0xC0) == 0x80;
    }
    if (valid) {
      out.append(value.substr(i, continuations + 1));
      i += continuations;
    } else {
      out += kReplacement;
    }
  }
}

void AppendJsonField(std::string& out, std::string_view name, std::string_view value) {
  out += ",\"";
  out += name;
  out += "\":\"";
  AppendJsonEscaped(out, value);
  out += '"';
}

void AppendJsonNumber(std::string& out, std::string_view name, long long value) {
  out += ",\"";
  out += name;
  out += "\":";
  out += std::to_string(value);
}

// absl truncates a LOG message at its 15000-byte buffer, and a truncated
// record is unparseable JSON - so the two unbounded, caller-controlled
// fields are capped well under it. 2KB of target is more than any
// legitimate route needs and enough of a hostile one to be diagnosable.
std::string_view Capped(std::string_view value, size_t cap) {
  return value.substr(0, std::min(value.size(), cap));
}

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

// One access-log line per request: a single JSON object in the metrics
// vocabulary (#1459) — http_method and route are the bounded labels the
// instruments carry, so a dashboard-to-logs pivot is a copy-paste; the raw
// target rides in its own field where an unbounded value is data, not a
// label. duration_us matches the histogram's unit.
//
// Kept separate from Observe because the line needs X-Forwarded-For and the
// response body size, which RequestObservation doesn't carry; it measures
// its own duration for the line only. The trace_id field is the W3C trace
// id parsed from the request's traceparent — the transport guard mints or
// joins it at ingress (smithy-cpp ADR-0011), so on transport-served
// requests it always parses. Empty only for hand-driven handler chains in
// tests.
//
// x_forwarded_for is the raw header, which since ADR-0012 is NOT the
// identity the rate limiter keys on — a 429's actual bucket (the derived
// client address) is not on this line.
smithy::server::Middleware AccessLog() {
  return [](smithy::http::RequestHandler next) {
    return [next = std::move(next)](
               const smithy::http::HttpRequest& request) -> smithy::http::HttpResponse {
      const auto start = std::chrono::steady_clock::now();

      smithy::http::HttpResponse response = next(request);

      const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start);
      const std::string trace_id =
          smithy::http::ParseTraceparent(request.headers.Get("traceparent").value_or(""))
              .value_or(smithy::http::TraceContext{})
              .trace_id;
      std::string line = R"({"event":"access")";
      AppendJsonField(line, "service_name", ServiceNameFromEnv());
      AppendJsonField(line, "http_method", MethodLabelOf(request.method));
      AppendJsonField(line, "route", RouteLabelOf(response.operation, request.target));
      AppendJsonField(line, "target", Capped(request.target, 2048));
      AppendJsonNumber(line, "status", response.status);
      AppendJsonNumber(line, "duration_us", duration_us.count());
      AppendJsonNumber(line, "response_bytes", static_cast<long long>(response.body.size()));
      AppendJsonField(line, "trace_id", trace_id);
      AppendJsonField(line, "x_forwarded_for",
                      Capped(request.headers.Get("X-Forwarded-For").value_or(""), 256));
      line += '}';
      LOG(INFO) << line;
      return response;
    };
  };
}

}  // namespace

std::shared_ptr<HttpMetricsSink> MakeHttpMetricsSink(
    std::shared_ptr<futility::otel::HttpMetricsManager> metrics) {
  return std::make_shared<OtelHttpMetricsSink>(std::move(metrics));
}

smithy::server::Middleware ServingObservability(std::shared_ptr<HttpMetricsSink> metrics) {
  return [metrics = std::move(metrics)](smithy::http::RequestHandler next) {
    // Metrics ride the runtime's Observe: microsecond durations (as of
    // smithy-cpp cfd8299) and start/complete guaranteed to pair even when
    // dispatch throws. The completion carries the observation's operation —
    // the matched handler the router annotated — which RouteLabelOf turns
    // into the bounded route label (#1305).
    smithy::server::Middleware observe = smithy::server::Observe(
        [metrics](const smithy::server::RequestObservation& observation) {
          metrics->RecordRequestComplete(RouteLabelOf(observation.operation, observation.target),
                                         MethodLabelOf(observation.method), observation.status,
                                         observation.duration);
        },
        [metrics](const smithy::server::RequestStart& start) {
          metrics->RecordRequestStart(MethodLabelOf(start.method));
        });
    return observe(AccessLog()(std::move(next)));
  };
}

smithy::http::RequestHandler ProductionChain(ChainOptions options,
                                             smithy::http::RequestHandler handler) {
  std::vector<smithy::server::Middleware> chain = {ServingObservability(std::move(options.metrics)),
                                                   smithy::server::HealthEndpoint(kHealthRoute)};
  if (options.allow_request) {
    chain.push_back(smithy::server::PerClientRateLimit(
        std::move(options.allow_request), std::move(options.trusted_proxies), options.retry_after));
  }
  return smithy::server::Chain(std::move(chain), std::move(handler));
}

std::function<void(const smithy::http::BeastServerTransport::RejectedRequest&)> RejectionMetrics(
    std::shared_ptr<HttpMetricsSink> metrics) {
  return [metrics = std::move(metrics)](
             const smithy::http::BeastServerTransport::RejectedRequest& rejected) {
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

std::function<void(const smithy::http::BeastServerTransport::ConnectionEvent&)>
ConnectionEventLog() {
  return [](const smithy::http::BeastServerTransport::ConnectionEvent& event) {
    // One line, no locks beyond the logger's own.
    LOG(WARNING) << "connection_event kind=" << KindName(event.kind)
                 << " peer=" << event.peer_address << " detail=" << event.detail << " elapsed_ms="
                 << std::chrono::duration_cast<std::chrono::milliseconds>(event.elapsed).count();
  };
}

std::optional<smithy::http::TrustedProxies> TrustedProxiesFromEnv() {
  if (std::getenv("TRUSTED_PROXY_CIDRS") == nullptr) {
    return smithy::http::TrustedProxies::None();
  }
  const std::vector<std::string> cidrs = futility::env::ReadList("TRUSTED_PROXY_CIDRS");
  if (cidrs.empty()) {
    LOG(ERROR) << "TRUSTED_PROXY_CIDRS is set but empty; unset it to serve direct-connect";
    return std::nullopt;
  }
  auto parsed = smithy::http::TrustedProxies::Parse(cidrs);
  if (!parsed.ok()) {
    LOG(ERROR) << "Invalid TRUSTED_PROXY_CIDRS: " << parsed.error().message();
    return std::nullopt;
  }
  return std::move(parsed).value();
}

}  // namespace aura
