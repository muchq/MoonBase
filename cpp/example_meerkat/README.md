# Example Meerkat Service

A demo HTTP API service built with the Meerkat library to showcase its features and usage patterns.

## Features

This example demonstrates:

- **RESTful API**: Full CRUD operations for user management
- **JSON Handling**: Automatic JSON parsing and serialization
- **Middleware**: Request logging middleware
- **Error Handling**: Proper HTTP status codes and error responses
- **CORS Support**: Enabled for frontend development
- **Thread Safety**: Thread-safe user storage with mutex protection

## API Endpoints

### General
- `GET /` - Welcome message with API overview
- `GET /health` - Health check endpoint

### User Management
- `GET /api/users` - Get all users
- `POST /api/users` - Create a new user (requires JSON body with `name` and `email`)
- `GET /api/user?id={id}` - Get user by ID
- `DELETE /api/user?id={id}` - Delete user by ID

## Building and Running

Build with Bazel:
```bash
bazel build //cpp/example_meerkat
```

Run the service:
```bash
bazel run //cpp/example_meerkat
```

The server will start on `http://127.0.0.1:8080`.

## Example Usage

### Create a user
```bash
curl -X POST http://localhost:8080/api/users \
  -H "Content-Type: application/json" \
  -d '{"name": "John Doe", "email": "john@example.com", "age": 30}'
```

### Get all users
```bash
curl http://localhost:8080/api/users
```

### Get a specific user
```bash
curl http://localhost:8080/api/user?id=1
```

### Delete a user
```bash
curl -X DELETE http://localhost:8080/api/user?id=1
```

### Health check
```bash
curl http://localhost:8080/health
```

## Code Structure

The example includes:

- **UserStore Class**: Thread-safe in-memory user storage
- **Route Handlers**: Individual functions for each API endpoint
- **Middleware**: Request logging for all incoming requests
- **Error Handling**: Comprehensive error responses with appropriate HTTP status codes
- **JSON Validation**: Input validation for required fields

## Key Meerkat Features Demonstrated

1. **Easy Route Registration**: Simple `server.get()`, `server.post()`, etc.
2. **JSON-First Design**: Built-in JSON parsing and response utilities
3. **Middleware Pipeline**: Composable request processing
4. **CORS Support**: Simple cross-origin resource sharing setup
5. **Thread Safety**: Safe concurrent request handling
6. **Error Responses**: Pre-built response utilities for common HTTP status codes

## Testing

The example includes comprehensive tests covering both unit and integration testing patterns for Meerkat services.

### Running Tests

Build and run the tests with Bazel:

```bash
# Run all tests
bazel test //cpp/example_meerkat:example_meerkat_test

# Run tests with verbose output
bazel test //cpp/example_meerkat:example_meerkat_test --test_output=all

# Run a specific test case
bazel test //cpp/example_meerkat:example_meerkat_test --test_filter="*UserStoreCreateAndRetrieve*"
```

### Test Coverage

The test suite demonstrates:

**Unit Tests:**
- **UserStore Logic**: Testing CRUD operations, thread safety, and edge cases
- **Route Handler Logic**: Testing individual endpoint logic without server setup
- **JSON Validation**: Testing request parsing and response formatting
- **Error Handling**: Testing various error conditions and status codes

**Integration Tests:**
- **Server Lifecycle**: Testing server startup, shutdown, and polling
- **Middleware Configuration**: Testing middleware registration and execution
- **HTTP Route Registration**: Testing route setup and configuration

**Test Patterns for Meerkat Services:**

1. **Unit Testing Business Logic**: Test your service classes (like `UserStore`) independently of the HTTP server
2. **Handler Logic Testing**: Test route handler functions by simulating `HttpRequest` objects
3. **Server Integration Testing**: Test server lifecycle and configuration
4. **Response Validation**: Test JSON serialization and HTTP status codes
5. **Thread Safety Testing**: Test concurrent access to shared resources

### Example Test Patterns

```cpp
// Unit test for service logic
TEST_F(ExampleMeerkatTest, UserStoreCreateAndRetrieve) {
    json user_data = {{"name", "John"}, {"email", "john@example.com"}};
    json created_user = user_store_->create_user(user_data);
    
    EXPECT_EQ(created_user["name"], "John");
    EXPECT_EQ(created_user["id"], 1);
}

// Handler logic test
TEST_F(ExampleMeerkatTest, CreateUserWithValidData) {
    HttpRequest req;
    req.body = R"({"name": "Test", "email": "test@example.com"})";
    
    // Test the handler logic directly
    json user_data = json::parse(req.body);
    json new_user = user_store_->create_user(user_data);
    auto response = responses::created(new_user);
    
    EXPECT_EQ(response.status_code, 201);
}

// Server integration test
TEST_F(ExampleMeerkatTest, ServerCanStartAndStop) {
    EXPECT_TRUE(server_->listen("127.0.0.1", port_));
    EXPECT_TRUE(server_->is_running());
    server_->stop();
    EXPECT_FALSE(server_->is_running());
}
```

This testing approach allows you to verify your service logic at multiple levels and ensures robust, maintainable Meerkat applications.

This example serves as a starting point for building more complex HTTP services with Meerkat.