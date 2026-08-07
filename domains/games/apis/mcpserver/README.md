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
  - `chess_com_games` - A player's games for a month, with filters (time_class, color, rated,
    rules, opponent) and projection (pgn/tcn omitted unless requested)
  - `chess_com_player` - A player's profile (including title, if any)
  - `chess_com_players` - Batch profile lookup for up to 50 usernames
  - `chess_com_stats` - A player's rating stats
  - `server_time` - Current UTC time
  - `index_chess_games` - Index a player's games into the in-process indexer (one_d4)
  - `index_status` - Poll an indexing request
  - `query_chess_games` - ChessQL search over indexed games
  - `aggregate_chess_games` - Grouped counts over indexed games ("most popular openings")
  - `analyze_position` - Motif detection for a single PGN without indexing

## Build the Java binary

```bash
bazel build //domains/games/apis/mcpserver:mcpserver
```

## Build the Docker image

```bash
bazel build //domains/games/apis/mcpserver:oci_tarball
```

## Run with Docker

```bash
# Load the image into Docker
bazel run //domains/games/apis/mcpserver:oci_tarball

# Run the container
docker run -p 8080:8080 mcpserver:latest

# Run on custom port
docker run -e PORT=9090 -e APP_NAME=mcp-server -p 9090:9090 mcpserver:latest
```

## Run locally

```bash
bazel run //domains/games/apis/mcpserver:mcpserver
```

## Authentication

The server supports optional Bearer token authentication. Set the `MCP_AUTH_TOKEN` environment variable to enable authentication:

```bash
# Run with authentication
export MCP_AUTH_TOKEN=my-secret-token
bazel run //domains/games/apis/mcpserver:mcpserver

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

### chess.com passthrough tools

- `chess_com_games` — a player's games for a month. Required: `username`, `year` (yyyy),
  `month` (MM). Optional filters: `time_class` (blitz/bullet/rapid/daily), `color`
  (white/black), `rated` (bool), `rules` (default `chess`; a variant name, or `all`),
  `opponent`. Optional projection/paging: `include_pgn` and `include_tcn` (default false),
  `limit` (default 100), `offset`.
- `chess_com_player` — a player's profile, including `title` (GM/IM/...), `location`, and
  `fideRating` when present.
- `chess_com_players` — batch profile lookup: `usernames` (array, max 50). Returns a map keyed
  by lowercased username plus `not_found` and per-username `errors`. Lookups run concurrently on
  a small pool, so fan-out against chess.com stays bounded.
- `chess_com_stats` — a player's rating stats.
- `server_time` — current UTC time.

### Indexer tools (in-process one_d4)

The indexer engine, database, and worker are embedded in this process (Option A in
`one_d4/.../docs/MCP_INTEGRATION.md`). By default the index lives in in-memory H2; set
`INDEXER_DB_URL` to point at a durable database.

**The deployed MCP server takes that default.** `INDEXER_DB_URL` is the only source `McpModule`
reads — there is no `/etc/…/db_config` fallback like `one_d4`'s — and the Compose service sets no
such variable, so its index is in-memory H2, scoped to the process: empty at boot, discarded on
restart, and entirely separate from the `one_d4` service's PostgreSQL corpus. Indexing tools work
here, but each container starts from nothing and re-indexes on demand. Pointing this at the shared
database is a deliberate deployment change (it would write to the production corpus), not a
missing setting to paste in.

- `index_chess_games` — index a player's games. Required: `username`, `platform`
  (`chess.com`), `start_month`/`end_month` (YYYY-MM). Optional: `exclude_bullet`, and
  `skip_cache` to refetch already-indexed months (refreshing stored rows, e.g. backfilling
  titles/opening names on rows indexed before those columns existed) — though it returns the
  in-flight request without refetching if one is already running for the same range.
  Single-month requests
  complete synchronously; longer ranges return `PENDING` and run in the background.
- `index_status` — poll an indexing request by `request_id`.
- `query_chess_games` — ChessQL search over indexed games: `query`, optional `player` (resolves
  perspective fields `me.*`, `opponent.*`, `outcome`), `limit` (default 10, max 50), and
  `include_pgn` (default false). The query can scope time with `date` (ISO comparisons, e.g.
  `date >= "2026-07-01"`) or `month` (`month = "2026-07"`, equality only).
- `aggregate_chess_games` — grouped counts over indexed games: `query`, `group_by` (e.g.
  `["opening_family"]`; with `player` also the perspective fields — rating fields bucket,
  default 100 points, e.g. `opponent.elo(200)`), optional `player` and `limit`. Answers "most
  popular openings" — or "hikaru's results against each GM he faced, both colors pooled" via
  perspective fields — in one call. The output's `count` is the number of groups returned, not
  games; `totalGames`/`totalGroups` cover the untruncated result and `truncated` says the group
  limit cut off a long tail (common with `opening_family`, whose chess.com ECO-URL-derived
  values are not normalized: "Closed Sicilian" and "Closed Sicilian Defense" are distinct
  groups).
- `analyze_position` — detect motifs in a single `pgn` without indexing it.

Both query tools see only what has been indexed, and neither reports which periods those are. A
`date` / `month` filter over a never-indexed period returns an empty result rather than an error,
which reads exactly like "played no games then" — run `index_chess_games` for the period before
concluding anything from an empty date-scoped result.

## Configuration

Environment variables:
- **PORT**: Server port (default: 8080)
- **APP_NAME**: Application name (default: mcp-server)
- **MCP_AUTH_TOKEN**: Bearer token for authentication (optional, no auth if not set)
- **INDEXER_DB_URL**: JDBC URL for the in-process indexer (default: in-memory H2)

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
