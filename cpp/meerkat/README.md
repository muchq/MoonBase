# Meerkat - Modern C++ HTTP Server

Meerkat is an easy-to-use C++ HTTP server wrapper built on top of Mongoose and nlohmann/json.

## Features

- **JSON First**: Built-in JSON serialization and deserialization with nlohmann/json
- **Middleware Support**: Composable request/response middleware pipeline
- **Exception Safe**: Automatic error handling and recovery
- **Non-blocking**: Poll-based event loop

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
    
    // Start the server
    if (server.listen("127.0.0.1", 8080)) {
        std::cout << "Server running on http://127.0.0.1:8080" << std::endl;
        server.run();  // Blocks until server.stop() is called
    }
    
    return 0;
}
```

## Build

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
server.use_middleware([](const HttpRequest& req, HttpResponse& res, Context& ctx) -> bool {
    // Authentication, logging, etc.
    if (req.headers.find("Authorization") == req.headers.end()) {
        res = responses::bad_request("Missing authorization");
        return false;  // Stop processing
    }
    return true;  // Continue to route handler
});
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

## Dependencies

- **Mongoose**: HTTP server implementation
- **nlohmann/json**: JSON serialization/deserialization
- **Abseil**: String utilities and other common functionality
- **GoogleTest**: Testing framework (test dependencies only)
