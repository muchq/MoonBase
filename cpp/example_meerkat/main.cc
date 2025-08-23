#include <iostream>
#include <mutex>
#include <unordered_map>

#include "cpp/meerkat/meerkat.h"

using namespace meerkat;

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
};

int main() {
  std::cout << "Starting Meerkat Example Server..." << std::endl;

  HttpServer server;
  UserStore user_store;

  // Basic greeting endpoint
  server.get("/", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(
        json{{"message", "Welcome to Meerkat Example API!"},
             {"version", "1.0.0"},
             {"endpoints", json::array({"GET /", "GET /health", "GET /api/users", "POST /api/users",
                                        "GET /api/users/{id}", "DELETE /api/users/{id}"})}});
  });

  // Health check endpoint
  server.get("/health", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"status", "healthy"}, {"timestamp", std::time(nullptr)}});
  });

  // Get all users
  server.get("/api/users", [&user_store](const HttpRequest& req) -> HttpResponse {
    json users = user_store.get_all_users();
    return responses::ok(json{{"users", users}});
  });

  // Create a new user
  server.post("/api/users", [&user_store](const HttpRequest& req) -> HttpResponse {
    try {
      json user_data = json::parse(req.body);

      // Validate required fields
      if (!user_data.contains("name") || !user_data.contains("email")) {
        return responses::bad_request("Missing required fields: name and email");
      }

      json new_user = user_store.create_user(user_data);
      return responses::created(new_user);

    } catch (const json::exception& e) {
      return responses::bad_request("Invalid JSON: " + std::string(e.what()));
    }
  });

  // Get user by ID (simplified - using query parameter)
  server.get("/api/user", [&user_store](const HttpRequest& req) -> HttpResponse {
    auto id_param = req.query_params.find("id");
    if (id_param == req.query_params.end()) {
      return responses::bad_request("Missing id parameter");
    }

    try {
      int id = std::stoi(id_param->second);
      auto user = user_store.get_user(id);
      if (user.has_value()) {
        return responses::ok(user.value());
      } else {
        return responses::not_found("User not found");
      }
    } catch (const std::exception& e) {
      return responses::bad_request("Invalid user ID");
    }
  });

  // Delete user by ID (simplified - using query parameter)
  server.del("/api/user", [&user_store](const HttpRequest& req) -> HttpResponse {
    auto id_param = req.query_params.find("id");
    if (id_param == req.query_params.end()) {
      return responses::bad_request("Missing id parameter");
    }

    try {
      int id = std::stoi(id_param->second);
      if (user_store.delete_user(id)) {
        return responses::ok(json{{"message", "User deleted successfully"}});
      } else {
        return responses::not_found("User not found");
      }
    } catch (const std::exception& e) {
      return responses::bad_request("Invalid user ID");
    }
  });

  // Add logging middleware
  server.use_middleware([](const HttpRequest& req, HttpResponse& res) -> bool {
    std::cout << "[" << std::time(nullptr) << "] " << req.method << " " << req.uri << std::endl;
    return true;
  });

  // Enable CORS for development
  server.allow_all_origins();

  // Start the server
  const std::string host = "127.0.0.1";
  const int port = 8080;

  if (server.listen(host, port)) {
    std::cout << "Meerkat Example Server running on http://" << host << ":" << port << std::endl;
    std::cout << "Try these endpoints:" << std::endl;
    std::cout << "  GET  http://localhost:8080/" << std::endl;
    std::cout << "  GET  http://localhost:8080/health" << std::endl;
    std::cout << "  GET  http://localhost:8080/api/users" << std::endl;
    std::cout << "  POST http://localhost:8080/api/users (with JSON body)" << std::endl;
    std::cout << "  GET  http://localhost:8080/api/user?id=1" << std::endl;
    std::cout << "  DELETE http://localhost:8080/api/user?id=1" << std::endl;
    std::cout << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;

    server.run();
  } else {
    std::cerr << "Failed to start server on " << host << ":" << port << std::endl;
    return 1;
  }

  return 0;
}