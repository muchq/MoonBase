#ifndef DOMAINS_API_PLATFORM_LIBS_MEERKAT_HTTP_CLIENT_H
#define DOMAINS_API_PLATFORM_LIBS_MEERKAT_HTTP_CLIENT_H

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "mongoose.h"
#include "nlohmann/json.hpp"

namespace meerkat {

using json = nlohmann::json;

struct HttpClientResponse {
  int status_code = 0;
  std::string body;
  std::unordered_map<std::string, std::string> headers;
  bool success = false;
  std::string error_message;
};

class HttpClient {
 public:
  HttpClient();
  ~HttpClient();

  // Non-copyable, non-moveable
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;
  HttpClient(HttpClient&&) = delete;
  HttpClient& operator=(HttpClient&&) = delete;

  // HTTP methods with timeout support
  HttpClientResponse get(const std::string& url, int timeout_ms = 5000);
  HttpClientResponse get(const std::string& url,
                         const std::unordered_map<std::string, std::string>& headers,
                         int timeout_ms = 5000);
  HttpClientResponse post(const std::string& url, const std::string& body,
                          const std::unordered_map<std::string, std::string>& headers = {},
                          int timeout_ms = 5000);
  HttpClientResponse put(const std::string& url, const std::string& body,
                         const std::unordered_map<std::string, std::string>& headers = {},
                         int timeout_ms = 5000);
  HttpClientResponse del(const std::string& url, int timeout_ms = 5000);

  // JSON convenience methods
  HttpClientResponse post_json(const std::string& url, const json& data, int timeout_ms = 5000);
  HttpClientResponse put_json(const std::string& url, const json& data, int timeout_ms = 5000);

 private:
  struct mg_mgr mgr_;

  struct RequestContext {
    HttpClientResponse response;
    bool done = false;
    std::string method;
    std::string url;
    std::string request_body;
    std::unordered_map<std::string, std::string> request_headers;
    std::mutex mutex;  // Protects access to this context
  };

  HttpClientResponse make_request(const std::string& method, const std::string& url,
                                  const std::string& body = "",
                                  const std::unordered_map<std::string, std::string>& headers = {},
                                  int timeout_ms = 5000);

  static void event_handler(struct mg_connection* c, int ev, void* ev_data);
  void handle_connect(struct mg_connection* c, RequestContext* ctx);
  void handle_response(struct mg_connection* c, struct mg_http_message* hm, RequestContext* ctx);
  void handle_error(struct mg_connection* c, RequestContext* ctx, const std::string& error);

  std::unordered_map<std::string, std::string> parse_response_headers(struct mg_http_message* hm);
  std::string build_request_headers(const std::unordered_map<std::string, std::string>& headers);
};

}  // namespace meerkat

#endif  // DOMAINS_API_PLATFORM_LIBS_MEERKAT_HTTP_CLIENT_H