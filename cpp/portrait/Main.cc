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

uint16_t readPort(const uint16_t default_port) {
  if (const char* env_p = std::getenv("PORT")) {
    return static_cast<uint16_t>(std::atoi(env_p));
  }
  return default_port;
}

absl::StatusOr<TraceRequest> readTraceRequest(const std::string& body) {
  try {
    const json request_json = json::parse(body);
    auto trace_request = request_json.template get<TraceRequest>();

    auto trace_request_status = validateTraceRequest(trace_request);
    if (!trace_request_status.ok()) {
      return trace_request_status;
    }

    return trace_request;
  } catch (const json::exception& e) {
    return absl::Status(absl::StatusCode::kInvalidArgument, std::string(e.what()));
  }
}

int main() {
  absl::InitializeLog();
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);
  LOG(INFO) << "Starting Portrait Server...";

  HttpServer server;
  TracerService tracer_service;

  // ray tracing endpoint
  server.post("/v1/trace", [&tracer_service](const HttpRequest& req) -> HttpResponse {
    absl::StatusOr<TraceRequest> trace_or_status = readTraceRequest(req.body);
    if (!trace_or_status.ok()) {
      return responses::bad_request(
          absl::StrCat("Invalid JSON: ", trace_or_status.status().message()));
    }
    const json response = tracer_service.trace(trace_or_status.value());
    return responses::ok(response);
  });

  server.enable_health_checks();
  server.enable_tracing();
  server.use_response_interceptor(interceptors::response::logging());
  server.allow_all_origins();

  const std::string host = "0.0.0.0";
  const int port = readPort(8080);

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