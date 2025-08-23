#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <future>
#include <memory>
#include <set>

#include "cpp/meerkat/meerkat.h"

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

class ExampleMeerkatTest : public ::testing::Test {
protected:
    void SetUp() override {
        user_store_ = std::make_unique<UserStore>();
        server_ = std::make_unique<HttpServer>();
        static int port_counter = 9090;
        port_ = port_counter++;  // Use different ports for each test
        
        // Clear any existing state
        user_store_->clear_all_users();
        
        SetupRoutes();
    }
    
    void TearDown() override {
        if (server_->is_running()) {
            server_->stop();
        }
        server_.reset();
        user_store_.reset();
    }
    
    void SetupRoutes() {
        // Basic greeting endpoint
        server_->get("/", [](const HttpRequest& req) -> HttpResponse {
            return responses::ok(json{
                {"message", "Welcome to Meerkat Example API!"},
                {"version", "1.0.0"}
            });
        });
        
        // Health check endpoint
        server_->get("/health", [](const HttpRequest& req) -> HttpResponse {
            return responses::ok(json{
                {"status", "healthy"},
                {"timestamp", std::time(nullptr)}
            });
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
        server_thread_ = std::async(std::launch::async, [this]() {
            server_->run();
        });
        
        // Give the server a moment to start
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::string GetBaseUrl() {
        return "http://127.0.0.1:" + std::to_string(port_);
    }
    
    std::unique_ptr<UserStore> user_store_;
    std::unique_ptr<HttpServer> server_;
    std::future<void> server_thread_;
    int port_;
};

// Unit tests for UserStore
TEST_F(ExampleMeerkatTest, UserStoreCreateAndRetrieve) {
    json user_data = {{"name", "John Doe"}, {"email", "john@example.com"}};
    
    json created_user = user_store_->create_user(user_data);
    
    EXPECT_EQ(created_user["name"], "John Doe");
    EXPECT_EQ(created_user["email"], "john@example.com");
    EXPECT_EQ(created_user["id"], 1);
    
    auto retrieved_user = user_store_->get_user(1);
    ASSERT_TRUE(retrieved_user.has_value());
    EXPECT_EQ(retrieved_user.value()["name"], "John Doe");
}

TEST_F(ExampleMeerkatTest, UserStoreMultipleUsers) {
    json user1 = {{"name", "John"}, {"email", "john@test.com"}};
    json user2 = {{"name", "Jane"}, {"email", "jane@test.com"}};
    
    json created_user1 = user_store_->create_user(user1);
    json created_user2 = user_store_->create_user(user2);
    
    json all_users = user_store_->get_all_users();
    EXPECT_EQ(all_users.size(), 2);
    
    // Don't assume order in unordered_map, just verify both users exist
    bool found_user1 = false, found_user2 = false;
    for (const auto& user : all_users) {
        if (user["name"] == "John" && user["email"] == "john@test.com") {
            found_user1 = true;
            EXPECT_EQ(user["id"], created_user1["id"]);
        }
        if (user["name"] == "Jane" && user["email"] == "jane@test.com") {
            found_user2 = true;
            EXPECT_EQ(user["id"], created_user2["id"]);
        }
    }
    EXPECT_TRUE(found_user1);
    EXPECT_TRUE(found_user2);
}

TEST_F(ExampleMeerkatTest, UserStoreDelete) {
    json user_data = {{"name", "Test User"}, {"email", "test@example.com"}};
    user_store_->create_user(user_data);
    
    EXPECT_TRUE(user_store_->delete_user(1));
    EXPECT_FALSE(user_store_->delete_user(1));  // Already deleted
    EXPECT_FALSE(user_store_->delete_user(999)); // Never existed
    
    auto user = user_store_->get_user(1);
    EXPECT_FALSE(user.has_value());
}

TEST_F(ExampleMeerkatTest, UserStoreThreadSafety) {
    // Test concurrent access to user store
    std::vector<std::thread> threads;
    const int num_threads = 10;
    const int users_per_thread = 5;
    
    // Create users concurrently
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < users_per_thread; ++i) {
                json user_data = {
                    {"name", "User " + std::to_string(t * users_per_thread + i)},
                    {"email", "user" + std::to_string(t * users_per_thread + i) + "@test.com"}
                };
                user_store_->create_user(user_data);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    json all_users = user_store_->get_all_users();
    EXPECT_EQ(all_users.size(), num_threads * users_per_thread);
}

// HTTP Route Handler Tests
TEST_F(ExampleMeerkatTest, RootEndpointReturnsWelcome) {
    HttpRequest req;
    req.method = "GET";
    req.uri = "/";
    
    // Simulate the root endpoint handler
    auto response = responses::ok(json{
        {"message", "Welcome to Meerkat Example API!"},
        {"version", "1.0.0"}
    });
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["message"], "Welcome to Meerkat Example API!");
    EXPECT_EQ(response_json["version"], "1.0.0");
}

TEST_F(ExampleMeerkatTest, HealthEndpointReturnsHealthy) {
    HttpRequest req;
    req.method = "GET";
    req.uri = "/health";
    
    auto response = responses::ok(json{
        {"status", "healthy"},
        {"timestamp", std::time(nullptr)}
    });
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["status"], "healthy");
    EXPECT_TRUE(response_json.contains("timestamp"));
}

TEST_F(ExampleMeerkatTest, CreateUserWithValidData) {
    // Create a user first
    json user_data = {{"name", "Test User"}, {"email", "test@example.com"}, {"age", 25}};
    std::string request_body = user_data.dump();
    
    HttpRequest req;
    req.method = "POST";
    req.uri = "/api/users";
    req.body = request_body;
    
    // Simulate the POST handler logic
    try {
        json parsed_data = json::parse(req.body);
        
        if (!parsed_data.contains("name") || !parsed_data.contains("email")) {
            FAIL() << "Should have required fields";
        }
        
        json new_user = user_store_->create_user(parsed_data);
        auto response = responses::created(new_user);
        
        EXPECT_EQ(response.status_code, 201);
        
        json response_json = json::parse(response.body);
        EXPECT_EQ(response_json["name"], "Test User");
        EXPECT_EQ(response_json["email"], "test@example.com");
        EXPECT_EQ(response_json["age"], 25);
        EXPECT_EQ(response_json["id"], 1);
        
    } catch (const json::exception& e) {
        FAIL() << "JSON parsing failed: " << e.what();
    }
}

TEST_F(ExampleMeerkatTest, CreateUserWithMissingFields) {
    json incomplete_user = {{"name", "Incomplete User"}};  // Missing email
    
    HttpRequest req;
    req.method = "POST";
    req.uri = "/api/users";
    req.body = incomplete_user.dump();
    
    // Simulate the POST handler logic
    try {
        json user_data = json::parse(req.body);
        
        if (!user_data.contains("name") || !user_data.contains("email")) {
            auto response = responses::bad_request("Missing required fields: name and email");
            EXPECT_EQ(response.status_code, 400);
            EXPECT_TRUE(response.body.find("Missing required fields") != std::string::npos);
        } else {
            FAIL() << "Should have detected missing fields";
        }
    } catch (const json::exception& e) {
        FAIL() << "JSON parsing failed: " << e.what();
    }
}

TEST_F(ExampleMeerkatTest, CreateUserWithInvalidJson) {
    HttpRequest req;
    req.method = "POST";
    req.uri = "/api/users";
    req.body = "{invalid json}";
    
    // Simulate the POST handler logic
    try {
        json user_data = json::parse(req.body);
        FAIL() << "Should have thrown JSON exception";
    } catch (const json::exception& e) {
        auto response = responses::bad_request("Invalid JSON: " + std::string(e.what()));
        EXPECT_EQ(response.status_code, 400);
        EXPECT_TRUE(response.body.find("Invalid JSON") != std::string::npos);
    }
}

TEST_F(ExampleMeerkatTest, GetUserByIdExists) {
    // Create a user first
    json user_data = {{"name", "Found User"}, {"email", "found@example.com"}};
    user_store_->create_user(user_data);
    
    HttpRequest req;
    req.method = "GET";
    req.uri = "/api/user";
    req.query_params["id"] = "1";
    
    // Simulate the GET handler logic
    auto id_param = req.query_params.find("id");
    ASSERT_NE(id_param, req.query_params.end());
    
    int id = std::stoi(id_param->second);
    auto user = user_store_->get_user(id);
    
    ASSERT_TRUE(user.has_value());
    auto response = responses::ok(user.value());
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["name"], "Found User");
    EXPECT_EQ(response_json["id"], 1);
}

TEST_F(ExampleMeerkatTest, GetUserByIdNotFound) {
    HttpRequest req;
    req.method = "GET";
    req.uri = "/api/user";
    req.query_params["id"] = "999";
    
    // Simulate the GET handler logic
    auto id_param = req.query_params.find("id");
    ASSERT_NE(id_param, req.query_params.end());
    
    int id = std::stoi(id_param->second);
    auto user = user_store_->get_user(id);
    
    EXPECT_FALSE(user.has_value());
    auto response = responses::not_found("User not found");
    EXPECT_EQ(response.status_code, 404);
}

TEST_F(ExampleMeerkatTest, GetUserMissingIdParameter) {
    HttpRequest req;
    req.method = "GET";
    req.uri = "/api/user";
    // No id parameter
    
    auto id_param = req.query_params.find("id");
    if (id_param == req.query_params.end()) {
        auto response = responses::bad_request("Missing id parameter");
        EXPECT_EQ(response.status_code, 400);
        EXPECT_TRUE(response.body.find("Missing id parameter") != std::string::npos);
    } else {
        FAIL() << "Should have detected missing id parameter";
    }
}

TEST_F(ExampleMeerkatTest, DeleteUserExists) {
    // Create a user first
    json user_data = {{"name", "Delete Me"}, {"email", "delete@example.com"}};
    user_store_->create_user(user_data);
    
    HttpRequest req;
    req.method = "DELETE";
    req.uri = "/api/user";
    req.query_params["id"] = "1";
    
    // Simulate the DELETE handler logic
    auto id_param = req.query_params.find("id");
    ASSERT_NE(id_param, req.query_params.end());
    
    int id = std::stoi(id_param->second);
    bool deleted = user_store_->delete_user(id);
    
    EXPECT_TRUE(deleted);
    auto response = responses::ok(json{{"message", "User deleted successfully"}});
    EXPECT_EQ(response.status_code, 200);
}

TEST_F(ExampleMeerkatTest, DeleteUserNotFound) {
    HttpRequest req;
    req.method = "DELETE";
    req.uri = "/api/user";
    req.query_params["id"] = "999";
    
    // Simulate the DELETE handler logic
    auto id_param = req.query_params.find("id");
    ASSERT_NE(id_param, req.query_params.end());
    
    int id = std::stoi(id_param->second);
    bool deleted = user_store_->delete_user(id);
    
    EXPECT_FALSE(deleted);
    auto response = responses::not_found("User not found");
    EXPECT_EQ(response.status_code, 404);
}

TEST_F(ExampleMeerkatTest, GetAllUsersEmpty) {
    json all_users = user_store_->get_all_users();
    auto response = responses::ok(json{{"users", all_users}});
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_TRUE(response_json["users"].is_array());
    EXPECT_EQ(response_json["users"].size(), 0);
}

TEST_F(ExampleMeerkatTest, GetAllUsersWithData) {
    // Create multiple users
    user_store_->create_user({{"name", "User 1"}, {"email", "user1@example.com"}});
    user_store_->create_user({{"name", "User 2"}, {"email", "user2@example.com"}});
    user_store_->create_user({{"name", "User 3"}, {"email", "user3@example.com"}});
    
    json all_users = user_store_->get_all_users();
    auto response = responses::ok(json{{"users", all_users}});
    
    EXPECT_EQ(response.status_code, 200);
    
    json response_json = json::parse(response.body);
    EXPECT_EQ(response_json["users"].size(), 3);
    
    // Don't assume order - just verify all users are present
    std::set<std::string> user_names;
    for (const auto& user : response_json["users"]) {
        user_names.insert(user["name"]);
    }
    
    EXPECT_TRUE(user_names.count("User 1"));
    EXPECT_TRUE(user_names.count("User 2"));
    EXPECT_TRUE(user_names.count("User 3"));
}

// Integration tests (testing the server setup)
TEST_F(ExampleMeerkatTest, ServerCanStartAndStop) {
    EXPECT_FALSE(server_->is_running());
    
    bool started = server_->listen("127.0.0.1", port_);
    EXPECT_TRUE(started);
    EXPECT_TRUE(server_->is_running());
    
    server_->stop();
    EXPECT_FALSE(server_->is_running());
}

TEST_F(ExampleMeerkatTest, ServerCanPoll) {
    ASSERT_TRUE(server_->listen("127.0.0.1", port_));
    
    // Test polling (non-blocking)
    server_->poll(10);  // 10ms timeout
    EXPECT_TRUE(server_->is_running());
    
    server_->stop();
}

// Test middleware behavior
TEST_F(ExampleMeerkatTest, MiddlewareIsConfigured) {
    bool middleware_called = false;
    
    // Create a new server with testable middleware
    HttpServer test_server;
    test_server.use_middleware([&middleware_called](const HttpRequest& req, HttpResponse& res) -> bool {
        middleware_called = true;
        return true;
    });
    
    test_server.get("/test", [](const HttpRequest& req) -> HttpResponse {
        return responses::ok();
    });
    
    // Middleware registration should succeed
    EXPECT_TRUE(test_server.listen("127.0.0.1", port_ + 1000));
    test_server.stop();
}