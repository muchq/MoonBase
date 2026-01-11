# MCP Server (Model Context Protocol)

A basic [Model Context Protocol](https://modelcontextprotocol.io) server built with Micronaut and HTTP. Implements the MCP JSON-RPC protocol to provide tools that AI assistants like Claude can use.

## What is MCP?

The Model Context Protocol (MCP) is an open protocol that enables seamless integration between LLM applications and external data sources and tools. This server implements MCP over HTTP, allowing AI assistants to discover and execute tools remotely.

## Features

- **JSON-RPC 2.0 Protocol**: Standard MCP communication over HTTP
- **Bearer Token Authentication**: Optional authentication for secure remote access
- **Tool Discovery**: Clients can list all available tools via `tools/list`
- **Tool Execution**: Execute tools remotely via `tools/call`
- **Built-in Tools**:
  - `echo` - Echoes back a message
  - `add` - Adds two numbers
  - `get_timestamp` - Returns current UTC timestamp
  - `random` - Generates random number between min and max

## Build the Java binary

```bash
bazel build //jvm/src/main/java/com/muchq/mcpserver:mcpserver
```

## Build the Docker image

```bash
bazel build //jvm/src/main/java/com/muchq/mcpserver:oci_tarball
```

## Run with Docker

```bash
# Load the image into Docker
bazel run //jvm/src/main/java/com/muchq/mcpserver:oci_tarball

# Run the container
docker run -p 8080:8080 mcpserver:latest

# Run on custom port
docker run -e PORT=9090 -e APP_NAME=mcp-server -p 9090:9090 mcpserver:latest
```

## Run locally

```bash
bazel run //jvm/src/main/java/com/muchq/mcpserver:mcpserver
```

## Authentication

The server supports optional Bearer token authentication. Set the `MCP_AUTH_TOKEN` environment variable to enable authentication:

```bash
# Run with authentication
export MCP_AUTH_TOKEN=my-secret-token
bazel run //jvm/src/main/java/com/muchq/mcpserver:mcpserver

# Or with Docker
docker run -e MCP_AUTH_TOKEN=my-secret-token -p 8080:8080 mcpserver:latest
```

If `MCP_AUTH_TOKEN` is not set, the server runs without authentication (suitable for local development).

## Testing with curl

```bash
# Set auth token (optional - only needed if MCP_AUTH_TOKEN is configured)
export MCP_AUTH_TOKEN=my-secret-token

# 1. Initialize the connection
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $MCP_AUTH_TOKEN" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}}'

# 2. List available tools
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $MCP_AUTH_TOKEN" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'

# 3. Call the echo tool
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $MCP_AUTH_TOKEN" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"message":"Hello, MCP!"}}}'

# 4. Call the add tool
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $MCP_AUTH_TOKEN" \
  -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"add","arguments":{"a":42,"b":58}}}'

# 5. Get timestamp
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $MCP_AUTH_TOKEN" \
  -d '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"get_timestamp","arguments":{}}}'

# 6. Generate random number
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $MCP_AUTH_TOKEN" \
  -d '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"random","arguments":{"min":1,"max":100}}}'

# Without authentication (if MCP_AUTH_TOKEN not set on server)
curl -X POST http://localhost:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
```

## MCP Protocol Reference

### Initialize

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "initialize",
  "params": {
    "protocolVersion": "2024-11-05",
    "capabilities": {},
    "clientInfo": {
      "name": "client-name",
      "version": "1.0.0"
    }
  }
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": {
      "tools": {
        "listChanged": true
      }
    },
    "serverInfo": {
      "name": "micronaut-mcp-server",
      "version": "1.0.0"
    }
  }
}
```

### List Tools

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/list",
  "params": {}
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "tools": [
      {
        "name": "echo",
        "description": "Echoes back the provided message",
        "inputSchema": {
          "type": "object",
          "properties": {
            "message": {
              "type": "string",
              "description": "The message to echo"
            }
          },
          "required": ["message"]
        }
      }
    ]
  }
}
```

### Call Tool

**Request:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "tools/call",
  "params": {
    "name": "echo",
    "arguments": {
      "message": "Hello, World!"
    }
  }
}
```

**Response:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "Echo: Hello, World!"
      }
    ]
  }
}
```

## Available Tools

### echo
Echoes back the provided message.

**Arguments:**
- `message` (string, required): The message to echo

**Example:**
```json
{"name": "echo", "arguments": {"message": "Hello!"}}
```

### add
Adds two numbers together.

**Arguments:**
- `a` (number, required): First number
- `b` (number, required): Second number

**Example:**
```json
{"name": "add", "arguments": {"a": 10, "b": 5}}
```

### get_timestamp
Returns the current UTC timestamp.

**Arguments:** None

**Example:**
```json
{"name": "get_timestamp", "arguments": {}}
```

### random
Generates a random number between min and max (inclusive).

**Arguments:**
- `min` (integer, required): Minimum value
- `max` (integer, required): Maximum value

**Example:**
```json
{"name": "random", "arguments": {"min": 1, "max": 100}}
```

## Configuration

Environment variables:
- **PORT**: Server port (default: 8080)
- **APP_NAME**: Application name (default: mcp-server)
- **MCP_AUTH_TOKEN**: Bearer token for authentication (optional, no auth if not set)

## HTTP Endpoint

- **Method**: POST
- **Path**: `/mcp`
- **URL**: `http://localhost:8080/mcp`
- **Content-Type**: `application/json`
- **Authentication**: `Authorization: Bearer <token>` (optional)

## Resources

- [Model Context Protocol Specification](https://modelcontextprotocol.io/specification/2025-11-25)
- [MCP Java SDK](https://github.com/modelcontextprotocol/java-sdk)
- [Anthropic MCP Introduction](https://www.anthropic.com/news/model-context-protocol)
