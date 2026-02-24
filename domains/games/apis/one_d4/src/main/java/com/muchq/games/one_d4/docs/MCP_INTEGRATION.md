# Chess Game Indexer — MCP Server Integration

## Overview

Expose the indexer's capabilities as MCP (Model Context Protocol) tool calls, so that LLM agents connected to the existing `mcpserver` can index chess games and query them using ChessQL — all through natural language.

## Architecture

There are two integration approaches. Both are viable; choose based on deployment topology.

### Option A: In-Process (Recommended)

Add indexer tool classes directly to the `mcpserver` package. The mcpserver process embeds the indexer engine, DB access, and queue. Uses in-process mode (H2) by default with PostgreSQL as an option.

```
┌──────────────────────────────────────────────────┐
│              mcpserver JVM Process                │
│                                                   │
│  McpRequestHandler                                │
│    └─ ToolRegistry                                │
│         ├─ ChessComGamesTool     (existing)       │
│         ├─ ChessComPlayerTool    (existing)       │
│         ├─ ChessComStatsTool     (existing)       │
│         ├─ ServerTimeTool        (existing)       │
│         ├─ IndexGamesTool        (NEW)            │
│         ├─ IndexStatusTool       (NEW)            │
│         ├─ QueryGamesTool        (NEW)            │
│         └─ AnalyzePositionTool   (NEW)            │
│                  │                                │
│         ┌────────▼─────────┐                      │
│         │ IndexerFacade    │                       │
│         │  (thin wrapper)  │                       │
│         └────────┬─────────┘                      │
│                  │                                │
│    ┌─────────────▼──────────────────┐             │
│    │  Indexer Engine + H2/Postgres  │             │
│    └────────────────────────────────┘             │
└──────────────────────────────────────────────────┘
```

**Pros**: Single process, no network hops, in-process mode works out of the box, simplest deployment.
**Cons**: Larger binary, indexer lifecycle coupled to mcpserver.

### Option B: HTTP Proxy

MCP tools call the indexer's REST API over HTTP. The indexer runs as a separate process.

```
┌────────────────────┐       HTTP        ┌──────────────────┐
│    mcpserver       │ ────────────────► │    indexer        │
│  IndexGamesTool ───┤  POST /v1/index  │  IndexController  │
│  QueryGamesTool ───┤  POST /v1/query  │  QueryController  │
└────────────────────┘                   └──────────────────┘
```

**Pros**: Independent scaling, separate deploys, clear service boundary.
**Cons**: Network latency, two processes to manage, need indexer running.

---

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
      "enum": ["chess.com"],
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
      "type": "integer",
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

## Implementation: Option A (In-Process)

### New Files

All under `domains/games/apis/mcpserver/src/main/java/com/muchq/games/mcpserver/tools/`:

#### IndexerFacade.java

Thin wrapper that manages indexer components without exposing internal types to MCP tools:

```java
public class IndexerFacade {
    private final IndexingRequestDao requestDao;
    private final GameFeatureDao gameFeatureDao;
    private final IndexQueue queue;
    private final IndexWorker worker;
    private final FeatureExtractor featureExtractor;
    private final SqlCompiler sqlCompiler;

    // Synchronous index (blocks until complete — suitable for small ranges)
    public IndexResult indexSync(String player, String platform, String startMonth, String endMonth) { ... }

    // Async index (enqueues and returns immediately)
    public IndexResult indexAsync(String player, String platform, String startMonth, String endMonth) { ... }

    // Poll status
    public IndexResult getStatus(UUID requestId) { ... }

    // Query
    public QueryResult query(String chessql, int limit) { ... }

    // Analyze a single PGN
    public AnalysisResult analyze(String pgn) { ... }
}
```

#### IndexGamesTool.java

```java
public class IndexGamesTool implements McpTool {
    private final IndexerFacade facade;
    private final ObjectMapper mapper;

    @Override
    public String getName() { return "index_chess_games"; }

    @Override
    public String getDescription() {
        return "Index a chess player's games for tactical motif detection. "
             + "Fetches games from chess.com, replays positions, and detects "
             + "pins, forks, skewers, discovered attacks, cross-pins, checks, "
             + "checkmates, and promotions (including promotion with check/checkmate). "
             + "Returns a request ID to check status with index_status.";
    }

    @Override
    public Map<String, Object> getInputSchema() {
        return Map.of(
            "type", "object",
            "properties", Map.of(
                "username", Map.of("type", "string", "description", "Chess platform username"),
                "platform", Map.of("type", "string", "enum", List.of("chess.com"),
                                   "description", "Chess platform"),
                "start_month", Map.of("type", "string",
                                      "description", "Start month (YYYY-MM format)"),
                "end_month", Map.of("type", "string",
                                    "description", "End month (YYYY-MM format)")
            ),
            "required", List.of("username", "platform", "start_month", "end_month")
        );
    }

    @Override
    public String execute(Map<String, Object> arguments) {
        String username = (String) arguments.get("username");
        String platform = (String) arguments.get("platform");
        String startMonth = (String) arguments.get("start_month");
        String endMonth = (String) arguments.get("end_month");

        var result = facade.indexAsync(username, platform, startMonth, endMonth);
        return serialize(result);
    }
}
```

#### IndexStatusTool.java, QueryGamesTool.java, AnalyzePositionTool.java

Same pattern as above — implement `McpTool`, delegate to `IndexerFacade`.

### McpModule Changes

Wire the new tools into the tool registry:

```java
@Context
public List<McpTool> mcpTools(
        Clock clock,
        ChessClient chessClient,
        ObjectMapper objectMapper,
        IndexerFacade indexerFacade) {
    return List.of(
        new ChessComGamesTool(chessClient, objectMapper),
        new ChessComPlayerTool(chessClient, objectMapper),
        new ChessComStatsTool(chessClient, objectMapper),
        new ServerTimeTool(clock),
        new IndexGamesTool(indexerFacade, objectMapper),       // NEW
        new IndexStatusTool(indexerFacade, objectMapper),      // NEW
        new QueryGamesTool(indexerFacade, objectMapper),       // NEW
        new AnalyzePositionTool(indexerFacade, objectMapper)   // NEW
    );
}

@Context
public IndexerFacade indexerFacade(...) {
    // Wire indexer components — reuses existing engine, DB, queue code
}
```

### BUILD.bazel Changes

Add indexer libraries as deps to `mcpserver/tools/BUILD.bazel`:

```bzl
deps = [
    # existing deps...
    "//domains/games/apis/one_d4/chessql/compiler",
    "//domains/games/apis/one_d4/chessql/parser",
    "//domains/games/apis/one_d4/db",
    "//domains/games/apis/one_d4/engine",
    "//domains/games/apis/one_d4/engine/model",
    "//domains/games/apis/one_d4/queue",
    "//domains/games/apis/one_d4/worker",
],
```

And H2 as a runtime dep for in-process mode on `mcpserver/BUILD.bazel`.

---

## Implementation: Option B (HTTP Proxy)

### New Files

Same tool classes, but `IndexerFacade` calls the indexer REST API instead:

```java
public class IndexerHttpClient {
    private final HttpClient httpClient;
    private final String baseUrl; // e.g. http://localhost:8080

    public IndexResult index(String player, String platform, String startMonth, String endMonth) {
        // POST http://localhost:8080/v1/index
        // Body: {"player":"...","platform":"...","startMonth":"...","endMonth":"..."}
    }

    public IndexResult getStatus(UUID requestId) {
        // GET http://localhost:8080/v1/index/{id}
    }

    public QueryResult query(String chessql, int limit) {
        // POST http://localhost:8080/v1/query
        // Body: {"query":"...","limit":N,"offset":0}
    }
}
```

This reuses the existing `HttpClient` abstraction (`domains.platform.http_client.core`).

No `analyze_position` over HTTP unless we add a dedicated endpoint to the indexer API. Alternatively, embed just the engine (no DB) in the mcpserver for analysis.

---

## Sync vs Async Indexing in MCP Context

MCP tool calls are synchronous — the LLM waits for a response. Indexing a month of games can involve hundreds of API calls to chess.com.

### Strategy: Hybrid

1. **Small requests (1 month)**: Run synchronously. The tool blocks until indexing is complete. Most months have < 500 games, completing in 30-60 seconds. The LLM can present results immediately.

2. **Large requests (multi-month)**: Run asynchronously. Return the request ID immediately. The LLM then polls with `index_status` in a follow-up turn.

```java
public String execute(Map<String, Object> arguments) {
    // ...
    YearMonth start = YearMonth.parse(startMonth);
    YearMonth end = YearMonth.parse(endMonth);
    long monthSpan = start.until(end, java.time.temporal.ChronoUnit.MONTHS) + 1;

    if (monthSpan <= 1) {
        // Synchronous — block and return full result
        var result = facade.indexSync(username, platform, startMonth, endMonth);
        return serialize(result);
    } else {
        // Async — enqueue and return ID
        var result = facade.indexAsync(username, platform, startMonth, endMonth);
        return serialize(result) + "\n\nNote: Multi-month indexing runs in the background. "
             + "Use index_status to check progress.";
    }
}
```

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

## Files Summary

### Option A (In-Process)

| File | Action | Description |
|------|--------|-------------|
| `mcpserver/tools/IndexerFacade.java` | Create | Thin wrapper over indexer components |
| `mcpserver/tools/IndexGamesTool.java` | Create | `index_chess_games` MCP tool |
| `mcpserver/tools/IndexStatusTool.java` | Create | `index_status` MCP tool |
| `mcpserver/tools/QueryGamesTool.java` | Create | `query_chess_games` MCP tool |
| `mcpserver/tools/AnalyzePositionTool.java` | Create | `analyze_position` MCP tool |
| `mcpserver/tools/BUILD.bazel` | Modify | Add indexer library deps |
| `mcpserver/McpModule.java` | Modify | Wire IndexerFacade + new tools |
| `mcpserver/BUILD.bazel` | Modify | Add indexer + H2 deps |

### Option B (HTTP Proxy)

| File | Action | Description |
|------|--------|-------------|
| `mcpserver/tools/IndexerHttpClient.java` | Create | HTTP client to indexer API |
| `mcpserver/tools/IndexGamesTool.java` | Create | Delegates to IndexerHttpClient |
| `mcpserver/tools/IndexStatusTool.java` | Create | Delegates to IndexerHttpClient |
| `mcpserver/tools/QueryGamesTool.java` | Create | Delegates to IndexerHttpClient |
| `mcpserver/tools/BUILD.bazel` | Modify | Add http_client dep |
| `mcpserver/McpModule.java` | Modify | Wire IndexerHttpClient + new tools |

Option A is recommended for initial implementation. It requires no separate process, works with in-process mode, and can be split into Option B later if scaling demands it.
