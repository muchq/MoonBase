#include "cpp/meerkat/http_client.h"

#include <gtest/gtest.h>

#include <future>
#include <thread>

#include "cpp/meerkat/meerkat.h"

using namespace meerkat;

class HttpClientTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use a unique port for each test - start from higher range to avoid conflicts
    static int port_counter = 9000;
    port_ = port_counter++;
    
    client_ = std::make_unique<HttpClient>();
    server_ = std::make_unique<HttpServer>();

    SetupTestServer();
  }

  void TearDown() override {
    if (server_->is_running()) {
      server_->stop();
    }
    // Wait for the server thread to finish before destroying the server object
    if (server_thread_.valid()) {
      server_thread_.wait();
    }
    
    // Reset server and client before next test
    server_.reset();
    client_.reset();
    
    // Small delay to ensure cleanup is complete
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  void SetupTestServer() {
    // Basic echo endpoint
    server_->get("/echo", [](const HttpRequest& req) -> HttpResponse {
      return responses::ok(json{{"echoed", "GET /echo"}});
    });

    // JSON POST endpoint
    server_->post("/json", [](const HttpRequest& req) -> HttpResponse {
      try {
        json request_data = json::parse(req.body);
        json response_data = {{"received", request_data}, {"method", "POST"}};
        return responses::created(response_data);
      } catch (const json::exception& e) {
        return responses::bad_request("Invalid JSON");
      }
    });

    // Headers test endpoint
    server_->get("/headers", [](const HttpRequest& req) -> HttpResponse {
      json headers_json;
      for (const auto& [key, value] : req.headers) {
        headers_json[key] = value;
      }
      return responses::ok(headers_json);
    });

    // Error endpoint
    server_->get("/error", [](const HttpRequest& req) -> HttpResponse {
      return responses::internal_error("Intentional server error");
    });
  }

  void StartServerAsync() {
    server_thread_ = std::async(std::launch::async, [this]() { server_->run(); });

    // Wait for server to actually start listening
    for (int i = 0; i < 50; i++) {
      if (server_->is_listening()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  std::string GetBaseUrl() { return "http://127.0.0.1:" + std::to_string(port_); }

  std::unique_ptr<HttpClient> client_;
  std::unique_ptr<HttpServer> server_;
  std::future<void> server_thread_;
  int port_;
};

TEST_F(HttpClientTest, BasicGetRequest) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/echo");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);
  EXPECT_FALSE(response.body.empty());

  // Verify JSON response
  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["echoed"], "GET /echo");
}

TEST_F(HttpClientTest, JsonPostRequest) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  json request_data = {{"name", "test user"}, {"email", "test@example.com"}};

  auto response = client_->post_json(GetBaseUrl() + "/json", request_data);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 201);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["method"], "POST");
  EXPECT_EQ(response_json["received"]["name"], "test user");
  EXPECT_EQ(response_json["received"]["email"], "test@example.com");
}

TEST_F(HttpClientTest, CustomHeaders) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  std::unordered_map<std::string, std::string> headers;
  headers["X-Custom-Header"] = "test-value";
  headers["Authorization"] = "Bearer test-token";

  auto response = client_->get(GetBaseUrl() + "/headers", headers);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["X-Custom-Header"], "test-value");
  EXPECT_EQ(response_json["Authorization"], "Bearer test-token");
}

TEST_F(HttpClientTest, ErrorResponse) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/error");

  EXPECT_TRUE(response.success);         // Connection succeeded
  EXPECT_EQ(response.status_code, 500);  // But server returned error
  EXPECT_FALSE(response.body.empty());
}

TEST_F(HttpClientTest, ConnectionTimeout) {
  // Don't start server - connection should fail quickly
  // The server created in SetUp won't be used, but we keep it alive
  // to avoid any cleanup issues
  
  auto response = client_->get("http://127.0.0.1:99999/nonexistent", 1000);

  EXPECT_FALSE(response.success);
  EXPECT_FALSE(response.error_message.empty());
}

TEST_F(HttpClientTest, NotFoundEndpoint) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/nonexistent");

  EXPECT_TRUE(response.success);         // Connection succeeded
  EXPECT_EQ(response.status_code, 404);  // But endpoint not found
}

TEST_F(HttpClientTest, ResponseHeaders) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/echo");

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);

  // Check that we received some response headers
  EXPECT_FALSE(response.headers.empty());
  EXPECT_EQ(response.headers["Content-Type"], "application/json");
}

TEST_F(HttpClientTest, MultipleSequentialRequestsWithHeaders) {
  // This test reproduces the exact scenario from the integration test that causes segfault:
  // 1. Simple GET request
  // 2. GET request with custom headers
  // This pattern triggers the segfault in MiddlewareAuthentication test

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // First request - simple GET (like the unauthenticated request)
  std::cout << "Making first request..." << std::endl;
  auto response1 = client_->get(GetBaseUrl() + "/echo", 10000);
  std::cout << "First request completed: " << response1.success << ", " << response1.status_code
            << std::endl;

  EXPECT_TRUE(response1.success);
  EXPECT_EQ(response1.status_code, 200);

  // Second request - with custom headers (like the authenticated request)
  std::cout << "Making second request with headers..." << std::endl;
  std::unordered_map<std::string, std::string> headers;
  headers["Authorization"] = "Bearer valid-token";
  headers["X-Custom"] = "test-value";

  auto response2 = client_->get(GetBaseUrl() + "/headers", headers, 10000);
  std::cout << "Second request completed: " << response2.success << ", " << response2.status_code
            << std::endl;
  std::cout << "Response body: '" << response2.body << "'" << std::endl;

  EXPECT_TRUE(response2.success);
  EXPECT_EQ(response2.status_code, 200);
  EXPECT_FALSE(response2.body.empty());

  std::cout << "About to parse JSON..." << std::endl;
  json response_json = json::parse(response2.body);
  std::cout << "JSON parsed successfully" << std::endl;

  EXPECT_EQ(response_json["Authorization"], "Bearer valid-token");
  EXPECT_EQ(response_json["X-Custom"], "test-value");

  std::cout << "Test completed successfully!" << std::endl;
}