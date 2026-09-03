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

### Corpus tools (one_d4 over HTTP)

These are calls to the `one_d4` service, not work this process does. `index_chess_games` is
`POST /v1/index`, `index_status` is `GET /v1/index/{id}`, `query_chess_games` is `POST /v1/query`,
and `aggregate_chess_games` is `POST /v1/aggregate`. The upstream is `ONE_D4_BASE_URL`, which
Compose sets to one_d4's `one-d4` network alias.

`analyze_position` is the exception: it is `POST /v2/analyze` against the C++ `one_d4_v2` service
(#1389 phase 6), configured separately by `ONE_D4_V2_BASE_URL`, because analysis touches no corpus
state and moved out of the Java service first.

**They act on the same corpus the site serves** — indexing through MCP puts games where
`1d4.net` can see them, and a query here sees everything `api.1d4.net` does. That is the point of
routing through the API: `one_d4` owns validation, the indexing lifecycle, retention, query limits,
the schema, and its migrations, and this server holds no database credentials at all (#1332).

`POST /v1/index` is asynchronous. Single-month requests are polled here until they finish, so
`index_chess_games` still answers with a final status in one call; longer ranges come back
`PENDING` immediately and are followed with `index_status`.

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
  default 100 points, e.g. `opponent.elo(200)`), optional `player`, `limit`, `order_by` and
  `min_games`. Answers "most popular openings" — or "hikaru's results against each GM he faced,
  both colors pooled" via perspective fields — in one call. With `player` set, each group also
  carries `wins`/`losses`/`draws` and `score` (W + D/2) for that player, so "how do I score in
  them" is the same call rather than a second `outcome` dimension to pivot back; the four fields
  are absent without a player. `order_by: "score"` ranks by score per game before the limit
  truncates (requires `player`, and wants `min_games` so a one-game sideline does not top the
  list). The output's `count` is the number of groups returned, not games;
  `totalGames`/`totalGroups` cover the untruncated result and `truncated` says the group limit cut
  off a long tail (common with `opening_family`, whose chess.com ECO-URL-derived values are not
  normalized: "Closed Sicilian Defense" and "Alapin Sicilian Defense" are distinct groups, not
  part of "Sicilian Defense").
- `analyze_position` — detect motifs in a single `pgn` without indexing it. Served by
  `one_d4_v2`, not `one_d4`; a 429 from its rate limiter surfaces as an upstream condition,
  not an input error.

Both query tools see only what has been indexed, and neither reports which periods those are. A
`date` / `month` filter over a never-indexed period returns an empty result rather than an error,
which reads exactly like "played no games then" — run `index_chess_games` for the period before
concluding anything from an empty date-scoped result.

## Resources

- `chessql://reference` (`text/markdown`) — one_d4's `CHESSQL.md`, served verbatim: the EBNF
  grammar, operator precedence, the field and motif rosters, perspective fields, date scoping and
  NULL semantics.

A resource, not an eleventh tool (#1326). A tool is a call the model chooses to make; a resource is
context a client attaches up front, and making a model spend a `tools/call` round trip to learn
query syntax *before* it can write a query is the wrong shape. Clients are not required to read
resources, so `query_chess_games`' description keeps the full vocabulary and a resource-blind
client loses the grammar, precedence and NULL semantics — not the field, motif or perspective
rosters.

All three copies of that vocabulary — the compiler, the doc, and the tool description — are pinned
against each other, in both directions:

- `ChessQlReferenceTest` (one_d4) — CHESSQL.md's tables vs `SqlCompiler`.
- `McpToolVocabularyTest` (here) — `query_chess_games`' served description vs `SqlCompiler`.
- `SqlCompilerTest` — every advertised name actually compiles, and an unadvertised one is
  rejected. Without this the other two pin the doc to a restatement of the accept rule rather than
  to the rule.

That is what makes serving the file verbatim safe rather than a third place to drift. It is not a
theoretical risk: the tool description had already drifted twice, missing `zugzwang` and
`overloaded_piece` from the motifs and `game.url` and `played.at` from the fields.

## Configuration

Environment variables:
- **PORT**: Server port (default: 8080)
- **APP_NAME**: Application name (default: `helloworld`, from the shared `application.yml`; Compose sets `mcpserver`)
- **MCP_AUTH_TOKEN**: Bearer token for authentication (optional, open if unset or empty)
- **MCP_SERVER_VERSION**: Version reported in `initialize`'s `serverInfo` (default: 1.0.0)
- **ONE_D4_BASE_URL**: Base URL of the one_d4 API the corpus tools call (default:
  `http://one-d4:8080`). The host must be one `java.net.URI` can parse, which rules
  out the `one_d4` service key — an underscore in an authority yields a null host, so
  no request built from it can be sent. one_d4 publishes `one-d4` as a network alias
  for this.
- **ONE_D4_V2_BASE_URL**: Base URL of the one_d4_v2 API `analyze_position` calls (default:
  `http://one-d4-v2:8090`). Hyphenated for the same `java.net.URI` reason; one_d4_v2
  publishes `one-d4-v2` as its alias.

## Logging

Every tool call logs one line on the `com.muchq.games.mcpserver.tools.ToolCallLog`
logger: message `tool_call`, with `tool`, `ms`, and `outcome` as key-value pairs
(the `kvpList` of the JSON line, the shape the stats pipeline reads): INFO for a success, WARN
when the tool answered `isError: true`, ERROR with the stack when an exception
escaped the tool (the framework's JSON-RPC error mapping still applies).
Arguments are not logged. A call the framework rejects before the tool method
runs — a missing required argument, an unbindable type — leaves no line on this
logger; the SDK's `ToolInputValidator` logs its own WARN for those.

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
