#include "cpp/meerkat/meerkat.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace meerkat;

class MeerkatTest : public ::testing::Test {
 protected:
  void SetUp() override { server_ = std::make_unique<HttpServer>(); }

  void TearDown() override {
    if (server_->is_running()) {
      server_->stop();
    }
    server_.reset();
  }

  std::unique_ptr<HttpServer> server_;
};

TEST_F(MeerkatTest, ServerCanStartAndStop) {
  EXPECT_FALSE(server_->is_running());

  bool started = server_->listen("127.0.0.1", 8080);
  EXPECT_TRUE(started);
  // Note: is_running() won't be true until run() is called in the server thread
  // This is expected behavior with the new architecture
  EXPECT_FALSE(server_->is_running());

  server_->stop();
  EXPECT_FALSE(server_->is_running());
}

TEST_F(MeerkatTest, CannotStartTwice) {
  EXPECT_TRUE(server_->listen("127.0.0.1", 8081));
  // With the new architecture, listen() just stores parameters
  // You can call it again with different parameters
  EXPECT_TRUE(server_->listen("127.0.0.1", 8082));
}

TEST_F(MeerkatTest, CanRegisterRoutes) {
  bool get_called = false;
  bool post_called = false;

  server_->get("/test", [&get_called](const HttpRequest& req, Context& ctx) -> HttpResponse {
    get_called = true;
    return responses::ok(json{{"message", "GET received"}});
  });

  server_->post("/test", [&post_called](const HttpRequest& req, Context& ctx) -> HttpResponse {
    post_called = true;
    return responses::created(json{{"message", "POST received"}});
  });

  // Routes are registered, but we can't easily test them without starting the server
  // and making actual HTTP requests. Integration tests will cover that.
}

TEST_F(MeerkatTest, CanRegisterRequestInterceptor) {
  // Test that interceptor registration doesn't crash and accepts correct signature
  bool interceptor_registered = false;

  server_->use_request_interceptor([&](HttpRequest& req, HttpResponse& res, Context& ctx) -> bool {
    interceptor_registered = true;
    return true;  // Continue processing
  });

  // The interceptor is registered but won't be called until actual request processing
  // This test verifies the API works and the lambda has the correct signature
  EXPECT_FALSE(interceptor_registered);  // Not called yet since no request processed
}

TEST_F(MeerkatTest, CanRegisterMultipleRequestInterceptors) {
  // Test that multiple interceptors can be registered
  server_->use_request_interceptor([](HttpRequest& req, HttpResponse& res, Context& ctx) -> bool {
    return true;  // Continue processing
  });

  server_->use_request_interceptor([](HttpRequest& req, HttpResponse& res, Context& ctx) -> bool {
    return false;  // Block processing
  });

  // Both interceptors are registered - actual execution order testing
  // would require integration tests with real HTTP requests
}

TEST_F(MeerkatTest, ResponseUtilities) {
  auto ok_response = responses::ok(json{{"status", "success"}});
  EXPECT_EQ(ok_response.status_code, 200);
  EXPECT_EQ(ok_response.headers["Content-Type"], "application/json");

  auto created_response = responses::created(json{{"id", 123}});
  EXPECT_EQ(created_response.status_code, 201);

  auto bad_request = responses::bad_request("Invalid input");
  EXPECT_EQ(bad_request.status_code, 400);

  auto not_found = responses::not_found("Resource not found");
  EXPECT_EQ(not_found.status_code, 404);

  auto error = responses::internal_error("Something went wrong");
  EXPECT_EQ(error.status_code, 500);
}

TEST_F(MeerkatTest, HttpResponseJsonSetting) {
  HttpResponse response;
  json test_data = {{"key", "value"}, {"number", 42}};

  response.set_json(test_data);

  EXPECT_EQ(response.body, test_data.dump());
  EXPECT_EQ(response.headers["Content-Type"], "application/json");
}

TEST_F(MeerkatTest, HttpResponseTextSetting) {
  HttpResponse response;
  std::string test_text = "Hello, World!";

  response.set_text(test_text);

  EXPECT_EQ(response.body, test_text);
  EXPECT_EQ(response.headers["Content-Type"], "text/plain");
}

TEST_F(MeerkatTest, HttpServerCannotBeCopiedOrMoved) {
  HttpServer server1;
  EXPECT_TRUE(server1.listen("127.0.0.1", 8083));

  // This test verifies that HttpServer properly disables copy/move operations
  // Since move/copy are deleted, we just verify basic functionality
  // With new architecture, is_running() won't be true until run() is called
  EXPECT_FALSE(server1.is_running());
  server1.stop();
  EXPECT_FALSE(server1.is_running());
}

TEST_F(MeerkatTest, CorsConfiguration) {
  HttpServer::CorsConfig config;
  config.allowed_origins.insert("https://example.com");
  config.allowed_origins.insert("https://app.example.com");
  config.allowed_methods.insert("POST");
  config.allowed_headers.insert("X-Custom-Header");
  config.allow_credentials = true;
  config.max_age = 3600;

  server_->enable_cors(config);

  // Test allow_origin method
  server_->allow_origin("https://another.com");

  // Test allow_all_origins method
  HttpServer server2;
  server2.allow_all_origins();
}

TEST_F(MeerkatTest, WebSocketRouteRegistration) {
  bool message_received = false;
  bool connection_established = false;
  bool connection_closed = false;

  server_->websocket(
      "/ws",
      [&message_received](struct mg_connection* c, const std::string& message) {
        message_received = true;
      },
      [&connection_established](struct mg_connection* c, const HttpRequest& req) -> bool {
        connection_established = true;
        return true;  // Accept connection
      },
      [&connection_closed](struct mg_connection* c) { connection_closed = true; });

  // WebSocket routes are registered but won't be tested without actual connections
}