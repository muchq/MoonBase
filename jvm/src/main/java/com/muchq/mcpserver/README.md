# MCP Server (Model Context Protocol)

A basic [Model Context Protocol](https://modelcontextprotocol.io) server built with Micronaut and WebSocket. Implements the MCP JSON-RPC protocol to provide tools that AI assistants like Claude can use.

## What is MCP?

The Model Context Protocol (MCP) is an open protocol that enables seamless integration between LLM applications and external data sources and tools. This server implements MCP over WebSocket, allowing AI assistants to discover and execute tools in real-time.

## Features

- **JSON-RPC 2.0 Protocol**: Standard MCP communication over WebSocket
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

## Testing with websocat

```bash
# Install websocat
# brew install websocat  # macOS
# cargo install websocat  # via Rust

# Connect to the MCP server
websocat ws://localhost:8080/mcp

# 1. Initialize the connection
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"test-client","version":"1.0.0"}}}

# 2. List available tools
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}

# 3. Call the echo tool
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{"message":"Hello, MCP!"}}}

# 4. Call the add tool
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"add","arguments":{"a":42,"b":58}}}

# 5. Get timestamp
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"get_timestamp","arguments":{}}}

# 6. Generate random number
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"random","arguments":{"min":1,"max":100}}}
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

- **PORT**: Server port (default: 8080)
- **APP_NAME**: Application name (default: mcp-server)

## WebSocket Endpoint

- **Path**: `/mcp`
- **URL**: `ws://localhost:8080/mcp`

## Resources

- [Model Context Protocol Specification](https://modelcontextprotocol.io/specification/2025-11-25)
- [MCP Java SDK](https://github.com/modelcontextprotocol/java-sdk)
- [Anthropic MCP Introduction](https://www.anthropic.com/news/model-context-protocol)
