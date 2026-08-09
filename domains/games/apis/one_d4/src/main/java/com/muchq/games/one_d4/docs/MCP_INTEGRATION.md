# Chess Game Indexer — MCP Server Integration

> **Status: implemented.** `mcpserver` serves `index_chess_games`, `index_status`,
> `query_chess_games`, `analyze_position` and `aggregate_chess_games` as `@Tool` methods on
> `@Singleton` beans, over MCP via micronaut-mcp; their parameters are their input schemas.
> Each one is an HTTP call to this service — see the Architecture section below. Code is in
> `mcpserver/tools/`, transport in `mcpserver/README.md`. The tool semantics below describe what
> the tools do; the served schemas are derived from the method signatures and carry no `enum` or
> `items` constraints, and `McpProtocolTest` is authoritative for their exact shape.

## Overview

Expose the indexer's capabilities as MCP (Model Context Protocol) tool calls, so that LLM agents connected to the existing `mcpserver` can index chess games and query them using ChessQL — all through natural language.

## Architecture

`mcpserver` is an HTTP adapter. It holds no database credentials and runs no indexer; every
corpus-backed tool is a call to this service over the internal network (#1332).

```
┌──────────────────┐          ┌───────────────────────────────┐
│  mcpserver JVM   │          │        one_d4 JVM             │
│                  │          │                               │
│  MCP tools       │  HTTP    │  /v1/index    /v1/query       │
│      │           │ ───────► │  /v1/index/{id}               │
│  IndexerFacade   │          │  /v1/aggregate  /v1/analyze   │
│  (HTTP client)   │          │            │                  │
└──────────────────┘          │  Indexer engine + PostgreSQL  │
                              └───────────────────────────────┘
```

One corpus, one owner. `one_d4` keeps validation, the indexing lifecycle, retention, query limits,
the schema and its migrations; a second process with a connection string would own a copy of all of
that by accident, and the copies drift. It also means indexing through MCP reaches the same corpus
`1d4.net` serves, rather than a private index nobody else can see.

`ONE_D4_BASE_URL` configures the upstream; Compose sets it to the internal service name. These
paths are not routed publicly through Caddy and do not need to be.

**Indexing is asynchronous upstream.** `POST /v1/index` returns immediately. The adapter polls
`GET /v1/index/{id}` for single-month requests so `index_chess_games` still answers with a final
status in one tool call, under an explicit timeout; longer ranges come back `PENDING` and are
followed with `index_status`. Running out of polling budget is not an error — the caller gets the
last status seen and a request id that still works.

## Tool Definitions

### 1. `index_chess_games`

Start indexing a player's games for motif detection.

**Input Schema**:
```json
{
  "type": "object",
  "properties": {
    "username": {
      "type": "string",
      "description": "The player's username on the chess platform"
    },
    "platform": {
      "type": "string",
      "description": "The chess platform (currently only chess.com)"
    },
    "start_month": {
      "type": "string",
      "description": "Start month in YYYY-MM format (e.g. 2024-03)"
    },
    "end_month": {
      "type": "string",
      "description": "End month in YYYY-MM format (e.g. 2024-03)"
    }
  },
  "required": ["username", "platform", "start_month", "end_month"]
}
```

**Output**: JSON with request ID and initial status.

```
{"id": "abc-123", "status": "PENDING", "gamesIndexed": 0}
```

**Example LLM Interaction**:
> User: "Index hikaru's games from March 2024"
> LLM calls `index_chess_games(username="hikaru", platform="chess.com", start_month="2024-03", end_month="2024-03")`

### 2. `index_status`

Check the status of an indexing request.

**Input Schema**:
```json
{
  "type": "object",
  "properties": {
    "request_id": {
      "type": "string",
      "description": "The UUID of the indexing request"
    }
  },
  "required": ["request_id"]
}
```

**Output**: JSON with current status, game count, and any error.

```
{"id": "abc-123", "status": "COMPLETED", "gamesIndexed": 147, "errorMessage": null}
```

**Example LLM Interaction**:
> User: "Is the indexing done yet?"
> LLM calls `index_status(request_id="abc-123")`
> LLM: "Yes, indexing is complete. 147 games were indexed."

### 3. `query_chess_games`

Search indexed games using ChessQL.

**Input Schema**:
```json
{
  "type": "object",
  "properties": {
    "query": {
      "type": "string",
      "description": "A ChessQL query string. Examples: 'white.elo >= 2500 AND motif(fork)', 'motif(pin) OR motif(skewer)', 'eco = \"B90\" AND NOT motif(fork)'. Available motifs: pin, cross_pin, fork, skewer, discovered_attack, check, checkmate, promotion, promotion_with_check, promotion_with_checkmate. Available fields: white.elo, black.elo, white.username, black.username, time.class, eco, result, num.moves, platform."
    },
    "limit": {
      "type": "number",
      "description": "Maximum number of results to return (default 10, max 50)"
    }
  },
  "required": ["query"]
}
```

**Output**: JSON array of matching games with key fields.

```
{"games": [{"gameUrl": "...", "whiteUsername": "hikaru", "whiteElo": 2850, ...}], "count": 3}
```

**Example LLM Interactions**:
> User: "Find hikaru's games where he played a fork as white"
> LLM calls `query_chess_games(query="white.username = \"hikaru\" AND motif(fork)", limit=10)`

> User: "Show me games with both pins and forks where someone was rated over 2500"
> LLM calls `query_chess_games(query="motif(pin) AND motif(fork) AND (white.elo >= 2500 OR black.elo >= 2500)")`

> User: "Any Sicilian Najdorf games with discovered attacks?"
> LLM calls `query_chess_games(query="eco = \"B90\" AND motif(discovered_attack)")`

### 4. `analyze_position`

Detect motifs in a single PGN without indexing it to the database.

**Input Schema**:
```json
{
  "type": "object",
  "properties": {
    "pgn": {
      "type": "string",
      "description": "A PGN string of the chess game to analyze"
    }
  },
  "required": ["pgn"]
}
```

**Output**: JSON with detected motifs and occurrence details.

```
{
  "numMoves": 42,
  "motifs": ["PIN", "FORK"],
  "occurrences": {
    "PIN": [{"moveNumber": 15, "description": "Pin detected at move 15"}],
    "FORK": [{"moveNumber": 23, "description": "Fork detected at move 23"}]
  }
}
```

**Example LLM Interaction**:
> User: "Analyze this game for tactics: [Event \"Live\"] ... 1. e4 e5 ..."
> LLM calls `analyze_position(pgn="[Event \"Live\"] ...")`
> LLM: "I found two tactical motifs: a pin at move 15 and a knight fork at move 23."

---

## Sync vs Async Indexing in MCP Context

MCP tool calls are synchronous — the LLM waits for a response. Indexing a month of games can involve hundreds of API calls to chess.com.

### Strategy: Hybrid

1. **Small requests (1 month)**: Run synchronously. The tool blocks until indexing is complete. Most months have < 500 games, completing in 30-60 seconds. The LLM can present results immediately.

2. **Large requests (multi-month)**: Run asynchronously. Return the request ID immediately. The LLM then polls with `index_status` in a follow-up turn.

`IndexerFacade` makes that call. `POST /v1/index` is asynchronous for both cases, so the single-
month path is bounded polling of `GET /v1/index/{id}` inside the adapter — the tool still answers
COMPLETED in one call. A longer range returns PENDING with a request id, and so does a single month
that outruns the polling budget, which keeps a slow month from costing the caller their request id.

---

## Conversation Flow Examples

### Example 1: Index and Query

```
User: Index hikaru's blitz games from January 2024 and find games with forks

LLM: I'll index hikaru's games first, then search for forks.

[Tool call: index_chess_games(username="hikaru", platform="chess.com",
                               start_month="2024-01", end_month="2024-01")]
→ {"id": "abc-123", "status": "COMPLETED", "gamesIndexed": 312}

[Tool call: query_chess_games(query="white.username = \"hikaru\" AND motif(fork)",
                               limit=10)]
→ {"games": [...], "count": 7}

LLM: I indexed 312 of hikaru's games from January 2024. I found 7 games where
     hikaru played as white and executed a fork. Here are the highlights...
```

### Example 2: Analyze a Specific Game

```
User: Can you analyze this game for tactics?
      1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O ...

[Tool call: analyze_position(pgn="1. e4 e5 2. Nf3 Nc6 ...")]
→ {"numMoves": 35, "motifs": ["PIN", "FORK"],
   "occurrences": {"PIN": [{"moveNumber": 12, ...}], "FORK": [{"moveNumber": 23, ...}]}}

LLM: I found two tactical motifs in this game:
     - A pin at move 12 where...
     - A knight fork at move 23 where...
```

### Example 3: Complex Research Query

```
User: Are there more forks or skewers in high-elo Sicilian games?

LLM: Let me search the indexed games for both patterns.

[Tool call: query_chess_games(query="eco IN [\"B20\",\"B90\",\"B91\",\"B92\"]
            AND (white.elo >= 2500 OR black.elo >= 2500) AND motif(fork)", limit=50)]
→ {"games": [...], "count": 23}

[Tool call: query_chess_games(query="eco IN [\"B20\",\"B90\",\"B91\",\"B92\"]
            AND (white.elo >= 2500 OR black.elo >= 2500) AND motif(skewer)", limit=50)]
→ {"games": [...], "count": 8}

LLM: In the indexed high-elo Sicilian games, forks are nearly 3x more common
     than skewers (23 vs 8 games). This makes sense because...
```

---
