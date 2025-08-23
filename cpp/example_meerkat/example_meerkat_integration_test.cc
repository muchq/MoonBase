#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <set>
#include <thread>

#include "cpp/meerkat/meerkat.h"
#include "cpp/meerkat/http_client.h"

using namespace meerkat;

// UserStore class (copied from main.cc for testing)
class UserStore {
 private:
  std::unordered_map<int, json> users_;
  int next_id_ = 1;
  std::mutex mutex_;

 public:
  json get_all_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    json users_array = json::array();
    for (const auto& [id, user] : users_) {
      users_array.push_back(user);
    }
    return users_array;
  }

  json create_user(const json& user_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    int id = next_id_++;
    json new_user = user_data;
    new_user["id"] = id;
    users_[id] = new_user;
    return new_user;
  }

  std::optional<json> get_user(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(id);
    if (it != users_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  bool delete_user(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.erase(id) > 0;
  }

  // Test helper to reset state
  void clear_all_users() {
    std::lock_guard<std::mutex> lock(mutex_);
    users_.clear();
    next_id_ = 1;
  }
};

class ExampleMeerkatIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    user_store_ = std::make_unique<UserStore>();
    server_ = std::make_unique<HttpServer>();
    client_ = std::make_unique<HttpClient>();
    static int port_counter = 9090;
    port_ = port_counter++;  // Use different ports for each test

    // Clear any existing state
    user_store_->clear_all_users();

    SetupRoutes();
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
    user_store_.reset();
  }

  void SetupRoutes() {
    // Basic greeting endpoint
    server_->get("/", [](const HttpRequest& req) -> HttpResponse {
      return responses::ok(
          json{{"message", "Welcome to Meerkat Example API!"}, {"version", "1.0.0"}});
    });

    // Health check endpoint
    server_->get("/health", [](const HttpRequest& req) -> HttpResponse {
      return responses::ok(json{{"status", "healthy"}, {"timestamp", std::time(nullptr)}});
    });

    // Get all users
    server_->get("/api/users", [this](const HttpRequest& req) -> HttpResponse {
      json users = user_store_->get_all_users();
      return responses::ok(json{{"users", users}});
    });

    // Create a new user
    server_->post("/api/users", [this](const HttpRequest& req) -> HttpResponse {
      try {
        json user_data = json::parse(req.body);

        // Validate required fields
        if (!user_data.contains("name") || !user_data.contains("email")) {
          return responses::bad_request("Missing required fields: name and email");
        }

        json new_user = user_store_->create_user(user_data);
        return responses::created(new_user);

      } catch (const json::exception& e) {
        return responses::bad_request("Invalid JSON: " + std::string(e.what()));
      }
    });

    // Get user by ID
    server_->get("/api/user", [this](const HttpRequest& req) -> HttpResponse {
      auto id_param = req.query_params.find("id");
      if (id_param == req.query_params.end()) {
        return responses::bad_request("Missing id parameter");
      }

      try {
        int id = std::stoi(id_param->second);
        auto user = user_store_->get_user(id);
        if (user.has_value()) {
          return responses::ok(user.value());
        } else {
          return responses::not_found("User not found");
        }
      } catch (const std::exception& e) {
        return responses::bad_request("Invalid user ID");
      }
    });

    // Delete user by ID
    server_->del("/api/user", [this](const HttpRequest& req) -> HttpResponse {
      auto id_param = req.query_params.find("id");
      if (id_param == req.query_params.end()) {
        return responses::bad_request("Missing id parameter");
      }

      try {
        int id = std::stoi(id_param->second);
        if (user_store_->delete_user(id)) {
          return responses::ok(json{{"message", "User deleted successfully"}});
        } else {
          return responses::not_found("User not found");
        }
      } catch (const std::exception& e) {
        return responses::bad_request("Invalid user ID");
      }
    });

    // Add logging middleware
    server_->use_middleware([](const HttpRequest& req, HttpResponse& res) -> bool {
      // In tests, we don't want to spam stdout
      return true;
    });

    // Enable CORS
    server_->allow_all_origins();
  }

  void StartServerAsync() {
    server_thread_ = std::async(std::launch::async, [this]() { server_->run(); });

    // Give the server a moment to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::string GetBaseUrl() { return "http://127.0.0.1:" + std::to_string(port_); }

  std::unique_ptr<UserStore> user_store_;
  std::unique_ptr<HttpServer> server_;
  std::unique_ptr<HttpClient> client_;
  std::future<void> server_thread_;
  int port_;
};

// HTTP Integration Tests - Testing real HTTP requests
TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationRootEndpoint) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/", 10000);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);
  EXPECT_EQ(response.headers["Content-Type"], "application/json");

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["message"], "Welcome to Meerkat Example API!");
  EXPECT_EQ(response_json["version"], "1.0.0");
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationHealthEndpoint) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/health", 10000);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["status"], "healthy");
  EXPECT_TRUE(response_json.contains("timestamp"));
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationCreateUser) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  json user_data = {{"name", "John Doe"}, {"email", "john@example.com"}, {"age", 30}};
  auto response = client_->post_json(GetBaseUrl() + "/api/users", user_data, 10000);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 201);

  json response_json = json::parse(response.body);
  EXPECT_EQ(response_json["name"], "John Doe");
  EXPECT_EQ(response_json["email"], "john@example.com");
  EXPECT_EQ(response_json["age"], 30);
  EXPECT_EQ(response_json["id"], 1);
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationCreateUserMissingFields) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  json incomplete_user = {{"name", "Incomplete User"}};  // Missing email
  auto response = client_->post_json(GetBaseUrl() + "/api/users", incomplete_user, 10000);

  EXPECT_TRUE(response.success);  // Connection succeeded
  EXPECT_EQ(response.status_code, 400);  // But request was bad

  json response_json = json::parse(response.body);
  EXPECT_TRUE(response_json.contains("error"));
  EXPECT_TRUE(response_json["error"].get<std::string>().find("Missing required fields") != std::string::npos);
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationGetAllUsersEmpty) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/api/users", 10000);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);

  json response_json = json::parse(response.body);
  EXPECT_TRUE(response_json["users"].is_array());
  EXPECT_EQ(response_json["users"].size(), 0);
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationGetAllUsersWithData) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Create multiple users via HTTP
  json user1 = {{"name", "User 1"}, {"email", "user1@example.com"}};
  json user2 = {{"name", "User 2"}, {"email", "user2@example.com"}};
  json user3 = {{"name", "User 3"}, {"email", "user3@example.com"}};

  auto response1 = client_->post_json(GetBaseUrl() + "/api/users", user1, 10000);
  auto response2 = client_->post_json(GetBaseUrl() + "/api/users", user2, 10000);
  auto response3 = client_->post_json(GetBaseUrl() + "/api/users", user3, 10000);

  ASSERT_TRUE(response1.success && response1.status_code == 201);
  ASSERT_TRUE(response2.success && response2.status_code == 201);
  ASSERT_TRUE(response3.success && response3.status_code == 201);

  // Now get all users
  auto get_response = client_->get(GetBaseUrl() + "/api/users", 10000);

  EXPECT_TRUE(get_response.success);
  EXPECT_EQ(get_response.status_code, 200);

  json response_json = json::parse(get_response.body);
  EXPECT_EQ(response_json["users"].size(), 3);

  // Verify all users are present
  std::set<std::string> user_names;
  for (const auto& user : response_json["users"]) {
    user_names.insert(user["name"]);
  }

  EXPECT_TRUE(user_names.count("User 1"));
  EXPECT_TRUE(user_names.count("User 2"));
  EXPECT_TRUE(user_names.count("User 3"));
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationGetUserById) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Create a user first
  json user_data = {{"name", "Found User"}, {"email", "found@example.com"}};
  auto create_response = client_->post_json(GetBaseUrl() + "/api/users", user_data, 10000);
  
  ASSERT_TRUE(create_response.success);
  ASSERT_EQ(create_response.status_code, 201);

  json created_user = json::parse(create_response.body);
  int user_id = created_user["id"];

  // Now get the user by ID
  std::string get_url = GetBaseUrl() + "/api/user?id=" + std::to_string(user_id);
  auto get_response = client_->get(get_url, 10000);

  EXPECT_TRUE(get_response.success);
  EXPECT_EQ(get_response.status_code, 200);

  json response_json = json::parse(get_response.body);
  EXPECT_EQ(response_json["name"], "Found User");
  EXPECT_EQ(response_json["email"], "found@example.com");
  EXPECT_EQ(response_json["id"], user_id);
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationGetUserByIdNotFound) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/api/user?id=999", 10000);

  EXPECT_TRUE(response.success);  // Connection succeeded
  EXPECT_EQ(response.status_code, 404);  // But user not found

  json response_json = json::parse(response.body);
  EXPECT_TRUE(response_json.contains("error"));
  EXPECT_EQ(response_json["error"], "User not found");
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationGetUserMissingIdParameter) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/api/user", 10000);  // No id parameter

  EXPECT_TRUE(response.success);  // Connection succeeded
  EXPECT_EQ(response.status_code, 400);  // But missing parameter

  json response_json = json::parse(response.body);
  EXPECT_TRUE(response_json.contains("error"));
  EXPECT_EQ(response_json["error"], "Missing id parameter");
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationDeleteUser) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // Create a user first
  json user_data = {{"name", "Delete Me"}, {"email", "delete@example.com"}};
  auto create_response = client_->post_json(GetBaseUrl() + "/api/users", user_data, 10000);
  
  ASSERT_TRUE(create_response.success);
  ASSERT_EQ(create_response.status_code, 201);

  json created_user = json::parse(create_response.body);
  int user_id = created_user["id"];

  // Delete the user
  std::string delete_url = GetBaseUrl() + "/api/user?id=" + std::to_string(user_id);
  auto delete_response = client_->del(delete_url, 10000);

  EXPECT_TRUE(delete_response.success);
  EXPECT_EQ(delete_response.status_code, 200);

  json response_json = json::parse(delete_response.body);
  EXPECT_EQ(response_json["message"], "User deleted successfully");

  // Verify user is gone
  auto get_response = client_->get(GetBaseUrl() + "/api/user?id=" + std::to_string(user_id), 10000);
  EXPECT_TRUE(get_response.success);
  EXPECT_EQ(get_response.status_code, 404);  // User not found
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationDeleteUserNotFound) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->del(GetBaseUrl() + "/api/user?id=999", 10000);

  EXPECT_TRUE(response.success);  // Connection succeeded
  EXPECT_EQ(response.status_code, 404);  // But user not found

  json response_json = json::parse(response.body);
  EXPECT_TRUE(response_json.contains("error"));
  EXPECT_EQ(response_json["error"], "User not found");
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationCorsHeadersPresent) {
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  auto response = client_->get(GetBaseUrl() + "/", 10000);

  EXPECT_TRUE(response.success);
  EXPECT_EQ(response.status_code, 200);

  // CORS headers should be present due to allow_all_origins() in SetupRoutes
  EXPECT_TRUE(response.headers.count("Access-Control-Allow-Origin"));
  EXPECT_EQ(response.headers["Access-Control-Allow-Origin"], "*");
}

TEST_F(ExampleMeerkatIntegrationTest, HttpIntegrationCreateUserWorkflow) {
  // Test complete create -> retrieve -> delete workflow
  ASSERT_TRUE(server_->listen("127.0.0.1", port_));
  StartServerAsync();

  // 1. Create user
  json user_data = {{"name", "Workflow User"}, {"email", "workflow@example.com"}, {"department", "Engineering"}};
  auto create_response = client_->post_json(GetBaseUrl() + "/api/users", user_data, 10000);
  
  ASSERT_TRUE(create_response.success);
  ASSERT_EQ(create_response.status_code, 201);

  json created_user = json::parse(create_response.body);
  int user_id = created_user["id"];
  EXPECT_EQ(created_user["name"], "Workflow User");

  // 2. Retrieve user
  std::string get_url = GetBaseUrl() + "/api/user?id=" + std::to_string(user_id);
  auto get_response = client_->get(get_url, 10000);
  
  EXPECT_TRUE(get_response.success);
  EXPECT_EQ(get_response.status_code, 200);

  json retrieved_user = json::parse(get_response.body);
  EXPECT_EQ(retrieved_user["name"], "Workflow User");
  EXPECT_EQ(retrieved_user["email"], "workflow@example.com");
  EXPECT_EQ(retrieved_user["department"], "Engineering");

  // 3. Verify in user list
  auto list_response = client_->get(GetBaseUrl() + "/api/users", 10000);
  EXPECT_TRUE(list_response.success);
  EXPECT_EQ(list_response.status_code, 200);

  json list_json = json::parse(list_response.body);
  EXPECT_EQ(list_json["users"].size(), 1);
  EXPECT_EQ(list_json["users"][0]["name"], "Workflow User");

  // 4. Delete user
  std::string delete_url = GetBaseUrl() + "/api/user?id=" + std::to_string(user_id);
  auto delete_response = client_->del(delete_url, 10000);
  
  EXPECT_TRUE(delete_response.success);
  EXPECT_EQ(delete_response.status_code, 200);

  // 5. Verify user is gone
  auto final_get = client_->get(get_url, 10000);
  EXPECT_EQ(final_get.status_code, 404);

  auto final_list = client_->get(GetBaseUrl() + "/api/users", 10000);
  json final_list_json = json::parse(final_list.body);
  EXPECT_EQ(final_list_json["users"].size(), 0);
}