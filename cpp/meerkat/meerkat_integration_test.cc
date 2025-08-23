#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <thread>

#include "cpp/meerkat/http_client.h"
#include "cpp/meerkat/meerkat.h"

using namespace meerkat;

class MeerkatIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    server_ = std::make_unique<HttpServer>();
    client_ = std::make_unique<HttpClient>();
    static int port_counter = 9100;  // Use different range than http_client_test
    port_ = port_counter++;  // Use different ports for each test
  }

  void TearDown() override {
    if (server_->is_running()) {
      server_->stop();
      // Wait for server thread to finish
      if (server_thread_.valid()) {
        server_thread_.wait();
      }
    }
    client_.reset();
    server_.reset();
  }

  void StartServerAsync() {
    server_thread_ = std::async(std::launch::async, [this]() { server_->run(); });

    // Give the server a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::string GetBaseUrl() { return "http://127.0.0.1:" + std::to_string(port_); }

  std::unique_ptr<HttpServer> server_;
  std::unique_ptr<HttpClient> client_;
  std::future<void> server_thread_;
  int port_;
};

TEST_F(MeerkatIntegrationTest, BasicGetRequest) {
  server_->get("/hello", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "Hello, World!"}});
  });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Make actual HTTP request
  auto response = client_->get(GetBaseUrl() + "/hello", 10000);  // 10s timeout

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);
  EXPECT_EQ(response.headers["Content-Type"], "application/json");

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["message"], "Hello, World!");
}

TEST_F(MeerkatIntegrationTest, JsonPostRequest) {
  server_->post("/api/users", [](const HttpRequest& req) -> HttpResponse {
    try {
      json request_data = json::parse(req.body);

      if (!request_data.contains("name")) {
        return responses::bad_request("Missing 'name' field");
      }

      json response_data = {{"id", 123}, {"name", request_data["name"]}, {"created", true}};

      return responses::created(response_data);
    } catch (const json::exception& e) {
      return responses::bad_request("Invalid JSON: " + std::string(e.what()));
    }
  });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Make actual JSON POST request
  json request_data = {{"name", "Integration Test User"}};
  auto response =
      client_->post_json(GetBaseUrl() + "/api/users", request_data, 10000);  // 10s timeout

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 201);
  EXPECT_FALSE(response.body.empty());

  json response_json = json::parse(response.body);

  EXPECT_EQ(response_json["id"], 123);
  EXPECT_EQ(response_json["name"], "Integration Test User");
  EXPECT_EQ(response_json["created"], true);
}

TEST_F(MeerkatIntegrationTest, MiddlewareAuthentication) {
  std::cout << "Starting MiddlewareAuthentication test..." << std::endl;

  // Add simple middleware that doesn't capture anything
  server_->use_middleware([](const HttpRequest& req, HttpResponse& res) -> bool {
    std::cout << "Middleware executed" << std::endl;

    auto auth_header = req.headers.find("Authorization");
    if (auth_header == req.headers.end() || auth_header->second != "Bearer valid-token") {
      res.status_code = 401;
      res.set_json(json{{"error", "Unauthorized"}});
      return false;  // Block the request
    }

    return true;  // Continue processing
  });

  std::cout << "Added middleware..." << std::endl;

  server_->get("/protected", [](const HttpRequest& req) -> HttpResponse {
    std::cout << "Handler executed" << std::endl;
    return responses::ok(json{{"message", "Access granted"}});
  });

  std::cout << "Added route..." << std::endl;

  std::cout << "Attempting to listen on port " << port_ << std::endl;
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  std::cout << "Server listening, starting async..." << std::endl;
  StartServerAsync();
  std::cout << "Server started, making first request..." << std::endl;

  // Test without authentication - should be rejected
  auto response_no_auth = client_->get(GetBaseUrl() + "/protected", 10000);
  std::cout << "First request completed: " << response_no_auth.success << ", "
            << response_no_auth.status_code << std::endl;
  EXPECT_TRUE(response_no_auth.success);         // Connection successful
  EXPECT_EQ(response_no_auth.status_code, 401);  // But unauthorized

  std::cout << "Making second request with auth..." << std::endl;

  // Test with valid authentication
  std::unordered_map<std::string, std::string> auth_headers;
  auth_headers["Authorization"] = "Bearer valid-token";
  auto response_with_auth = client_->get(GetBaseUrl() + "/protected", auth_headers, 10000);

  std::cout << "Second request completed: " << response_with_auth.success << ", "
            << response_with_auth.status_code << std::endl;
  std::cout << "Response body: '" << response_with_auth.body << "'" << std::endl;
  std::cout << "Body length: " << response_with_auth.body.size() << std::endl;

  EXPECT_TRUE(response_with_auth.success);
  EXPECT_EQ(response_with_auth.status_code, 200);

  std::cout << "About to parse JSON..." << std::endl;
  json response_json = json::parse(response_with_auth.body);
  std::cout << "JSON parsed successfully" << std::endl;

  EXPECT_EQ(response_json["message"], "Access granted");

  std::cout << "Test completed successfully!" << std::endl;
}

TEST_F(MeerkatIntegrationTest, ErrorHandling) {
  server_->get("/error", [](const HttpRequest& req) -> HttpResponse {
    throw std::runtime_error("Intentional test error");
    return responses::ok();  // This won't be reached
  });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Test that server handles exceptions properly
  auto response = client_->get(GetBaseUrl() + "/error");

  EXPECT_TRUE(response.success);         // Connection successful
  EXPECT_EQ(response.status_code, 500);  // Internal server error
  EXPECT_FALSE(response.body.empty());   // Should have error message
}

TEST_F(MeerkatIntegrationTest, QueryParameters) {
  server_->get("/search", [](const HttpRequest& req) -> HttpResponse {
    std::cout << "Server received request:" << std::endl;
    std::cout << "  URI: '" << req.uri << "'" << std::endl;
    std::cout << "  Query params count: " << req.query_params.size() << std::endl;

    json result = json::object();

    for (const auto& [key, value] : req.query_params) {
      std::cout << "  Param: '" << key << "' = '" << value << "'" << std::endl;
      result[key] = value;
    }

    return responses::ok(json{{"params", result}});
  });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Test query parameters
  std::string url = GetBaseUrl() + "/search?q=test&category=books&limit=10";
  std::cout << "Making request to: " << url << std::endl;
  auto response = client_->get(url, 10000);

  std::cout << "Response success: " << response.success << ", status: " << response.status_code
            << std::endl;
  std::cout << "Response body: " << response.body << std::endl;

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);

  json response_json = json::parse(response.body);
  std::cout << "Parsed JSON: " << response_json.dump(2) << std::endl;

  EXPECT_EQ(response_json["params"]["q"], "test");
  EXPECT_EQ(response_json["params"]["category"], "books");
  EXPECT_EQ(response_json["params"]["limit"], "10");
}

TEST_F(MeerkatIntegrationTest, CorsPreflightRequest) {
  // Enable CORS with specific configuration
  HttpServer::CorsConfig config;
  config.allowed_origins.insert("https://example.com");
  config.allowed_methods.insert("POST");
  config.allowed_methods.insert("PUT");
  config.allowed_headers.insert("Content-Type");
  config.allowed_headers.insert("Authorization");
  config.allow_credentials = true;
  config.max_age = 3600;

  server_->enable_cors(config);

  // Add a regular route
  server_->post("/api/data", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "POST received"}});
  });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, WebSocketConnection) {
  std::vector<std::string> received_messages;
  bool connection_accepted = false;
  bool connection_closed = false;

  server_->websocket(
      "/ws/test",
      // Message handler
      [&received_messages](struct mg_connection* c, const std::string& message) {
        received_messages.push_back(message);

        // Echo the message back
        websocket::send_text(c, "Echo: " + message);

        // Send a JSON response
        json response = {
            {"type", "echo"}, {"original", message}, {"timestamp", std::time(nullptr)}};
        websocket::send_json(c, response);
      },
      // Connect handler
      [&connection_accepted](struct mg_connection* c, const HttpRequest& req) -> bool {
        connection_accepted = true;

        // Check for authentication or other connection criteria
        auto auth_header = req.headers.find("Authorization");
        if (auth_header != req.headers.end() && auth_header->second == "Bearer valid-token") {
          return true;  // Accept connection
        }

        // For this test, accept all connections
        return true;
      },
      // Close handler
      [&connection_closed](struct mg_connection* c) { connection_closed = true; });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, WebSocketWithCors) {
  // Enable CORS for WebSocket upgrades
  server_->allow_all_origins();

  server_->websocket("/ws/cors", [](struct mg_connection* c, const std::string& message) {
    websocket::send_text(c, "CORS WebSocket: " + message);
  });

  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}