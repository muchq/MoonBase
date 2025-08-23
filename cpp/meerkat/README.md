# Meerkat - Modern C++ HTTP Server

Meerkat is an easy-to-use, RAII-compliant modern C++ HTTP server wrapper built on top of Mongoose. It provides a clean, type-safe interface with built-in JSON serialization using nlohmann/json.

## Features

- **RAII Compliant**: Automatic resource management with proper constructors/destructors
- **Modern C++**: Uses C++20 features like `std::function`, `std::unordered_map`, and move semantics
- **JSON First**: Built-in JSON serialization and deserialization with nlohmann/json
- **Middleware Support**: Composable request/response middleware pipeline
- **CORS Support**: Simple CORS configuration with flexible origin, method, and header controls
- **WebSocket Support**: Full-duplex WebSocket communication with message, connect, and close handlers
- **Static File Serving**: Easy static file serving with configurable paths
- **Thread Safe**: Safe to use across multiple threads
- **Exception Safe**: Automatic error handling and recovery
- **Non-blocking**: Poll-based event loop for high performance

## Quick Start

```cpp
#include "cpp/meerkat/meerkat.h"

using namespace meerkat;

int main() {
    HttpServer server;
    
    // Simple GET endpoint
    server.get("/hello", [](const HttpRequest& req) -> HttpResponse {
        return responses::ok(json{{"message", "Hello, World!"}});
    });
    
    // POST endpoint with JSON
    server.post("/api/users", [](const HttpRequest& req) -> HttpResponse {
        try {
            json user_data = json::parse(req.body);
            
            // Process user creation...
            json response = {
                {"id", 123},
                {"name", user_data["name"]},
                {"created", true}
            };
            
            return responses::created(response);
        } catch (const json::exception& e) {
            return responses::bad_request("Invalid JSON");
        }
    });
    
    // Enable CORS for frontend access
    server.allow_all_origins();
    
    // Add WebSocket support
    server.websocket("/ws", [](struct mg_connection* c, const std::string& message) {
        // Echo messages back to client
        websocket::send_text(c, "Echo: " + message);
    });
    
    // Start the server
    if (server.listen("127.0.0.1", 8080)) {
        std::cout << "Server running on http://127.0.0.1:8080" << std::endl;
        server.run();  // Blocks until server.stop() is called
    }
    
    return 0;
}
```

## Building with Bazel

Add the meerkat library as a dependency in your `BUILD.bazel` file:

```bazel
cc_binary(
    name = "my_server",
    srcs = ["main.cc"],
    deps = [
        "//cpp/meerkat",
    ],
)
```

Build your application:

```bash
bazel build //path/to/your:my_server
```

## API Reference

### HttpServer

The main server class that handles HTTP requests and responses.

#### Constructor and Destructor

```cpp
HttpServer server;  // RAII - automatically initializes mongoose
// Destructor automatically cleans up resources
```

#### Starting and Stopping

```cpp
bool listen(const std::string& address, int port);  // Start listening
void stop();                                        // Stop the server
bool is_running() const;                           // Check if running

void poll(int timeout_ms = 100);  // Non-blocking poll
void run();                       // Blocking run loop
```

#### Route Registration

```cpp
void get(const std::string& path, RouteHandler handler);
void post(const std::string& path, RouteHandler handler);
void put(const std::string& path, RouteHandler handler);
void del(const std::string& path, RouteHandler handler);
void route(const std::string& method, const std::string& path, RouteHandler handler);
```

#### Middleware

```cpp
void use_middleware(MiddlewareHandler middleware);
```

Middleware functions receive the request and response, and return `true` to continue processing or `false` to stop:

```cpp
server.use_middleware([](const HttpRequest& req, HttpResponse& res) -> bool {
    // Authentication, logging, etc.
    if (req.headers.find("Authorization") == req.headers.end()) {
        res = responses::bad_request("Missing authorization");
        return false;  // Stop processing
    }
    return true;  // Continue to route handler
});
```

#### Static File Serving

```cpp
void serve_static(const std::string& path_prefix, const std::string& directory);

// Example:
server.serve_static("/static", "/var/www/html");
```

#### CORS Configuration

```cpp
// Simple CORS - allow all origins
server.allow_all_origins();

// Allow specific origin
server.allow_origin("https://myapp.com");

// Advanced CORS configuration
HttpServer::CorsConfig cors_config;
cors_config.allowed_origins = {"https://app1.com", "https://app2.com"};
cors_config.allowed_methods = {"GET", "POST", "PUT", "DELETE"};
cors_config.allowed_headers = {"Content-Type", "Authorization", "X-API-Key"};
cors_config.exposed_headers = {"X-Total-Count"};
cors_config.allow_credentials = true;
cors_config.max_age = 3600; // 1 hour

server.enable_cors(cors_config);
```

#### WebSocket Support

```cpp
server.websocket("/ws/path", 
    // Message handler (required)
    [](struct mg_connection* c, const std::string& message) {
        websocket::send_text(c, "Echo: " + message);
        
        // Or send JSON
        json response = {{"type", "echo"}, {"data", message}};
        websocket::send_json(c, response);
    },
    // Connect handler (optional) - return true to accept, false to reject
    [](struct mg_connection* c, const HttpRequest& req) -> bool {
        // Check authentication, origin, etc.
        return req.headers.find("Authorization") != req.headers.end();
    },
    // Close handler (optional)
    [](struct mg_connection* c) {
        std::cout << "WebSocket connection closed" << std::endl;
    }
);
```

### HttpRequest

Structure containing all request information:

```cpp
struct HttpRequest {
    std::string method;                                        // GET, POST, etc.
    std::string uri;                                          // Request path
    std::string body;                                         // Request body
    std::unordered_map<std::string, std::string> headers;     // HTTP headers
    std::unordered_map<std::string, std::string> query_params; // Query parameters
};
```

### HttpResponse

Structure for building responses:

```cpp
struct HttpResponse {
    int status_code = 200;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    
    void set_json(const json& j);           // Set JSON body
    void set_text(const std::string& text); // Set plain text body
};
```

### Response Utilities

Pre-built response functions for common HTTP status codes:

```cpp
namespace responses {
    HttpResponse ok(const json& data = json::object());
    HttpResponse created(const json& data = json::object());
    HttpResponse bad_request(const std::string& message = "Bad Request");
    HttpResponse not_found(const std::string& message = "Not Found");
    HttpResponse internal_error(const std::string& message = "Internal Server Error");
}
```

### WebSocket Utilities

Send messages to WebSocket clients:

```cpp
namespace websocket {
    void send_text(struct mg_connection* c, const std::string& message);
    void send_json(struct mg_connection* c, const json& data);
    void send_binary(struct mg_connection* c, const void* data, size_t length);
    void close(struct mg_connection* c, int code = 1000, const std::string& reason = "");
}
```

## Advanced Examples

### RESTful API

```cpp
class UserController {
private:
    std::unordered_map<int, json> users_;
    int next_id_ = 1;

public:
    HttpResponse get_users(const HttpRequest& req) {
        json users_array = json::array();
        for (const auto& [id, user] : users_) {
            users_array.push_back(user);
        }
        return responses::ok(json{{"users", users_array}});
    }
    
    HttpResponse create_user(const HttpRequest& req) {
        try {
            json user_data = json::parse(req.body);
            
            if (!user_data.contains("name") || !user_data.contains("email")) {
                return responses::bad_request("Missing required fields");
            }
            
            int id = next_id_++;
            user_data["id"] = id;
            users_[id] = user_data;
            
            return responses::created(user_data);
        } catch (const json::exception& e) {
            return responses::bad_request("Invalid JSON");
        }
    }
    
    HttpResponse get_user(const HttpRequest& req) {
        // Extract ID from URL path (simplified - real implementation would use path parameters)
        std::string id_str = req.query_params.at("id");
        int id = std::stoi(id_str);
        
        if (users_.find(id) == users_.end()) {
            return responses::not_found("User not found");
        }
        
        return responses::ok(users_[id]);
    }
};

int main() {
    HttpServer server;
    UserController controller;
    
    server.get("/api/users", [&](const HttpRequest& req) {
        return controller.get_users(req);
    });
    
    server.post("/api/users", [&](const HttpRequest& req) {
        return controller.create_user(req);
    });
    
    server.get("/api/user", [&](const HttpRequest& req) {
        return controller.get_user(req);
    });
    
    if (server.listen("0.0.0.0", 3000)) {
        server.run();
    }
}
```

### With Authentication Middleware

```cpp
HttpServer server;

// Authentication middleware
server.use_middleware([](const HttpRequest& req, HttpResponse& res) -> bool {
    // Skip auth for public endpoints
    if (req.uri.starts_with("/public")) {
        return true;
    }
    
    auto auth_header = req.headers.find("Authorization");
    if (auth_header == req.headers.end()) {
        res = responses::bad_request("Missing Authorization header");
        return false;
    }
    
    if (!auth_header->second.starts_with("Bearer ")) {
        res = responses::bad_request("Invalid Authorization format");
        return false;
    }
    
    // Validate token (simplified)
    std::string token = auth_header->second.substr(7);
    if (token != "valid-secret-token") {
        res.status_code = 401;
        res.set_json(json{{"error", "Invalid token"}});
        return false;
    }
    
    return true;
});

// Protected route
server.get("/api/protected", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"message", "You are authenticated!"}});
});

// Public route
server.get("/public/health", [](const HttpRequest& req) -> HttpResponse {
    return responses::ok(json{{"status", "healthy"}});
});
```

### WebSocket Chat Server

```cpp
#include <unordered_set>
#include <mutex>

class ChatServer {
private:
    std::unordered_set<struct mg_connection*> clients_;
    std::mutex clients_mutex_;

public:
    void add_client(struct mg_connection* c) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.insert(c);
    }
    
    void remove_client(struct mg_connection* c) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.erase(c);
    }
    
    void broadcast_message(const std::string& message) {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        
        json broadcast = {
            {"type", "message"},
            {"content", message},
            {"timestamp", std::time(nullptr)},
            {"clients_count", clients_.size()}
        };
        
        for (auto* client : clients_) {
            websocket::send_json(client, broadcast);
        }
    }
    
    void handle_message(struct mg_connection* c, const std::string& message) {
        try {
            json msg = json::parse(message);
            
            if (msg["type"] == "chat") {
                std::string username = msg.value("username", "Anonymous");
                std::string content = msg["content"];
                
                json chat_message = {
                    {"type", "chat"},
                    {"username", username},
                    {"content", content},
                    {"timestamp", std::time(nullptr)}
                };
                
                // Broadcast to all clients
                std::lock_guard<std::mutex> lock(clients_mutex_);
                for (auto* client : clients_) {
                    websocket::send_json(client, chat_message);
                }
            } else if (msg["type"] == "ping") {
                websocket::send_json(c, json{{"type", "pong"}});
            }
        } catch (const json::exception& e) {
            websocket::send_json(c, json{
                {"type", "error"},
                {"message", "Invalid JSON message"}
            });
        }
    }
};

int main() {
    HttpServer server;
    ChatServer chat;
    
    // Enable CORS for web clients
    server.allow_all_origins();
    
    // WebSocket endpoint for chat
    server.websocket("/ws/chat",
        // Message handler
        [&chat](struct mg_connection* c, const std::string& message) {
            chat.handle_message(c, message);
        },
        // Connect handler
        [&chat](struct mg_connection* c, const HttpRequest& req) -> bool {
            chat.add_client(c);
            
            // Send welcome message
            websocket::send_json(c, json{
                {"type", "welcome"},
                {"message", "Welcome to the chat!"}
            });
            
            return true;
        },
        // Close handler
        [&chat](struct mg_connection* c) {
            chat.remove_client(c);
        }
    );
    
    // REST API for chat history, user management, etc.
    server.get("/api/stats", [&chat](const HttpRequest& req) -> HttpResponse {
        return responses::ok(json{
            {"active_connections", chat.clients_.size()},
            {"server_uptime", std::time(nullptr)}
        });
    });
    
    if (server.listen("0.0.0.0", 8080)) {
        std::cout << "Chat server running on http://0.0.0.0:8080" << std::endl;
        server.run();
    }
}
```

### CORS Configuration Examples

```cpp
// Example 1: Production CORS setup
HttpServer::CorsConfig prod_cors;
prod_cors.allowed_origins = {
    "https://myapp.com", 
    "https://www.myapp.com",
    "https://admin.myapp.com"
};
prod_cors.allowed_methods = {"GET", "POST", "PUT", "DELETE", "PATCH"};
prod_cors.allowed_headers = {
    "Content-Type", 
    "Authorization", 
    "X-API-Key",
    "X-Requested-With"
};
prod_cors.exposed_headers = {
    "X-Total-Count",
    "X-Page-Count", 
    "Link"
};
prod_cors.allow_credentials = true;
prod_cors.max_age = 86400; // 24 hours

server.enable_cors(prod_cors);

// Example 2: Development CORS setup
server.allow_all_origins(); // Simple but less secure

// Example 3: API-specific CORS
HttpServer api_server;
api_server.allow_origin("https://dashboard.myapp.com");

// Add API routes with CORS automatically handled
api_server.get("/api/v1/users", [](const HttpRequest& req) -> HttpResponse {
    // CORS headers automatically added to response
    return responses::ok(json{{"users", json::array()}});
});
```

## Testing

Run the tests with Bazel:

```bash
# Unit tests
bazel test //cpp/meerkat:meerkat_test

# Integration tests
bazel test //cpp/meerkat:meerkat_integration_test

# All tests
bazel test //cpp/meerkat:...
```

## Dependencies

- **Mongoose**: HTTP server implementation
- **nlohmann/json**: JSON serialization/deserialization
- **Abseil**: String utilities and other common functionality
- **GoogleTest**: Testing framework (test dependencies only)

## Design Principles

1. **RAII Compliance**: All resources are automatically managed
2. **Exception Safety**: All operations are exception safe
3. **Move Semantics**: Efficient resource transfer with move constructors/assignment
4. **Type Safety**: Strong typing throughout the API
5. **Performance**: Zero-copy operations where possible
6. **Simplicity**: Easy to use API with sensible defaults

## Thread Safety

The HttpServer is thread-safe for route registration and middleware registration when done before calling `listen()`. Once the server is running, route handlers may be called from multiple threads, so ensure your handlers are thread-safe if they share mutable state.

## Performance Considerations

- Use `poll()` instead of `run()` if you need to integrate with other event loops
- Middleware is executed in registration order - put expensive middleware last
- Consider connection pooling for database operations within handlers
- Static file serving bypasses the route handler system for better performance

## Contributing

This library is part of the MoonBase project. Follow the existing code style and add tests for new features.