#include "cpp/meerkat/http_client.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

#include "cpp/meerkat/meerkat.h"

using namespace meerkat;

class HttpClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    client_ = std::make_unique<HttpClient>();
    server_ = std::make_unique<HttpServer>();

    SetupTestServer();
  }

  void TearDown() override {
    // Stop server if running
    if (server_ && server_->is_running()) {
      server_->stop();
    }

    // Wait for server thread to complete
    if (server_thread_.valid()) {
      server_thread_.wait();
    }

    // Clean shutdown
    server_.reset();
    client_.reset();
  }

  void SetupTestServer() {
    // Basic echo endpoint
    server_->get("/echo", [](const HttpRequest& req, Context& ctx) -> HttpResponse {
      return responses::ok(json{{"echoed", "GET /echo"}});
    });

    // JSON POST endpoint
    server_->post("/json", [](const HttpRequest& req, Context& ctx) -> HttpResponse {
      try {
        json request_data = json::parse(req.body);
        json response_data = {{"received", request_data}, {"method", "POST"}};
        return responses::created(response_data);
      } catch (const json::exception& e) {
        return responses::bad_request("Invalid JSON");
      }
    });

    // Headers test endpoint
    server_->get("/headers", [](const HttpRequest& req, Context& ctx) -> HttpResponse {
      json headers_json;
      for (const auto& [key, value] : req.headers) {
        headers_json[key] = value;
      }
      return responses::ok(headers_json);
    });

    // Error endpoint
    server_->get("/error", [](const HttpRequest& req, Context& ctx) -> HttpResponse {
      return responses::internal_error("Intentional server error");
    });
  }

  bool StartServer() {
    // Use port 0 to let OS assign a port
    if (!server_->listen("127.0.0.1", 0)) {
      return false;
    }

    // Start server in background thread
    server_thread_ = std::async(std::launch::async, [this]() { server_->run(); });

    // Wait for server to actually start listening
    // The server needs to call mg_http_listen before we can get the port
    for (int i = 0; i < 100; ++i) {
      if (server_->is_listening()) {
        // Now we can get the actual port
        port_ = server_->get_port();
        if (port_ > 0) {
          // Verify server is responsive
          auto temp_client = std::make_unique<HttpClient>();
          auto response = temp_client->get(GetBaseUrl() + "/echo", 5000);
          if (response.success) {
            return true;
          }
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return false;
  }

  std::string GetBaseUrl() { return "http://127.0.0.1:" + std::to_string(port_); }

  std::unique_ptr<HttpClient> client_;
  std::unique_ptr<HttpServer> server_;
  std::future<void> server_thread_;
  int port_ = 0;
};

TEST_F(HttpClientTest, BasicGetRequest) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  auto response = client_->get(GetBaseUrl() + "/echo");

  ASSERT_TRUE(response.success) << "Request failed: " << response.error_message;
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.body.empty());

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["echoed"], "GET /echo");
}

TEST_F(HttpClientTest, JsonPostRequest) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  json request_data = {{"name", "test user"}, {"email", "test@example.com"}};

  auto response = client_->post_json(GetBaseUrl() + "/json", request_data);

  ASSERT_TRUE(response.success) << "Request failed: " << response.error_message;
  EXPECT_EQ(response.status_code, 201);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["method"], "POST");
  EXPECT_EQ(response_json["received"]["name"], "test user");
  EXPECT_EQ(response_json["received"]["email"], "test@example.com");
}

TEST_F(HttpClientTest, CustomHeaders) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  std::unordered_map<std::string, std::string> headers;
  headers["X-Custom-Header"] = "test-value";
  headers["Authorization"] = "Bearer test-token";

  auto response = client_->get(GetBaseUrl() + "/headers", headers);

  ASSERT_TRUE(response.success) << "Request failed: " << response.error_message;
  EXPECT_EQ(response.status_code, 200);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["X-Custom-Header"], "test-value");
  EXPECT_EQ(response_json["Authorization"], "Bearer test-token");
}

TEST_F(HttpClientTest, ErrorResponse) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  auto response = client_->get(GetBaseUrl() + "/error");

  ASSERT_TRUE(response.success);  // Connection should succeed
  EXPECT_EQ(response.status_code, 500);
  EXPECT_FALSE(response.body.empty());
}

TEST_F(HttpClientTest, ConnectionRefused) {
  // Don't start server - connection should be refused
  // Use a specific port that we know is not listening
  auto response = client_->get("http://127.0.0.1:65432");

  EXPECT_FALSE(response.success);
  EXPECT_FALSE(response.error_message.empty());
}

TEST_F(HttpClientTest, NotFoundEndpoint) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  auto response = client_->get(GetBaseUrl() + "/nonexistent");

  ASSERT_TRUE(response.success);  // Connection should succeed
  EXPECT_EQ(response.status_code, 404);
}

TEST_F(HttpClientTest, ResponseHeaders) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  auto response = client_->get(GetBaseUrl() + "/echo");

  ASSERT_TRUE(response.success) << "Request failed: " << response.error_message;
  EXPECT_EQ(response.status_code, 200);

  // Check that we received response headers
  EXPECT_FALSE(response.headers.empty());
  EXPECT_EQ(response.headers["Content-Type"], "application/json");
}

TEST_F(HttpClientTest, MultipleSequentialRequests) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  // First request - simple GET
  auto response1 = client_->get(GetBaseUrl() + "/echo");
  ASSERT_TRUE(response1.success) << "First request failed: " << response1.error_message;
  EXPECT_EQ(response1.status_code, 200);

  // Second request - with custom headers
  std::unordered_map<std::string, std::string> headers;
  headers["Authorization"] = "Bearer valid-token";
  headers["X-Custom"] = "test-value";

  auto response2 = client_->get(GetBaseUrl() + "/headers", headers);
  ASSERT_TRUE(response2.success) << "Second request failed: " << response2.error_message;
  EXPECT_EQ(response2.status_code, 200);

  json response_json = json::parse(response2.body);
  EXPECT_EQ(response_json["Authorization"], "Bearer valid-token");
  EXPECT_EQ(response_json["X-Custom"], "test-value");

  // Third request - POST with JSON
  json post_data = {{"test", "data"}};
  auto response3 = client_->post_json(GetBaseUrl() + "/json", post_data);
  ASSERT_TRUE(response3.success) << "Third request failed: " << response3.error_message;
  EXPECT_EQ(response3.status_code, 201);
}

TEST_F(HttpClientTest, LargePayload) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  // Create a large JSON payload
  json large_data;
  for (int i = 0; i < 1000; ++i) {
    large_data["field_" + std::to_string(i)] = "value_" + std::to_string(i);
  }

  auto response = client_->post_json(GetBaseUrl() + "/json", large_data);

  ASSERT_TRUE(response.success) << "Request failed: " << response.error_message;
  EXPECT_EQ(response.status_code, 201);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["received"]["field_0"], "value_0");
  EXPECT_EQ(response_json["received"]["field_999"], "value_999");
}

TEST_F(HttpClientTest, EmptyBody) {
  ASSERT_TRUE(StartServer()) << "Failed to start server";

  auto response = client_->post(GetBaseUrl() + "/json", "", {{"Content-Type", "application/json"}});

  // Should get bad request since empty string is not valid JSON
  ASSERT_TRUE(response.success);  // Connection should succeed
  EXPECT_EQ(response.status_code, 400);
}