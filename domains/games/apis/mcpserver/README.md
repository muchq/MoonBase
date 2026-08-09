# MCP Server (Model Context Protocol)

1d4's chess indexer and chess.com lookups, exposed as ten
[Model Context Protocol](https://modelcontextprotocol.io) tools. Deployed at
`https://mcp.1d4.net/mcp`.

## The transport is not ours

The protocol comes from
[micronaut-mcp](https://micronaut-projects.github.io/micronaut-mcp/latest/guide/)
(`io.micronaut.mcp:micronaut-mcp-server-java-sdk`), configured with
`micronaut.mcp.server.transport=HTTP` — **Streamable HTTP**, MCP's current remote
transport. `initialize`, `notifications/initialized`, `tools/list` and `tools/call`
are the framework's, as is the JSON-RPC framing and the derivation of
`ServerCapabilities` from the declared primitives. Nothing in this package implements
any of it (#1325).

A tool is a `@Tool`-annotated method on a `@Singleton` bean. Its **parameters are its
input schema**: property names come from the parameter (or `@ToolArg(name = ...)` for
the snake_case ones), descriptions from `@ToolArg(description = ...)`, and a parameter
is required unless it is `@Nullable`. There is no hand-written schema to fall out of
step with the signature, and no registry to remember to add the tool to.

Two things the transport does not do, both deliberate:

- **No server→client SSE stream.** micronaut-mcp does not implement that leg, so
  `GET /mcp` answers `405`. Streamable HTTP makes it optional and every tool here is
  request/response, so nothing needs it today. `index_chess_games` is the one that
  would benefit — streamed progress instead of polling `index_status` — if the
  library grows it.
- **No sessions.** The server is stateless, so there is no `Mcp-Session-Id`. The
  2026-07-28 revision removes session pinning from the protocol altogether, so this is
  the direction of travel rather than a shortfall.

### Versions

micronaut-mcp **2.0.0**, on Micronaut **5.1.10** (`bazel/java.MODULE.bazel`).

The server negotiates protocol revisions up to **2025-11-25** — a client that asks for
an older revision it speaks is answered there, and one that asks for something newer is
answered with that ceiling. MCP's current revision is
[2026-07-28](https://modelcontextprotocol.io/specification/versioning), which drops the
handshake and `Mcp-Session-Id` entirely; no Java SDK implements it yet.
`McpProtocolTest` pins the ceiling, so a library bump that raises it fails the build
rather than changing what clients negotiate unnoticed.

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

## Connecting a client

Streamable HTTP, so a client points straight at it:

```bash
claude mcp add --transport http 1d4 https://mcp.1d4.net/mcp
```

No key, no bridge.

## Authentication

Optional bearer token, off unless `MCP_AUTH_TOKEN` is set to a non-empty value. The
shared `application.yml` always defines `mcp.auth.token` (defaulting to empty), so it
is the emptiness, not the absence, that leaves the endpoint open — which is what the
deployment runs. `McpAuthenticationTest` pins both states.

```bash
# Run with authentication
export MCP_AUTH_TOKEN=my-secret-token
bazel run //domains/games/apis/mcpserver:mcpserver

# Or with Docker
docker run -e MCP_AUTH_TOKEN=my-secret-token -p 8080:8080 mcpserver:latest
```

## Testing with curl

It is JSON-RPC 2.0 over HTTP POST, so every method is also one `curl`. Against a
local server on 8080:

```bash
# 1. Initialize
curl -s http://localhost:8080/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"curl","version":"1.0.0"}}}'

# 2. Complete the handshake. A notification carries no id and draws no response body,
#    so this answers 202 Accepted with nothing in it.
curl -s -i http://localhost:8080/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","method":"notifications/initialized"}'

# 3. List the tools, each with the input schema derived from its method signature
curl -s http://localhost:8080/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'

# 4. Call one
curl -s http://localhost:8080/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"chess_com_stats","arguments":{"username":"hikaru"}}}'

# With authentication enabled, add: -H "Authorization: Bearer $MCP_AUTH_TOKEN"
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
- **APP_NAME**: Application name (default: `helloworld`, from the shared `application.yml`; Compose sets `mcpserver`)
- **MCP_AUTH_TOKEN**: Bearer token for authentication (optional, open if unset or empty)
- **MCP_SERVER_VERSION**: Version reported in `initialize`'s `serverInfo` (default: 1.0.0)
- **INDEXER_DB_URL**: JDBC URL for the in-process indexer (default: in-memory H2)

The MCP transport itself is configured in the shared `application.yml`
(`domains/platform/resources`) under `micronaut.mcp.server`: `transport: HTTP`,
`endpoint: /mcp`, and `info.name` / `info.version`. `transport` has no usable default —
micronaut-mcp gates its whole server configuration on that property being present, so
removing it disables the endpoint rather than falling back.

## HTTP Endpoint

- **Path**: `/mcp` (`micronaut.mcp.server.endpoint`; Caddy routes `mcp.1d4.net/mcp` here)
- **POST**: JSON-RPC 2.0 requests and notifications. `Content-Type: application/json`
- **GET**: `405` — the optional server→client SSE stream is not implemented
- **Authentication**: `Authorization: Bearer <token>`, only when a token is configured
- **CORS**: `https://1d4.net` is allowed at the Caddy layer, so 1d4.net's `/mcp` page
  can call the endpoint from a browser

## Resources

- [Model Context Protocol Specification](https://modelcontextprotocol.io/specification/2025-11-25)
- [Streamable HTTP transport](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports#streamable-http)
- [micronaut-mcp guide](https://micronaut-projects.github.io/micronaut-mcp/latest/guide/)
- [MCP Java SDK](https://github.com/modelcontextprotocol/java-sdk)
