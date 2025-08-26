#include "absl/log/initialize.h"
#include "absl/log/log.h"
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

int main() {
  absl::InitializeLog();
  LOG(INFO) << "Starting Portrait Server...";

  HttpServer server;
  TracerService tracer_service;

  // Create a new user
  server.post("/v1/trace", [](const HttpRequest& req) -> HttpResponse {
    try {
      json request_json = json::parse(req.body);
      auto trace_request = request_json.template get<TraceRequest>();

      auto trace_request_status = validateTraceRequest(trace_request);
      if (!trace_request_status.ok()) {
        return responses::bad_request(trace_request_status.message().data());
      }

      return responses::ok("yes");

    } catch (const json::exception& e) {
      return responses::bad_request("Invalid JSON: " + std::string(e.what()));
    }
  });

  server.enable_health_checks();
  server.use_middleware(middleware::request_logging());
  server.allow_all_origins();

  const std::string host = "0.0.0.0";
  const int port = readPort(8080);

  if (server.listen(host, port)) {
    LOG(INFO) << "Portrait Server running on http://" << host << ":" << port;
    LOG(INFO) << "Serving:";
    LOG(INFO) << "  GET  http://localhost:8080/health";
    LOG(INFO) << "  POST http://localhost:8080/v1/trace";
    LOG(INFO) << std::endl;
    LOG(INFO) << "Press Ctrl+C to stop the server";

    server.run();
  } else {
    LOG(ERROR) << "Failed to start server on " << host << ":" << port;
    return 1;
  }

  return 0;
}