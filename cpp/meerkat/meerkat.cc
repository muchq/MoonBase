#include "cpp/meerkat/meerkat.h"

#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>
#include <string>

#include "absl/log/log.h"
#include "cpp/futility/rate_limiter/sliding_window_rate_limiter.h"

namespace meerkat {
// TODO: move this header name to some language neutral config format for cross-project sharing
const std::string TRACE_ID_HEADER_NAME{"x-trace-id"};
const std::string X_FORWARDED_FOR{"X-Forwarded-For"};

uint16_t read_port(const uint16_t default_port) {
  if (const char* env_p = std::getenv("PORT")) {
    return static_cast<uint16_t>(std::atoi(env_p));
  }
  return default_port;
}

HttpServer::HttpServer()
    : listener_(nullptr),
      running_(false),
      mgr_initialized_(false),
      listen_port_(0),
      should_listen_(false) {
  // Disable mongoose verbose logging (default is MG_LL_INFO)
  mg_log_set(MG_LL_ERROR);
  mg_mgr_init(&mgr_);
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

void HttpServer::use_request_interceptor(RequestInterceptor request_interceptor) {
  request_interceptors_.push_back(std::move(request_interceptor));
}

void HttpServer::use_response_interceptor(ResponseInterceptor response_interceptor) {
  response_interceptors_.push_back(std::move(response_interceptor));
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

int HttpServer::get_port() const {
  if (listener_ && listener_->is_listening) {
    // Parse the actual port from the listener's local address
    // Mongoose stores the address in the format "ip:port"
    char addr[100];
    mg_snprintf(addr, sizeof(addr), "%M", mg_print_ip_port, &listener_->loc);

    // Extract port from "ip:port" format
    const char* port_str = strrchr(addr, ':');
    if (port_str) {
      return atoi(port_str + 1);
    }
  }

  // If not listening yet, return the configured port
  return listen_port_;
}

void HttpServer::poll(int timeout_ms) {
  if (running_ && mgr_initialized_ && listener_) {
    mg_mgr_poll(&mgr_, timeout_ms);
  }
}

void HttpServer::run() {
  // Initialize mongoose in the polling thread to avoid thread safety issues
  if (should_listen_ && !mgr_initialized_) {
    mg_log_set(MG_LL_ERROR);
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

void HttpServer::enable_health_checks() {
  get("/health", [](const HttpRequest& req, Context& ctx) -> HttpResponse {
    return responses::ok(json{{"status", "healthy"}, {"timestamp", std::time(nullptr)}});
  });
}

void HttpServer::enable_tracing() {
  use_request_interceptor(interceptors::request::trace_id());
  use_response_interceptor(interceptors::response::trace_id_header());
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
  }
}

void HttpServer::handle_request(struct mg_connection* c, struct mg_http_message* hm) {
  HttpRequest request = parse_request(hm);
  Context context = {};
  context.start_time = std::chrono::steady_clock::now();
  HttpResponse response;

  bool process_request = true;

  // Apply middleware
  for (const auto& request_interceptor : request_interceptors_) {
    if (!request_interceptor(request, response, context)) {
      // Request interceptor blocked the request
      process_request = false;
      break;
    }
  }

  // TODO: validate mime-type
  RouteHandler* handler = find_handler(request.method, request.uri);

  if (process_request && handler) {
    try {
      response = (*handler)(request, context);
    } catch (const std::exception& e) {
      response = responses::internal_error(e.what());
    } catch (...) {
      response = responses::internal_error("Unknown error occurred");
    }
  } else if (process_request) {
    response = responses::not_found();
  }

  try {
    for (const auto& response_interceptor : response_interceptors_) {
      response_interceptor(request, response, context);
    }
  } catch (...) {
    response = responses::internal_error();
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

namespace responses {
HttpResponse wrap(const absl::StatusOr<json> status_or_data) {
  if (!status_or_data.ok()) {
    switch (status_or_data.status().code()) {
      case absl::StatusCode::kInvalidArgument: {
        return bad_request(status_or_data.status().message().data());
      }
      case absl::StatusCode::kNotFound: {
        return not_found(status_or_data.status().message().data());
      }
      default: {
        return internal_error();
      }
    }
  }
  return ok(status_or_data.value());
}

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

HttpResponse too_many_requests(const std::string& message) {
  HttpResponse response;
  response.status_code = 429;
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

namespace interceptors {
static long random_positive_long() {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<long> dis(1, LONG_MAX);

  return dis(gen);
}

namespace request {
RequestInterceptor trace_id() {
  return [](HttpRequest& req, HttpResponse& res, Context& ctx) -> bool {
    if (req.headers.contains(TRACE_ID_HEADER_NAME)) {
      ctx.trace_id = req.headers[TRACE_ID_HEADER_NAME];
    } else {
      ctx.trace_id = std::to_string(random_positive_long());
    }
    return true;
  };
}

using futility::rate_limiter::SlidingWindowRateLimiter;

RequestInterceptor rate_limiter(
    std::shared_ptr<SlidingWindowRateLimiter<std::string>> ip_rate_limiter) {
  return [ip_rate_limiter](HttpRequest& req, HttpResponse& res, Context& ctx) -> bool {
    auto ip_address = req.headers[X_FORWARDED_FOR];
    if (!ip_rate_limiter->allow(ip_address, 1)) {
      res.status_code = 429;
      res.set_json(json{{"error", "Too many requests"}});
      return false;
    }
    return true;
  };
}
}  // namespace request

namespace response {
ResponseInterceptor trace_id_header() {
  return [](const HttpRequest& req, HttpResponse& res, Context& ctx) -> void {
    if (!ctx.trace_id.empty()) {
      res.headers.try_emplace(TRACE_ID_HEADER_NAME, ctx.trace_id);
    }
  };
}
ResponseInterceptor logging() {
  return [](const HttpRequest& req, HttpResponse& res, Context& ctx) -> void {
    auto end_time = std::chrono::steady_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - ctx.start_time);

    std::string ip_address;
    if (req.headers.contains(X_FORWARDED_FOR)) {
      ip_address = req.headers.at(X_FORWARDED_FOR);
    }

    LOG(INFO) << "[" << req.method << " " << req.uri << "]: X-Forwarded-For=" << ip_address
              << " trace_id=" << ctx.trace_id << " status=" << res.status_code
              << " res.body.bytes=" << res.body.size() << " duration_ms=" << duration.count();
  };
}
}  // namespace response
}  // namespace interceptors
}  // namespace meerkat