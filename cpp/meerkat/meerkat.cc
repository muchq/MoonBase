#include "cpp/meerkat/meerkat.h"

#include <climits>
#include <iostream>
#include <random>
#include <sstream>
#include <string>

#include "absl/log/log.h"

namespace meerkat {

std::string TRACE_ID_HEADER_NAME{"x-trace-id"};

HttpServer::HttpServer()
    : listener_(nullptr),
      running_(false),
      mgr_initialized_(false),
      listen_port_(0),
      should_listen_(false),
      cors_enabled_(false) {
  // Initialize mgr_ to zero to ensure consistent behavior across platforms
  memset(&mgr_, 0, sizeof(mgr_));

  // Clear WebSocket connections map
  websocket_connections_.clear();
}

HttpServer::~HttpServer() {
  if (running_) {
    stop();
  }
}

void HttpServer::get(const std::string& path, RouteHandler handler) {
  route("GET", path, std::move(handler));
}

void HttpServer::post(const std::string& path, RouteHandler handler) {
  route("POST", path, std::move(handler));
}

void HttpServer::put(const std::string& path, RouteHandler handler) {
  route("PUT", path, std::move(handler));
}

void HttpServer::del(const std::string& path, RouteHandler handler) {
  route("DELETE", path, std::move(handler));
}

void HttpServer::route(const std::string& method, const std::string& path, RouteHandler handler) {
  routes_.push_back({method, path, std::move(handler)});
}

void HttpServer::use_middleware(MiddlewareHandler middleware) {
  middleware_.push_back(std::move(middleware));
}

bool HttpServer::listen(const std::string& address, int port) {
  if (running_) {
    return false;
  }

  // Store parameters for initialization in run() thread
  listen_address_ = address;
  listen_port_ = port;

  // Mark that we should start listening when run() is called
  should_listen_ = true;

  return true;
}

void HttpServer::stop() {
  if (running_) {
    running_ = false;
    // Don't close listener here - let the run() thread handle all mongoose cleanup
  }
}

void HttpServer::poll(int timeout_ms) {
  if (running_ && mgr_initialized_ && listener_) {
    mg_mgr_poll(&mgr_, timeout_ms);
  }
}

void HttpServer::run() {
  // Initialize mongoose in the polling thread to avoid thread safety issues
  if (should_listen_ && !mgr_initialized_) {
    mg_mgr_init(&mgr_);
    mgr_initialized_ = true;

    std::string url = "http://" + listen_address_ + ":" + std::to_string(listen_port_);
    listener_ = mg_http_listen(&mgr_, url.c_str(), event_handler, this);

    if (listener_ == nullptr || !listener_->is_listening) {
      // Failed to listen
      if (mgr_initialized_) {
        mg_mgr_free(&mgr_);
        mgr_initialized_ = false;
      }
      return;
    }

    running_ = true;
    should_listen_ = false;
  }

  while (running_) {
    poll();
  }

  // Clean up mongoose in the same thread where it was initialized
  if (mgr_initialized_) {
    mg_mgr_free(&mgr_);
    mgr_initialized_ = false;
    listener_ = nullptr;
  }
}

void HttpServer::serve_static(const std::string& path_prefix, const std::string& directory) {
  static_paths_[path_prefix] = directory;
}

void HttpServer::enable_health_checks() {
  get("/health", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"status", "healthy"}, {"timestamp", std::time(nullptr)}});
  });
}

void HttpServer::event_handler(struct mg_connection* c, int ev, void* ev_data) {
  // Get the server pointer - for accepted connections, we need to get it from the listener
  HttpServer* server = nullptr;

  if (c->fn_data) {
    server = static_cast<HttpServer*>(c->fn_data);
  } else if (c->mgr) {
    // For accepted connections, fn_data may not be set, so find the listener
    for (struct mg_connection* lc = c->mgr->conns; lc != nullptr; lc = lc->next) {
      if (lc->is_listening && lc->fn_data) {
        server = static_cast<HttpServer*>(lc->fn_data);
        c->fn_data = server;  // Cache it for future events
        break;
      }
    }
  }

  if (!server) {
    return;  // Safety check
  }

  if (ev == MG_EV_HTTP_MSG) {
    const auto hm = static_cast<struct mg_http_message*>(ev_data);
    server->handle_request(c, hm);
  } else if (ev == MG_EV_WS_MSG) {
    const auto wm = static_cast<struct mg_ws_message*>(ev_data);
    server->handle_websocket_message(c, wm);
  } else if (ev == MG_EV_CLOSE) {
    server->handle_websocket_close(c);
  }
}

void HttpServer::handle_request(struct mg_connection* c, struct mg_http_message* hm) {
  const HttpRequest request = parse_request(hm);
  HttpResponse response;

  // Handle CORS preflight requests
  if (cors_enabled_ && request.method == "OPTIONS") {
    handle_cors_preflight(c, request);
    return;
  }

  // Apply middleware
  for (const auto& middleware : middleware_) {
    if (!middleware(request, response)) {
      // Middleware blocked the request
      if (cors_enabled_) {
        add_cors_headers(response, request);
      }
      send_response(c, response);
      return;
    }
  }

  // Check for static file serving
  for (const auto& [prefix, directory] : static_paths_) {
    if (request.uri.starts_with(prefix)) {
      std::string file_path = directory + request.uri.substr(prefix.length());
      mg_http_serve_file(c, hm, file_path.c_str(), nullptr);
      return;
    }
  }

  // Find route handler
  RouteHandler* handler = find_handler(request.method, request.uri);

  if (handler) {
    try {
      response = (*handler)(request);
    } catch (const std::exception& e) {
      response = responses::internal_error(e.what());
    } catch (...) {
      response = responses::internal_error("Unknown error occurred");
    }
  } else {
    response = responses::not_found();
  }

  // Add CORS headers if enabled
  if (cors_enabled_) {
    add_cors_headers(response, request);
  }

  send_response(c, response);
}

HttpRequest HttpServer::parse_request(struct mg_http_message* hm) const {
  HttpRequest request;

  request.method = std::string(hm->method.buf, hm->method.len);
  request.uri = std::string(hm->uri.buf, hm->uri.len);

  // Parse query params
  if (hm->query.len > 0) {
    std::string query(hm->query.buf, hm->query.len);
    request.query_params = parse_query_params(query);
  }

  request.body = std::string(hm->body.buf, hm->body.len);

  for (int i = 0; i < MG_MAX_HTTP_HEADERS && hm->headers[i].name.buf; i++) {
    std::string name(hm->headers[i].name.buf, hm->headers[i].name.len);
    std::string value(hm->headers[i].value.buf, hm->headers[i].value.len);
    request.headers[name] = value;
  }

  return request;
}

void HttpServer::send_response(struct mg_connection* c, const HttpResponse& response) const {
  std::ostringstream headers_stream;

  for (const auto& [key, value] : response.headers) {
    // mg_http_reply adds Content-Length
    if (key != "Content-Length") {
      headers_stream << key << ": " << value << "\r\n";
    }
  }

  std::string headers_str = headers_stream.str();

  mg_http_reply(c, response.status_code, headers_str.c_str(), response.body.c_str());
}

RouteHandler* HttpServer::find_handler(const std::string& method, const std::string& uri) {
  for (auto& route : routes_) {
    if (route.method == method && route.path == uri) {
      return &route.handler;
    }
  }
  return nullptr;
}

std::unordered_map<std::string, std::string> HttpServer::parse_query_params(
    const std::string& query) const {
  std::unordered_map<std::string, std::string> params;
  std::istringstream stream(query);
  std::string pair;

  while (std::getline(stream, pair, '&')) {
    size_t eq_pos = pair.find('=');
    if (eq_pos != std::string::npos) {
      std::string key = pair.substr(0, eq_pos);
      std::string value = pair.substr(eq_pos + 1);
      params[key] = value;
    } else {
      params[pair] = "";
    }
  }

  return params;
}

// CORS implementation
void HttpServer::enable_cors(const CorsConfig& config) {
  cors_enabled_ = true;
  cors_config_ = config;
}

void HttpServer::allow_origin(const std::string& origin) {
  cors_enabled_ = true;
  cors_config_.allowed_origins.insert(origin);
}

void HttpServer::allow_all_origins() {
  cors_enabled_ = true;
  cors_config_.allowed_origins.insert("*");
}

void HttpServer::handle_cors_preflight(struct mg_connection* c, const HttpRequest& request) {
  HttpResponse response;
  response.status_code = 204;  // No Content
  add_cors_headers(response, request);

  // Add preflight-specific headers
  if (!cors_config_.allowed_methods.empty()) {
    std::string methods;
    for (const auto& method : cors_config_.allowed_methods) {
      if (!methods.empty()) methods += ", ";
      methods += method;
    }
    response.headers["Access-Control-Allow-Methods"] = methods;
  }

  if (!cors_config_.allowed_headers.empty()) {
    std::string headers;
    for (const auto& header : cors_config_.allowed_headers) {
      if (!headers.empty()) headers += ", ";
      headers += header;
    }
    response.headers["Access-Control-Allow-Headers"] = headers;
  }

  response.headers["Access-Control-Max-Age"] = std::to_string(cors_config_.max_age);

  send_response(c, response);
}

void HttpServer::add_cors_headers(HttpResponse& response, const HttpRequest& request) {
  if (!cors_enabled_) return;

  // Access-Control-Allow-Origin
  if (cors_config_.allowed_origins.count("*")) {
    response.headers["Access-Control-Allow-Origin"] = "*";
  } else {
    auto origin_header = request.headers.find("Origin");
    if (origin_header != request.headers.end()) {
      const std::string& origin = origin_header->second;
      if (cors_config_.allowed_origins.count(origin)) {
        response.headers["Access-Control-Allow-Origin"] = origin;
      }
    }
  }

  // Access-Control-Allow-Credentials
  if (cors_config_.allow_credentials) {
    response.headers["Access-Control-Allow-Credentials"] = "true";
  }

  // Access-Control-Expose-Headers
  if (!cors_config_.exposed_headers.empty()) {
    std::string headers;
    for (const auto& header : cors_config_.exposed_headers) {
      if (!headers.empty()) headers += ", ";
      headers += header;
    }
    response.headers["Access-Control-Expose-Headers"] = headers;
  }
}

// WebSocket implementation
void HttpServer::websocket(const std::string& path, WebSocketHandler message_handler,
                           WebSocketConnectHandler connect_handler,
                           WebSocketCloseHandler close_handler) {
  websocket_routes_.push_back(
      {path, std::move(message_handler), std::move(connect_handler), std::move(close_handler)});
}

HttpServer::WebSocketRoute* HttpServer::find_websocket_route(const std::string& uri) {
  for (auto& route : websocket_routes_) {
    if (route.path == uri) {
      return &route;
    }
  }
  return nullptr;
}

void HttpServer::handle_websocket_handshake(struct mg_connection* c, struct mg_http_message* hm) {
  HttpRequest request = parse_request(hm);
  WebSocketRoute* route = find_websocket_route(request.uri);

  if (route) {
    // Check if connection should be accepted
    bool accept = true;
    if (route->connect_handler) {
      accept = route->connect_handler(c, request);
    }

    if (accept) {
      mg_ws_upgrade(c, hm, nullptr);
      websocket_connections_[c] = request.uri;
    } else {
      HttpResponse response = responses::bad_request("WebSocket connection rejected");
      send_response(c, response);
    }
  } else {
    HttpResponse response = responses::not_found("WebSocket endpoint not found");
    send_response(c, response);
  }
}

void HttpServer::handle_websocket_message(struct mg_connection* c, struct mg_ws_message* wm) {
  auto conn_it = websocket_connections_.find(c);
  if (conn_it != websocket_connections_.end()) {
    WebSocketRoute* route = find_websocket_route(conn_it->second);
    if (route && route->message_handler) {
      std::string message(reinterpret_cast<const char*>(wm->data.buf), wm->data.len);
      route->message_handler(c, message);
    }
  }
}

void HttpServer::handle_websocket_close(struct mg_connection* c) {
  auto conn_it = websocket_connections_.find(c);
  if (conn_it != websocket_connections_.end()) {
    WebSocketRoute* route = find_websocket_route(conn_it->second);
    if (route && route->close_handler) {
      route->close_handler(c);
    }
    websocket_connections_.erase(conn_it);
  }
}

namespace responses {
HttpResponse ok(const json& data) {
  HttpResponse response;
  response.status_code = 200;
  response.set_json(data);
  return response;
}

HttpResponse created(const json& data) {
  HttpResponse response;
  response.status_code = 201;
  response.set_json(data);
  return response;
}

HttpResponse bad_request(const std::string& message) {
  HttpResponse response;
  response.status_code = 400;
  response.set_json(json{{"error", message}});
  return response;
}

HttpResponse not_found(const std::string& message) {
  HttpResponse response;
  response.status_code = 404;
  response.set_json(json{{"error", message}});
  return response;
}

HttpResponse internal_error(const std::string& message) {
  HttpResponse response;
  response.status_code = 500;
  response.set_json(json{{"error", message}});
  return response;
}
}  // namespace responses

namespace middleware {
static long random_positive_long() {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<long> dis(1, LONG_MAX);

  return dis(gen);
}

MiddlewareHandler trace_id() {
  return [](const HttpRequest& req, HttpResponse& res) -> bool {
    res.headers.try_emplace(TRACE_ID_HEADER_NAME, std::to_string(random_positive_long()));
    return true;
  };
}

MiddlewareHandler request_logging() {
  return [](const HttpRequest& req, HttpResponse& res) -> bool {
    std::string trace_id = "unknown";
    if (res.headers.contains(TRACE_ID_HEADER_NAME)) {
      trace_id = res.headers[TRACE_ID_HEADER_NAME];
    }
    LOG(INFO) << "[" << req.method << " " << req.uri << "]: trace_id=" << trace_id
              << " status=" << res.status_code << " res.body.bytes=" << res.body.size();
    return true;
  };
}
}  // namespace middleware

// WebSocket utility functions
namespace websocket {
void send_text(struct mg_connection* c, const std::string& message) {
  mg_ws_send(c, message.c_str(), message.length(), WEBSOCKET_OP_TEXT);
}

void send_json(struct mg_connection* c, const json& data) {
  std::string message = data.dump();
  send_text(c, message);
}

void send_binary(struct mg_connection* c, const void* data, size_t length) {
  mg_ws_send(c, data, length, WEBSOCKET_OP_BINARY);
}

void close(struct mg_connection* c, int code, const std::string& reason) {
  mg_ws_send(c, reason.c_str(), reason.length(), WEBSOCKET_OP_CLOSE);
}
}  // namespace websocket

}  // namespace meerkat