// The Golf hub server, phase 2 (#1187): sessions, rooms, chat, and the
// golf game layer on smithy-cpp's streaming stack — generated async
// handlers (ADR-0021), SessionRegistry fan-out with reconnect grace
// (ADR-0017/0020/0022), the JSON-text browser wire (ADR-0018).
//
//   bazel run //domains/games/apis/golf_hub
//   curl -X POST localhost:8080/games/v2/session -H 'content-type: application/json' -d '{}'
//   # browser: new WebSocket("ws://localhost:8080/games/v2/golf/play?ticket=<t>",
//   #                        "smithy.eventstream.v1+json")
//   kill -TERM <pid>   # drains sessions, then exits 0

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "domains/games/apis/golf_hub/hub_handler.h"
#include "domains/games/apis/golf_hub/id_generator.h"
#include "domains/games/apis/golf_hub/ticket_vault.h"
#include "domains/games/libs/cards/dealer.h"
#include "domains/platform/libs/aura/middleware.h"
#include "domains/platform/libs/futility/env/env.h"
#include "domains/platform/libs/futility/otel/http_metrics.h"
#include "domains/platform/libs/futility/otel/metrics.h"
#include "domains/platform/libs/futility/otel/otel_provider.h"
#include "moonbase/golf/server.h"
#include "smithy/http/beast_transport.h"
#include "smithy/http/forwarded.h"
#include "smithy/http/message.h"
#include "smithy/server/origin_gate.h"

namespace {

// The ticket query member, pre-101 (ADR-0018's blessed pattern): presence
// and vault freshness checked at the gate; the handler's SpendTicket stays
// the single-use authority.
std::optional<std::string> ExtractQueryParam(std::string_view target, std::string_view name) {
  const auto question = target.find('?');
  if (question == std::string_view::npos) return std::nullopt;
  std::string_view query = target.substr(question + 1);
  while (!query.empty()) {
    const auto amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    query = amp == std::string_view::npos ? std::string_view{} : query.substr(amp + 1);
    const auto eq = pair.find('=');
    if (eq != std::string_view::npos && pair.substr(0, eq) == name) {
      return std::string(pair.substr(eq + 1));
    }
  }
  return std::nullopt;
}

}  // namespace

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // OTel setup: OTLP push, service "golf_hub".
  futility::otel::OtelConfig otel_config{.service_name = "golf_hub", .service_version = "1.0.0"};
  futility::otel::OtelProvider otel_provider(otel_config);

  auto vault = std::make_shared<golf_hub::TicketVault>(
      /*ticket_ttl=*/std::chrono::seconds(30), /*resume_ttl=*/std::chrono::hours(24));
  // Stream-side instruments (sessions, commands, events) ride the same
  // meter the aura chain's unary instruments use.
  auto handler = std::make_shared<golf_hub::HubHandler>(
      vault, std::make_shared<cards::Dealer>(), std::make_shared<golf_hub::WhimsicalIdGenerator>(),
      /*grace_period=*/std::chrono::minutes(5),
      std::make_shared<futility::otel::MetricsRecorder>("golf_hub"));

  // Block shutdown signals before the transport spawns its thread pool.
  sigset_t shutdown_signals;
  sigemptyset(&shutdown_signals);
  sigaddset(&shutdown_signals, SIGINT);
  sigaddset(&shutdown_signals, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &shutdown_signals, nullptr);

  moonbase::golf::GolfHubServer server(handler);

  // Unary neighbors (GetSession, health) on the shared aura chain:
  // http_server_* instruments + access log outermost, then health. No rate
  // limiter yet — session minting is already gated by the ticket vault, and
  // a per-client budget can slot into ChainOptions when it earns its keep.
  auto metrics =
      aura::MakeHttpMetricsSink(std::make_shared<futility::otel::HttpMetricsManager>("golf_hub"));

  // The reverse-proxy trust boundary (smithy-cpp ADR-0012, contract in
  // smithy/http/forwarded.h): deploy/consolidated/compose.yaml pins Caddy's
  // address and passes it here. Unset is the deliberate direct-connect
  // statement (TrustedProxies::None()); set-but-empty or malformed fails
  // startup rather than silently collapsing proxied traffic onto one key.
  smithy::http::TrustedProxies trusted_proxies = smithy::http::TrustedProxies::None();
  if (std::getenv("TRUSTED_PROXY_CIDRS") != nullptr) {
    const std::vector<std::string> cidrs = futility::env::ReadList("TRUSTED_PROXY_CIDRS");
    if (cidrs.empty()) {
      LOG(ERROR) << "TRUSTED_PROXY_CIDRS is set but empty; unset it to serve direct-connect";
      return 1;
    }
    try {
      trusted_proxies = smithy::http::TrustedProxies(cidrs);
    } catch (const std::invalid_argument& error) {
      LOG(ERROR) << "Invalid TRUSTED_PROXY_CIDRS: " << error.what();
      return 1;
    }
  }

  auto unary = aura::ProductionChain(
      aura::ChainOptions{.metrics = metrics, .trusted_proxies = std::move(trusted_proxies)},
      server.Handler());

  // Gate chain: origin allowlist (browser CSWSH defense; unset
  // ALLOWED_ORIGINS admits all origins — dev parity with the Go hub's
  // DEV_MODE) -> ticket freshness -> the stream router's own refusals.
  const std::vector<std::string> allowed_origins = futility::env::ReadList("ALLOWED_ORIGINS");
  auto origin_gate = allowed_origins.empty()
                         ? std::function<std::optional<smithy::http::HttpResponse>(
                               const smithy::http::HttpRequest&)>()
                         : smithy::server::RequireOrigin(allowed_origins);
  auto router_gate = server.StreamRouter()->Gate();
  smithy::http::BeastServerTransport::Options options;
  options.websocket_gate =
      [origin_gate = std::move(origin_gate), router_gate = std::move(router_gate), vault](
          const smithy::http::HttpRequest& request) -> std::optional<smithy::http::HttpResponse> {
    if (origin_gate) {
      if (auto refusal = origin_gate(request)) return refusal;
    }
    const auto ticket = ExtractQueryParam(request.target, "ticket");
    if (!ticket.has_value() || !vault->PeekTicket(*ticket)) {
      smithy::http::HttpResponse refusal;
      refusal.status = 401;
      refusal.headers.Set("content-type", "application/json");
      refusal.body = R"({"message":"mint a ticket via POST /games/v2/session"})";
      return refusal;
    }
    return router_gate(request);
  };
  options.on_websocket_session = server.StreamRouter()->ServeSession();
  options.websocket_accept_json_frames = true;  // the browser wire (ADR-0018)

  options.address = "0.0.0.0";
  options.port = futility::env::ReadPort(8080);
  // Sessions hold no threads (ADR-0021); this pool serves launch points
  // and unary requests only.
  options.handler_threads = 4;
  // GetSession bodies are tiny; nothing here needs big payloads.
  options.max_body_bytes = std::size_t{64} * 1024;
  // 413/431s the transport writes itself land in the same instruments;
  // connections it terminates without a response get a WARNING line
  // (ADR-0013).
  options.on_rejected = aura::RejectionMetrics(metrics);
  options.on_connection_event = aura::ConnectionEventLog();
  smithy::http::BeastServerTransport transport(options);

  smithy::Outcome<smithy::Unit> started = transport.Start(unary);
  if (!started.ok()) {
    LOG(ERROR) << "Failed to start golf hub on " << options.address << ":" << options.port << ": "
               << started.error().message();
    return 1;
  }

  LOG(INFO) << "Golf hub running on http://" << options.address << ":" << transport.port();
  LOG(INFO) << "  POST http://localhost:" << transport.port() << "/games/v2/session";
  LOG(INFO) << "  WS   ws://localhost:" << transport.port()
            << "/games/v2/golf/play?ticket=<ticket>";

  int signal_number = 0;
  sigwait(&shutdown_signals, &signal_number);
  LOG(INFO) << "Signal " << signal_number << " received; draining sessions";
  handler->registry().Drain(std::chrono::seconds(5));
  transport.Stop();
  return 0;
}
