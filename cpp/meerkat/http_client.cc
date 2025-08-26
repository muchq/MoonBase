#include "cpp/meerkat/http_client.h"

#include <iostream>
#include <sstream>
#include <thread>

namespace meerkat {

HttpClient::HttpClient() { mg_mgr_init(&mgr_); }

HttpClient::~HttpClient() { mg_mgr_free(&mgr_); }

HttpClientResponse HttpClient::get(const std::string& url, int timeout_ms) {
  return make_request("GET", url, "", {}, timeout_ms);
}

HttpClientResponse HttpClient::get(const std::string& url,
                                   const std::unordered_map<std::string, std::string>& headers,
                                   int timeout_ms) {
  return make_request("GET", url, "", headers, timeout_ms);
}

HttpClientResponse HttpClient::post(const std::string& url, const std::string& body,
                                    const std::unordered_map<std::string, std::string>& headers,
                                    int timeout_ms) {
  return make_request("POST", url, body, headers, timeout_ms);
}

HttpClientResponse HttpClient::put(const std::string& url, const std::string& body,
                                   const std::unordered_map<std::string, std::string>& headers,
                                   int timeout_ms) {
  return make_request("PUT", url, body, headers, timeout_ms);
}

HttpClientResponse HttpClient::del(const std::string& url, int timeout_ms) {
  return make_request("DELETE", url, "", {}, timeout_ms);
}

HttpClientResponse HttpClient::post_json(const std::string& url, const json& data, int timeout_ms) {
  std::unordered_map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  return post(url, data.dump(), headers, timeout_ms);
}

HttpClientResponse HttpClient::put_json(const std::string& url, const json& data, int timeout_ms) {
  std::unordered_map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  return put(url, data.dump(), headers, timeout_ms);
}

HttpClientResponse HttpClient::make_request(
    const std::string& method, const std::string& url, const std::string& body,
    const std::unordered_map<std::string, std::string>& headers, int timeout_ms) {
  // Use heap-allocated context to avoid use-after-free when Mongoose callbacks
  // access the context from a different thread or after this function returns
  std::unique_ptr<RequestContext> ctx = std::make_unique<RequestContext>();
  ctx->method = method;
  ctx->url = url;
  ctx->request_body = body;
  ctx->request_headers = headers;

  // Create connection - pass raw pointer that will be managed by Mongoose
  struct mg_connection* conn = mg_http_connect(&mgr_, url.c_str(), event_handler, ctx.get());
  if (!conn) {
    HttpClientResponse response;
    response.error_message = "Failed to create connection";
    return response;
  }

  // Wait for completion with timeout
  auto start_time = std::chrono::steady_clock::now();
  auto timeout_duration = std::chrono::milliseconds(timeout_ms);

  while (!ctx->done) {
    auto current_time = std::chrono::steady_clock::now();
    if (current_time - start_time > timeout_duration) {
      ctx->response.error_message = "Request timed out";
      ctx->done = true;
      break;
    }

    mg_mgr_poll(&mgr_, 100);  // Poll every 100ms
  }

  // Copy response before context is destroyed
  HttpClientResponse response = ctx->response;

  // Transfer ownership to event handler for cleanup on connection close
  ctx.release();

  return response;
}

void HttpClient::event_handler(struct mg_connection* c, int ev, void* ev_data) {
  RequestContext* ctx = static_cast<RequestContext*>(c->fn_data);
  if (!ctx) return;

  // Clean up context on close to prevent memory leak
  if (ev == MG_EV_CLOSE) {
    c->fn_data = nullptr;
    delete ctx;  // Safe to delete now that connection is closed
    return;
  }

  if (ev == MG_EV_CONNECT) {
    // Connection established, send request
    std::ostringstream request_stream;

    // Parse URL manually
    std::string url = ctx->url;
    std::string host, uri = "/";

    // Extract host and URI from URL
    size_t protocol_end = url.find("://");
    if (protocol_end != std::string::npos) {
      std::string remainder = url.substr(protocol_end + 3);
      size_t path_start = remainder.find('/');
      if (path_start != std::string::npos) {
        host = remainder.substr(0, path_start);
        uri = remainder.substr(path_start);
      } else {
        host = remainder;
        uri = "/";
      }
    } else {
      // Assume it's just host:port
      size_t path_start = url.find('/');
      if (path_start != std::string::npos) {
        host = url.substr(0, path_start);
        uri = url.substr(path_start);
      } else {
        host = url;
      }
    }

    request_stream << ctx->method << " " << uri << " HTTP/1.1\r\n";
    request_stream << "Host: " << host << "\r\n";

    // Add custom headers
    for (const auto& [key, value] : ctx->request_headers) {
      request_stream << key << ": " << value << "\r\n";
    }

    // Always add content length (even for empty body)
    request_stream << "Content-Length: " << ctx->request_body.length() << "\r\n";

    request_stream << "Connection: close\r\n";  // Ensure connection closes after response
    request_stream << "\r\n";

    // Add body if present
    if (!ctx->request_body.empty()) {
      request_stream << ctx->request_body;
    }

    std::string request_str = request_stream.str();
    mg_send(c, request_str.c_str(), request_str.length());

  } else if (ev == MG_EV_HTTP_MSG) {
    // Response received
    struct mg_http_message* hm = static_cast<struct mg_http_message*>(ev_data);

    // Parse status code
    ctx->response.status_code = mg_http_status(hm);

    // Parse body
    ctx->response.body = std::string(hm->body.buf, hm->body.len);

    // Parse headers
    for (int i = 0; i < MG_MAX_HTTP_HEADERS && hm->headers[i].name.buf; i++) {
      std::string name(hm->headers[i].name.buf, hm->headers[i].name.len);
      std::string value(hm->headers[i].value.buf, hm->headers[i].value.len);
      ctx->response.headers[name] = value;
    }

    ctx->response.success = true;
    ctx->done = true;

  } else if (ev == MG_EV_ERROR) {
    // Connection error
    char* error_msg = static_cast<char*>(ev_data);
    ctx->response.error_message = error_msg ? std::string(error_msg) : "Unknown connection error";
    ctx->done = true;

  } else if (ev == MG_EV_CLOSE) {
    // Connection closed
    if (!ctx->done && ctx->response.error_message.empty()) {
      ctx->response.error_message = "Connection closed unexpectedly";
    }
    ctx->done = true;
  }
}

void HttpClient::handle_connect(struct mg_connection* c, RequestContext* ctx) {
  // This method is not used in the current implementation
  // Logic is handled directly in event_handler
}

void HttpClient::handle_response(struct mg_connection* c, struct mg_http_message* hm,
                                 RequestContext* ctx) {
  // This method is not used in the current implementation
  // Logic is handled directly in event_handler
}

void HttpClient::handle_error(struct mg_connection* c, RequestContext* ctx,
                              const std::string& error) {
  // This method is not used in the current implementation
  // Logic is handled directly in event_handler
}

std::unordered_map<std::string, std::string> HttpClient::parse_response_headers(
    struct mg_http_message* hm) {
  // This method is not used in the current implementation
  // Logic is handled directly in event_handler
  return {};
}

std::string HttpClient::build_request_headers(
    const std::unordered_map<std::string, std::string>& headers) {
  // This method is not used in the current implementation
  // Logic is handled directly in event_handler
  return "";
}

}  // namespace meerkat