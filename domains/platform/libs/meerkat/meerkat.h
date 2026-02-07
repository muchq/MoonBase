#ifndef DOMAINS_API_PLATFORM_LIBS_MEERKAT_MEERKAT_H
#define DOMAINS_API_PLATFORM_LIBS_MEERKAT_MEERKAT_H

#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "domains/platform/libs/futility/rate_limiter/sliding_window_rate_limiter.h"
#include "domains/platform/libs/meerkat/metrics_manager.h"
#include "mongoose.h"
#include "nlohmann/json.hpp"

namespace meerkat {

using json = nlohmann::json;

struct Context {
  std::chrono::steady_clock::time_point start_time;
  std::string trace_id;
  std::string route_pattern;  // For metrics: the route pattern (e.g., "/v1/trace")
};

struct HttpRequest {
  std::string method;
  std::string uri;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
  std::unordered_map<std::string, std::string> query_params;
};

struct HttpResponse {
  int status_code = 200;
  std::string body;
  std::unordered_map<std::string, std::string> headers;

  HttpResponse() { headers["Content-Type"] = "application/json"; }

  void set_json(const json& j) {
    body = j.dump();
    headers["Content-Type"] = "application/json";
  }

  void set_text(const std::string& text) {
    body = text;
    headers["Content-Type"] = "text/plain";
  }
};

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;
using RequestInterceptor = std::function<bool(HttpRequest&, HttpResponse&, Context&)>;
using ResponseInterceptor = std::function<void(const HttpRequest&, HttpResponse&, Context&)>;

uint16_t read_port(const uint16_t default_port);

class HttpServer {
 public:
  HttpServer();
  ~HttpServer();

  // Non-copyable, non-moveable (mongoose mgr can't be safely moved)
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

  // Route registration methods
  void get(const std::string& path, RouteHandler handler);
  void post(const std::string& path, RouteHandler handler);
  void put(const std::string& path, RouteHandler handler);
  void del(const std::string& path, RouteHandler handler);
  void route(const std::string& method, const std::string& path, RouteHandler handler);

  // Interceptor registration
  void use_request_interceptor(RequestInterceptor interceptor);
  void use_response_interceptor(ResponseInterceptor interceptor);

  // Server control
  bool listen(const std::string& address, int port);
  void stop();
  bool is_running() const { return running_; }
  bool is_listening() const { return listener_ != nullptr && listener_->is_listening; }
  int get_port() const;

  // Non-blocking poll (call in a loop)
  void poll(int timeout_ms = 100);

  // Blocking run method
  void run();

  // Health Checks
  void enable_health_checks();

  // Request Tracing
  void enable_tracing();

  // Metrics
  void enable_metrics(const std::string& service_name);
  void disable_metrics();

 private:
  struct mg_mgr mgr_;
  struct mg_connection* listener_;
  bool running_;
  bool mgr_initialized_;

  // Deferred listening parameters
  std::string listen_address_;
  int listen_port_;
  bool should_listen_;

  struct Route {
    std::string method;
    std::string path;
    RouteHandler handler;
  };

  std::vector<Route> routes_;
  std::vector<RequestInterceptor> request_interceptors_;
  std::vector<ResponseInterceptor> response_interceptors_;

  // Metrics
  std::shared_ptr<HttpMetricsManager> metrics_manager_;
  bool metrics_enabled_ = false;

  static void event_handler(struct mg_connection* c, int ev, void* ev_data);
  void handle_request(struct mg_connection* c, struct mg_http_message* hm);

  HttpRequest parse_request(struct mg_http_message* hm) const;
  void send_response(struct mg_connection* c, const HttpResponse& response) const;

  RouteHandler* find_handler(const std::string& method, const std::string& uri);
  std::unordered_map<std::string, std::string> parse_query_params(const std::string& query) const;

  // Metrics helpers
  std::string ExtractRoutePattern(const std::string& uri, const std::string& method) const;
};

// Utility functions for request handling
namespace requests {
template <typename T>
absl::StatusOr<T> read_request(const HttpRequest& request) {
  try {
    const json request_json = json::parse(request.body);
    return request_json.template get<T>();
  } catch (const json::exception& e) {
    return absl::Status(absl::StatusCode::kInvalidArgument, std::string(e.what()));
  }
}
}  // namespace requests

// Utility functions for common responses
namespace responses {

HttpResponse wrap(const absl::StatusOr<json> status_or_data);
HttpResponse ok(const json& data = json::object());
HttpResponse created(const json& data = json::object());
HttpResponse bad_request(const std::string& message = "Bad Request");
HttpResponse not_found(const std::string& message = "Not Found");
HttpResponse too_many_requests(const std::string& message = "Too Many Requests");
HttpResponse internal_error(const std::string& message = "Internal Server Error");
}  // namespace responses

namespace interceptors {
namespace request {
RequestInterceptor trace_id();
RequestInterceptor rate_limiter(
    std::shared_ptr<futility::rate_limiter::SlidingWindowRateLimiter<std::string>> ip_rate_limiter);
RequestInterceptor metrics(std::shared_ptr<HttpMetricsManager> manager);
}  // namespace request
namespace response {
ResponseInterceptor trace_id_header();
ResponseInterceptor logging();
ResponseInterceptor metrics(std::shared_ptr<HttpMetricsManager> manager);
}  // namespace response
}  // namespace interceptors

template <typename REQ, typename RESP>
std::function<HttpResponse(HttpRequest)> wrap(std::function<absl::StatusOr<RESP>(REQ&)> handler) {
  return [&handler](const HttpRequest& req) -> HttpResponse {
    absl::StatusOr<REQ> status_or_request = requests::read_request<REQ>(req);
    if (!status_or_request.ok()) {
      return responses::bad_request(
          absl::StrCat("Invalid JSON: ", status_or_request.status().message()));
    }
    auto status_or_response = handler(status_or_request.value());
    return responses::wrap(status_or_response);
  };
}

}  // namespace meerkat

#endif  // DOMAINS_API_PLATFORM_LIBS_MEERKAT_MEERKAT_H
