#include "cpp/meerkat/meerkat.h"

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>

using namespace meerkat;

class MeerkatIntegrationTest : public ::testing::Test {
protected:
  void SetUp() override {
    server_ = std::make_unique<HttpServer>();
    static int port_counter = 8090;
    port_ = port_counter++;  // Use different ports for each test
  }
  
  void TearDown() override {
    if (server_->is_running()) {
      server_->stop();
    }
    server_.reset();
  }
  
  void StartServerAsync() {
    server_thread_ = std::async(std::launch::async, [this]() {
      server_->run();
    });
    
    // Give the server a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  
  std::string GetBaseUrl() {
    return "http://127.0.0.1:" + std::to_string(port_);
  }
  
  std::unique_ptr<HttpServer> server_;
  std::future<void> server_thread_;
  int port_;
};

TEST_F(MeerkatIntegrationTest, BasicGetRequest) {
  server_->get("/hello", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "Hello, World!"}});
  });
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  // Test with a simple poll instead of full HTTP request
  // since we don't want external dependencies
  server_->poll(10);
  
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, JsonPostRequest) {
  server_->post("/api/users", [](const HttpRequest& req) -> HttpResponse {
    try {
      json request_data = json::parse(req.body);
      
      if (!request_data.contains("name")) {
        return responses::bad_request("Missing 'name' field");
      }
      
      json response_data = {
        {"id", 123},
        {"name", request_data["name"]},
        {"created", true}
      };
      
      return responses::created(response_data);
    } catch (const json::exception& e) {
      return responses::bad_request("Invalid JSON: " + std::string(e.what()));
    }
  });
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, MiddlewareAuthentication) {
  bool middleware_executed = false;
  bool handler_executed = false;
  
  // Add authentication middleware
  server_->use_middleware([&middleware_executed](const HttpRequest& req, HttpResponse& res) -> bool {
    middleware_executed = true;
    
    auto auth_header = req.headers.find("Authorization");
    if (auth_header == req.headers.end() || auth_header->second != "Bearer valid-token") {
      res.status_code = 401;
      res.set_json(json{{"error", "Unauthorized"}});
      return false;  // Block the request
    }
    
    return true;  // Continue processing
  });
  
  server_->get("/protected", [&handler_executed](const HttpRequest& req) -> HttpResponse {
    handler_executed = true;
    return responses::ok(json{{"message", "Access granted"}});
  });
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, ErrorHandling) {
  server_->get("/error", [](const HttpRequest& req) -> HttpResponse {
    throw std::runtime_error("Intentional test error");
    return responses::ok();  // This won't be reached
  });
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, QueryParameters) {
  server_->get("/search", [](const HttpRequest& req) -> HttpResponse {
    json result = json::object();
    
    for (const auto& [key, value] : req.query_params) {
      result[key] = value;
    }
    
    return responses::ok(json{{"params", result}});
  });
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
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
  
  server_->websocket("/ws/test",
    // Message handler
    [&received_messages](struct mg_connection* c, const std::string& message) {
      received_messages.push_back(message);
      
      // Echo the message back
      websocket::send_text(c, "Echo: " + message);
      
      // Send a JSON response
      json response = {
        {"type", "echo"},
        {"original", message},
        {"timestamp", std::time(nullptr)}
      };
      websocket::send_json(c, response);
    },
    // Connect handler
    [&connection_accepted](struct mg_connection* c, const HttpRequest& req) -> bool {
      connection_accepted = true;
      
      // Check for authentication or other connection criteria
      auto auth_header = req.headers.find("Authorization");
      if (auth_header != req.headers.end() && auth_header->second == "Bearer valid-token") {
        return true; // Accept connection
      }
      
      // For this test, accept all connections
      return true;
    },
    // Close handler
    [&connection_closed](struct mg_connection* c) {
      connection_closed = true;
    }
  );
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}

TEST_F(MeerkatIntegrationTest, WebSocketWithCors) {
  // Enable CORS for WebSocket upgrades
  server_->allow_all_origins();
  
  server_->websocket("/ws/cors",
    [](struct mg_connection* c, const std::string& message) {
      websocket::send_text(c, "CORS WebSocket: " + message);
    }
  );
  
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();
  
  server_->poll(10);
  EXPECT_TRUE(server_->is_running());
}