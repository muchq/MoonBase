#include "cpp/meerkat/meerkat.h"
#include "cpp/portrait/types.h"
#include "cpp/tracy/tracy.h"

using namespace meerkat;
using namespace portrait;

uint16_t readPort(const uint16_t default_port) {
  if (const char* env_p = std::getenv("PORT")) {
    return static_cast<uint16_t>(std::atoi(env_p));
  }
  return default_port;
}

int main() {
  std::cout << "Starting Portrait Server..." << std::endl;

  HttpServer server;

  // Health check endpoint
  server.get("/health", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"status", "healthy"}, {"timestamp", std::time(nullptr)}});
  });

  // Create a new user
  server.post("/api/trace", [](const HttpRequest& req) -> HttpResponse {
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

  // Add logging middleware
  server.use_middleware([](const HttpRequest& req, HttpResponse& res) -> bool {
    std::cout << "[" << std::time(nullptr) << "] " << req.method << " " << req.uri << std::endl;
    return true;
  });

  // Enable CORS for development
  server.allow_all_origins();

  // Start the server
  const std::string host = "0.0.0.0";
  const int port = readPort(8080);

  if (server.listen(host, port)) {
    std::cout << "Portrait Server running on http://" << host << ":" << port << std::endl;
    std::cout << "Serving:" << std::endl;
    std::cout << "  GET  http://localhost:8080/health" << std::endl;
    std::cout << "  POST http://localhost:8080/api/trace" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;

    server.run();
  } else {
    std::cerr << "Failed to start server on " << host << ":" << port << std::endl;
    return 1;
  }

  return 0;
}