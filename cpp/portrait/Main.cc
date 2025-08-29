#include <functional>

#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "cpp/meerkat/meerkat.h"
#include "cpp/portrait/types.h"
#include "tracer_service.h"

using namespace meerkat;
using namespace portrait;

template <typename REQ, typename RESP>
std::function<HttpResponse(HttpRequest)> wrap(std::function<absl::StatusOr<RESP>(REQ)> handler) {
  return [&handler](const HttpRequest& req) -> HttpResponse {
    absl::StatusOr<REQ> status_or_request = requests::read_request<REQ>(req);
    if (!status_or_request.ok()) {
      return responses::bad_request(
          absl::StrCat("Invalid JSON: ", status_or_request.status().message()));
    }
    const auto status_or_response = handler(status_or_request.value());
    return responses::wrap(status_or_response);
  };
}

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  LOG(INFO) << "Starting Portrait Server...";

  HttpServer server;
  TracerService tracer_service;

  // ray tracing endpoint
  server.post("/v1/trace",
              wrap<TraceRequest, TraceResponse>(
                  [&tracer_service](TraceRequest req) -> absl::StatusOr<TraceResponse> {
                    return tracer_service.trace(req);
                  }));

  server.enable_health_checks();
  server.enable_tracing();
  server.use_response_interceptor(interceptors::response::logging());
  server.allow_all_origins();

  const std::string host = "0.0.0.0";
  const int port = read_port(8080);

  if (server.listen(host, port)) {
    LOG(INFO) << "Portrait Server running on http://" << host << ":" << port;
    LOG(INFO) << "Serving:";
    LOG(INFO) << "  GET  http://localhost:8080/health";
    LOG(INFO) << "  POST http://localhost:8080/v1/trace";
    LOG(INFO) << "Press Ctrl+C to stop the server";

    server.run();
  } else {
    LOG(ERROR) << "Failed to start server on " << host << ":" << port;
    return 1;
  }

  return 0;
}