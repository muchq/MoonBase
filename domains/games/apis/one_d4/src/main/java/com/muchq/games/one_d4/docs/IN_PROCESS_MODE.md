# Chess Game Indexer — In-Process Mode

## Quick Start

```bash
# Start the service (one JVM, one Postgres — see the README for the container)
INDEXER_DB_URL="jdbc:postgresql://localhost:5432/indexer" \
  INDEXER_DB_USERNAME=indexer INDEXER_DB_PASSWORD=indexer \
  bazel run //domains/games/apis/one_d4:one_d4

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

**In-process mode is the default.** No SQS, no S3, no separate worker fleet: the HTTP API, the
index worker and the retention sweep all run in one JVM, and `indexing_requests` in the database
*is* the work queue — the in-memory `IndexQueue` survives only as a wake-up nudge.

The one external process is PostgreSQL, and it is required: H2 is a test-only dependency, its
driver is not on the service's classpath, and `INDEXER_DB_URL` has no default. The README has a
one-line `docker run postgres:18` for local work.

This mode is useful for:
- Local development against one container instead of a fleet
- CLI tooling (index a player, query results, exit)
- Demos and evaluations

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
│        └───►│        PostgreSQL            │ │
│             │  indexing_requests            │ │
│             │  game_features               │ │
│             └──────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

One external process — the database — and no others. Start it, start the JAR, use it, stop it.

## Configuration

### Wiring

There is no mode switch. The database is whatever `INDEXER_DB_URL` names, and the
dialect is inferred from it:

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
its own H2 database — the only place H2 is reachable from. Nothing sets it in
production: when it is blank, `readJdbcUrl()` reads `$INDEXER_DB_URL` and throws if it
is missing. Nothing sits under that variable — no host file, no in-memory default.

Credentials are separate environment variables rather than Micronaut properties or
URL query parameters — pgjdbc decodes query values, so a password containing `+`,
`&` or `%` would be corrupted in the URL. Unset leaves whatever credentials the URL
itself carries, which is what keeps credential-bearing URLs (Neon's, for instance)
working unchanged.

### Environment variables

There is no default. `INDEXER_DB_URL` is required, and startup fails without it:

| Variable              | Default Value                            | Effect                                      |
|-----------------------|------------------------------------------|---------------------------------------------|
| `INDEXER_DB_URL`      | (none — required)                        | PostgreSQL JDBC URL. Unset or blank fails at startup. |
| `INDEXER_DB_USERNAME` | (unset)                                  | Database username. Unset leaves whatever the URL carries. |
| `INDEXER_DB_PASSWORD` | (unset)                                  | Database password. Passed as a connection property, so no URL-encoding constraint. |

The DAOs and migrations still carry both dialects and pick one from the URL — that is how the test
suite runs the production DAOs against H2.

## Operational Characteristics

### What Works

- All API endpoints (`POST /v1/index`, `GET /v1/index/{id}`, `POST /v1/query`)
- Full ChessQL query support
- Full motif detection pipeline
- chess.com API fetching (still makes real HTTP calls)
- Concurrent indexing requests, claimed from `indexing_requests` by the in-process worker

### What This Mode Does Not Give You

- No horizontal scaling of the worker: one JVM claims and indexes. Leases in
  `indexing_requests` mean a second instance is safe to add, but that is no longer
  "in-process mode".
- No crash recovery beyond the lease: an interrupted job is reclaimed after its lease
  lapses, not resumed from where it stopped.

Data itself persists — it is in Postgres, and it outlives the process. What bounds its
lifetime is the retention sweep (7 days of games, 30 of requests), not a restart.

### Performance Profile

Postgres is the only database the service can open, so there is no database choice to
tune here. The time goes to the chess.com fetch and PGN replay, which dominate indexing;
the database is not the bottleneck at this size.

Query latency against a local Postgres runs 1-5ms simple, 5-50ms complex, and startup is
~3s including migrations.

### Memory Sizing

The dataset lives in Postgres rather than the heap, so heap is not sized against game count:

```
Base JVM overhead:     ~100MB
Chariot replay state:  ~20MB per concurrent replay (INDEXER_EXTRACTION_THREADS, default 4)
Queue overhead:        Negligible

-Xmx512m is enough for the service itself; extraction concurrency is what moves it.
```

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

This is the *only* place H2 appears: it is declared on the test targets that need it and on
nothing else, so the production dependency closure carries no H2 driver
(`IndexerModuleTest.h2IsNotOnTheProductionClasspath`). The dialect branches in the DAOs and
migrations remain, because the tests exercise the production DAOs rather than copies. What that
buys — and what it costs — is below.

## Comparison with Alternatives

| Approach           | Startup | External Deps | SQL Compat | JSONB | Effort |
|--------------------|---------|---------------|------------|-------|--------|
| **H2 in-memory**   | ~2s     | None          | High       | Partial (TEXT fallback) | Low |
| SQLite (via JDBC)  | ~2s     | Native lib    | Medium     | No    | Medium |
| HSQLDB in-memory   | ~2s     | None          | Medium     | No    | Medium |
| Pure Java maps     | ~1s     | None          | None       | N/A   | High (rewrite DAOs) |
| Testcontainers PG  | ~10s    | Docker        | Perfect    | Yes   | Low    |

H2 remains the best tradeoff **for the test suite**: pure Java (no native libs, works in the Bazel
sandbox), high PostgreSQL compatibility, and sub-second startup, which is what keeps the DAO and
e2e suites runnable on every commit.

The cost is that "high compatibility" is not "perfect": every dialect difference is a place where a
green H2 test can hide a Postgres failure, which is why the `pg_db_tests` suite exists and why CI
supplies `PG_TEST_DB_URL` from a real `postgres:18` service. A behaviour that depends on SQL the two
engines disagree about belongs in that suite, not this one.
