# Chess Game Indexer — In-Process Mode

## Quick Start

```bash
# Start the service (H2 in-memory, no external dependencies)
INDEXER_DB_URL="jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1" bazel run //domains/games/apis/one_d4:one_d4

# Index a player's games
curl -X POST http://localhost:8080/v1/index \
  -H 'Content-Type: application/json' \
  -d '{"player":"hikaru","platform":"CHESS_COM","startMonth":"2026-01","endMonth":"2026-01"}'

# Check indexing status (replace {id} with the returned ID)
curl http://localhost:8080/v1/index/{id}

# Query indexed games using ChessQL
curl -X POST http://localhost:8080/v1/query \
  -H 'Content-Type: application/json' \
  -d '{"query":"white.elo > 2500","limit":10,"offset":0}'

# Query with motif detection
curl -X POST http://localhost:8080/v1/query \
  -H 'Content-Type: application/json' \
  -d '{"query":"motif(fork) AND motif(pin)","limit":10,"offset":0}'
```

## Overview

**In-process mode is the default.** The indexer runs with no external dependencies — no PostgreSQL, no SQS, no S3. Everything lives in-process against an H2 in-memory database, which is also the work queue —
the in-memory `IndexQueue` survives only as a wake-up nudge.

This mode is useful for:
- Local development without Docker or PostgreSQL installed
- Unit and integration testing without test containers
- CLI tooling (index a player, query results, exit)
- Demos and evaluations
- CI pipelines

To use PostgreSQL instead, set the `INDEXER_DB_URL` environment variable to a PostgreSQL JDBC URL (e.g., `jdbc:postgresql://localhost:5432/indexer`).

## Architecture

```
┌──────────────────────────────────────────────┐
│                  JVM Process                  │
│                                               │
│  ┌───────────┐   ┌──────────────────────┐    │
│  │ HTTP API  │   │  InMemoryIndexQueue   │    │
│  │ /v1/index ├──►│  (wake-up nudge only)  │    │
│  │ /v1/query │   └──────────┬───────────┘    │
│  └─────┬─────┘              ╎ (nudge)        │
│        │              ┌─────▼──────┐         │
│        │              │IndexWorker │◄────────┼─ claims from
│        │              └─────┬──────┘         │  indexing_requests
│        │                    │                │
│        │                    │                │
│        │    ┌───────────────▼──────────────┐ │
│        └───►│       H2 (in-memory)         │ │
│             │  indexing_requests            │ │
│             │  game_features               │ │
│             └──────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

Zero external processes. Start the JAR, use it, stop it. Data lives only for the lifetime of the process.

## Configuration

### Wiring

There is no mode switch. H2 is not selected — it is what the resolution chain lands
on when nothing else is configured:

```java
@Factory
public class IndexerModule {

    @Context
    public DataSource dataSource(@Value("${indexer.db.url:}") String configuredUrl) {
        return DataSourceFactory.create(
                jdbcUrl(configuredUrl),
                System.getenv("INDEXER_DB_USERNAME"),
                System.getenv("INDEXER_DB_PASSWORD"));
    }

    // Queue is already InMemoryIndexQueue by default — no change needed
}
```

`indexer.db.url` is a Micronaut property tests set to give each ApplicationContext
its own database; nothing sets it in production. When it is blank, `readJdbcUrl()`
resolves `$INDEXER_DB_URL`, then H2 in-memory. (A `/etc/one_d4/db_config` host file
sat between those two until MoonBase#1362.)

Credentials are separate environment variables rather than Micronaut properties or
URL query parameters — pgjdbc decodes query values, so a password containing `+`,
`&` or `%` would be corrupted in the URL. Unset leaves whatever credentials the URL
itself carries, which is what keeps H2 and credential-bearing URLs (Neon's, for
instance) working unchanged.

### Environment variables

H2 in-memory is the default; a PostgreSQL URL is what moves off it:

| Variable              | Default Value                            | Effect                                      |
|-----------------------|------------------------------------------|---------------------------------------------|
| `INDEXER_DB_URL`      | `jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1`  | H2 in-memory (default). No external deps.   |
| `INDEXER_DB_URL`      | `jdbc:postgresql://localhost:5432/...`   | PostgreSQL mode. Requires external database.|
| `INDEXER_DB_USERNAME` | (unset)                                  | Database username. Unset leaves whatever the URL carries. |
| `INDEXER_DB_PASSWORD` | (unset)                                  | Database password. Passed as a connection property, so no URL-encoding constraint. |

The system auto-detects H2 vs PostgreSQL from the JDBC URL and uses the appropriate SQL dialect.

## Operational Characteristics

### What Works

- All API endpoints (`POST /v1/index`, `GET /v1/index/{id}`, `POST /v1/query`)
- Full ChessQL query support
- Full motif detection pipeline
- chess.com API fetching (still makes real HTTP calls)
- Concurrent indexing requests, claimed from `indexing_requests` by the in-process worker

### What Doesn't Persist

- All data is lost when the process exits
- No crash recovery — interrupted indexing jobs are gone
- No way to share data between instances

### Performance Profile

| Metric                  | In-Process (H2) | PostgreSQL         |
|-------------------------|------------------|--------------------|
| Insert throughput       | ~50K rows/sec    | ~5-10K rows/sec    |
| Simple query latency    | < 1ms            | 1-5ms              |
| Complex query latency   | 1-5ms            | 5-50ms             |
| Memory per 10K games    | ~50MB heap       | ~0 (on disk)       |
| Max practical dataset   | ~100K games      | Millions           |
| Startup time            | ~2s              | ~3s (with migration)|

H2 in-memory is significantly faster for small datasets because there's no network round-trip or disk I/O. The bottleneck shifts entirely to the chess.com API fetch and PGN replay.

### Memory Sizing

```
Base JVM overhead:     ~100MB
H2 per 10K games:     ~50MB
Chariot replay state:  ~20MB (per concurrent replay)
Queue overhead:        Negligible

Recommended heap:
  -Xmx512m for < 50K games
  -Xmx1g   for < 100K games
  -Xmx2g   for < 200K games (pushing H2 limits)
```

Beyond ~200K games, switch to PostgreSQL mode. H2 in-memory keeps the entire dataset in the Java heap, and GC pressure becomes the dominant cost.

## CLI Mode (Future Extension)

In-process mode enables a non-HTTP CLI workflow:

```bash
# Index and query in one shot, no server
java -jar indexer.jar --cli \
  --index hikaru chess.com 2024-03 2024-03 \
  --query "motif(fork) AND white.elo > 2500" \
  --format json
```

Implementation:
- Detect `--cli` flag in `App.main()`
- Skip Micronaut server startup
- Wire beans manually (or use Micronaut `ApplicationContext` without HTTP)
- Run indexing synchronously (bypass queue, call `IndexWorker.process()` directly)
- Run query, print results, exit

This shares 100% of the engine, motif detection, and ChessQL code with the server mode.

## Testing Benefits

H2 in-memory is what lets the suite run integration tests without Docker or test containers:
`TestDb`-backed DAO tests and the `e2e/` suites boot against it directly. See those tests for
the current setup rather than a sample here.

## Comparison with Alternatives

| Approach           | Startup | External Deps | SQL Compat | JSONB | Effort |
|--------------------|---------|---------------|------------|-------|--------|
| **H2 in-memory**   | ~2s     | None          | High       | Partial (TEXT fallback) | Low |
| SQLite (via JDBC)  | ~2s     | Native lib    | Medium     | No    | Medium |
| HSQLDB in-memory   | ~2s     | None          | Medium     | No    | Medium |
| Pure Java maps     | ~1s     | None          | None       | N/A   | High (rewrite DAOs) |
| Testcontainers PG  | ~10s    | Docker        | Perfect    | Yes   | Low    |

H2 is the best tradeoff: pure Java (no native libs, works in Bazel sandbox), high PostgreSQL compatibility, minimal code changes, and sub-second database startup.
