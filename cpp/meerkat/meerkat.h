#ifndef CPP_MEERKAT_MEERKAT_H
#define CPP_MEERKAT_MEERKAT_H

#include <chrono>
#include <functional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "mongoose.h"
#include "nlohmann/json.hpp"

namespace meerkat {

using json = nlohmann::json;

struct HttpRequest {
  std::string method;
  std::string uri;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
  std::unordered_map<std::string, std::string> query_params;
  std::chrono::steady_clock::time_point start_time;
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
using RequestInterceptor = std::function<bool(HttpRequest&, HttpResponse&)>;
using ResponseInterceptor = std::function<void(const HttpRequest&, HttpResponse&)>;
using WebSocketHandler = std::function<void(struct mg_connection*, const std::string&)>;
using WebSocketConnectHandler = std::function<bool(struct mg_connection*, const HttpRequest&)>;
using WebSocketCloseHandler = std::function<void(struct mg_connection*)>;

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

  // Non-blocking poll (call in a loop)
  void poll(int timeout_ms = 100);

  // Blocking run method
  void run();

  // Static file serving
  void serve_static(const std::string& path_prefix, const std::string& directory);

  // Health Checks
  void enable_health_checks();

  // Request Tracing
  void enable_tracing();

  // CORS configuration
  struct CorsConfig {
    std::set<std::string> allowed_origins;
    std::set<std::string> allowed_methods;
    std::set<std::string> allowed_headers;
    std::set<std::string> exposed_headers;
    bool allow_credentials = false;
    int max_age = 86400;  // 24 hours

    CorsConfig() {
      allowed_methods = {"GET", "POST", "PUT", "DELETE", "OPTIONS"};
      allowed_headers = {"Content-Type", "Authorization", "X-Requested-With"};
    }
  };

  void enable_cors(const CorsConfig& config = CorsConfig{});
  void allow_origin(const std::string& origin);
  void allow_all_origins();

  // WebSocket support
  void websocket(const std::string& path, WebSocketHandler message_handler,
                 WebSocketConnectHandler connect_handler = nullptr,
                 WebSocketCloseHandler close_handler = nullptr);

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
  std::unordered_map<std::string, std::string> static_paths_;

  // CORS configuration
  bool cors_enabled_;
  CorsConfig cors_config_;

  // WebSocket support
  struct WebSocketRoute {
    std::string path;
    WebSocketHandler message_handler;
    WebSocketConnectHandler connect_handler;
    WebSocketCloseHandler close_handler;
  };
  std::vector<WebSocketRoute> websocket_routes_;
  std::unordered_map<struct mg_connection*, std::string> websocket_connections_;

  static void event_handler(struct mg_connection* c, int ev, void* ev_data);
  void handle_request(struct mg_connection* c, struct mg_http_message* hm);

  HttpRequest parse_request(struct mg_http_message* hm) const;
  void send_response(struct mg_connection* c, const HttpResponse& response) const;

  RouteHandler* find_handler(const std::string& method, const std::string& uri);
  std::unordered_map<std::string, std::string> parse_query_params(const std::string& query) const;

  // CORS helpers
  void handle_cors_preflight(struct mg_connection* c, const HttpRequest& request);
  void add_cors_headers(HttpResponse& response, const HttpRequest& request);

  // WebSocket helpers
  WebSocketRoute* find_websocket_route(const std::string& uri);
  void handle_websocket_message(struct mg_connection* c, struct mg_ws_message* wm);
  void handle_websocket_handshake(struct mg_connection* c, struct mg_http_message* hm);
  void handle_websocket_close(struct mg_connection* c);
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
HttpResponse internal_error(const std::string& message = "Internal Server Error");
}  // namespace responses

// WebSocket utility functions
namespace websocket {
void send_text(struct mg_connection* c, const std::string& message);
void send_json(struct mg_connection* c, const json& data);
void send_binary(struct mg_connection* c, const void* data, size_t length);
void close(struct mg_connection* c, int code = 1000, const std::string& reason = "");
}  // namespace websocket

namespace interceptors {
namespace request {
RequestInterceptor trace_id();
}
namespace response {
ResponseInterceptor trace_id_header();
ResponseInterceptor logging();
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

#endif  // CPP_MEERKAT_MEERKAT_H
