# Chess Game Indexer — Roadmap

## Current State (Phase 1 — Delivered)

- Indexing pipeline: chess.com → PGN parse → position replay → motif detection → PostgreSQL
- ChessQL query language with parameterized SQL compilation
- In-memory queue with interface abstraction
- 5 motif detectors: pin, cross-pin, fork, skewer, discovered attack
- Bazel build with OCI image target
- Test coverage for ChessQL (lexer, parser, compiler), PGN parser, queue

### Known Gaps

- No input validation on API DTOs
- No structured error responses (exceptions propagate as 500s)
- No authentication or rate limiting
- Single worker thread, no concurrency control
- No retry logic for chess.com API failures
- No data retention or lifecycle management
- No observability beyond SLF4J logging
- Motif occurrences only record move number, not the actual move notation (see below)

### Motif Recording Enhancement

Currently, `MotifOccurrence` records only the move number where a motif was detected:
```json
{"moveNumber": 12, "description": "Fork detected at move 12"}
```

This should be enhanced to record the actual move in algebraic notation:
- White knight fork on move 12: `"12. Nf3"` (or the square the knight moved to)
- Black bishop fork after capturing on d5: `"14...Bxd5"`
- Include the piece type, source/destination squares, and capture notation

**Implementation notes:**
- `PositionContext` needs to include the move that led to the position (currently only has FEN and move number)
- `GameReplayer` should pass the SAN (Standard Algebraic Notation) for each move
- `MotifOccurrence` should store: `moveNumber`, `san` (e.g., "Nf3"), `fullNotation` (e.g., "12. Nf3"), `piece`, `fromSquare`, `toSquare`

This enables richer query results and makes it possible to jump directly to the tactical moment in a game viewer.

---

## Phase 2 — Validation & Error Handling

### Input Validation

Add Micronaut validation annotations to DTOs:

```java
public record IndexRequest(
    @NotBlank String player,
    @NotBlank @Pattern(regexp = "chess\\.com|lichess") String platform,
    @NotBlank @Pattern(regexp = "\\d{4}-\\d{2}") String startMonth,
    @NotBlank @Pattern(regexp = "\\d{4}-\\d{2}") String endMonth
) {}
```

Additional validations:
- `startMonth <= endMonth` (semantic check in controller)
- Month range cap (max 12 months per request to bound work)
- `QueryRequest.query` max length (prevent abuse)

### Error Mapping

Add a Micronaut `@Error` handler or exception mapper:

| Exception               | HTTP Status | Response Body                        |
|-------------------------|-------------|--------------------------------------|
| `ParseException`        | 400         | `{"error": "...", "position": N}`    |
| `IllegalArgumentException` | 400      | `{"error": "..."}`                   |
| Entity not found        | 404         | `{"error": "Not found"}`             |
| Unexpected              | 500         | `{"error": "Internal error"}`        |

### Duplicate Request Detection

Before creating a new indexing request, check if an identical (player, platform, startMonth, endMonth) request already exists with status PENDING or PROCESSING. Return the existing request ID instead of creating a duplicate.

### Historical Period Caching

Avoid re-fetching games for player/platform/month combinations that have already been fully indexed.

**Key insight:** A month's games are only "complete" if the fetch occurred *after* the month ended. For example:
- Fetching hikaru's January 2024 games on February 1, 2024 → complete, safe to cache
- Fetching hikaru's January 2024 games on January 15, 2024 → partial, should re-fetch later

**Implementation:**

1. New table to track fetched periods:
```sql
CREATE TABLE indexed_periods (
    id            UUID PRIMARY KEY,
    player        VARCHAR(255) NOT NULL,
    platform      VARCHAR(50) NOT NULL,
    month         VARCHAR(7) NOT NULL,      -- "2024-01"
    fetched_at    TIMESTAMP NOT NULL,
    is_complete   BOOLEAN NOT NULL,         -- true if fetched_at > end of month
    games_count   INT NOT NULL,
    UNIQUE (player, platform, month)
);
```

2. Before fetching a month's games:
   - Check if `indexed_periods` has a complete entry for (player, platform, month)
   - If complete: skip fetch, return existing game count
   - If incomplete or missing: fetch from API, upsert the period record

3. Mark `is_complete = true` only if `fetched_at > last day of month`

4. Optional: Admin endpoint to invalidate cached periods and force re-fetch

**Edge cases:**
- Player changes username → old username's cache is stale (detect via player ID if API provides it)
- Games added retroactively by chess.com (rare) → accept minor staleness or add TTL

### Skip-cache

Allow forcing a full re-fetch and re-index for periods that would otherwise be served from `indexed_periods`, for example when the user knows data changed or for debugging.

**Per-request skip:** Add an optional `skipCache` (or `forceRefresh`) flag to the index request:

- `POST /v1/index` body: `{ "player": "...", "platform": "...", "startMonth": "...", "endMonth": "...", "skipCache": true }`
- When `skipCache` is true, the worker does not use `findCompletePeriod` for that request; it fetches every month in the range from the API and re-indexes, then upserts `indexed_periods` as usual. Existing `game_features` rows for those games may be updated or deduped by `game_url` depending on current insert/merge behavior.

**Admin invalidation:** Optional endpoint to clear or invalidate cached periods so that future requests re-fetch:

- `DELETE /admin/indexed-periods?player=X&platform=Y&yearMonth=2024-01` (or body with a list of (player, platform, year_month)) to delete specific rows from `indexed_periods`.
- Or a “soft” invalidate (e.g. set `is_complete = false` or bump a version) so the next index request for that period refetches.

**Implementation notes:**
- Index request DTO and validation: add optional `Boolean skipCache` (default false).
- IndexWorker: when processing a message, if the request has skipCache, do not call `periodStore.findCompletePeriod` for any month in the range; always fetch. Either pass the flag on `IndexMessage` or look up the request row and read a `skip_cache` column.

### Disk cap

Avoid filling disk when using file-based storage (e.g. H2 file in Docker with `one_d4_data` volume). Neither H2 nor the current Compose config impose a size limit; the volume can grow until the host runs out of space.

**App-level cap (optional):**

- Config: `indexer.disk.maxBytes` (or `INDEXER_DISK_MAX_BYTES`) — maximum allowed size for the data directory (or H2 DB files). Default: 0 or unset = no cap.
- Before accepting a new index request (or before the worker processes the next month): check total size of the data path (e.g. `/data` or the directory containing the H2 `.mv.db` file). If at or over the cap, refuse new work:
  - `POST /v1/index` → 503 or 429 with a clear message (“disk cap reached”).
  - Worker: skip processing or mark request failed with “disk cap reached” and do not fetch/index more data until below cap.
- Optionally expose status: e.g. `GET /health` or a simple admin endpoint that reports current usage and cap so operators can monitor.

**Deployment / volume:**

- Document that production deployments should put the indexer data volume on a quota-backed filesystem or use a volume driver that supports a size limit where available.
- Compose does not support a max size on named volumes directly; document recommended host-level limits or quota setup for `one_d4_data`.

**Note:** Once the indexer uses PostgreSQL (e.g. Neon) instead of H2 file storage, this is less of a concern: the database runs in a managed service with its own storage limits and scaling; the app no longer owns the data directory on disk.

### Fanout by player+year_month

**Current:** One queue message per API request (one `IndexMessage` per player, platform, startMonth, endMonth). The worker iterates months inside `process()`. Overlapping requests (e.g. 2024-01–03 and 2024-02–04) do not dedupe at the queue level; we only skip re-fetching months that are already complete in `indexed_periods`.

**Proposed:** Fan out so that work is enqueued and deduped at (player, platform, year_month) granularity.

1. **Work units:** When `POST /v1/index` is received for a range (e.g. 2024-01 to 2024-03), instead of enqueueing a single message for the range, enqueue one work unit per month in the range (e.g. three units: 2024-01, 2024-02, 2024-03). Each unit is keyed by (player, platform, year_month).

2. **Deduplication:** Before enqueueing a (player, platform, year_month) unit:
   - If that period is already complete in `indexed_periods`, skip (no message).
   - If that unit is already in the queue or currently being processed (e.g. from another request), skip or coalesce so each (player, platform, year_month) is processed at most once.

3. **Request identity:** The API still returns a single request ID for the range. Options:
   - Each work unit carries the request ID(s) that “need” that month; when the unit completes, update progress for all associated requests.
   - Or: one parent “request” row plus a separate table of (request_id, player, platform, year_month) rows; workers process per-month units and update the parent request’s status/count as months complete.

4. **Benefits:** Overlapping requests (e.g. 2024-01–03 and 2024-02–04) automatically share work for 2024-02 and 2024-03; no duplicate fetch or index for the same player+month.

**Implementation notes:**
- `IndexMessage` (or a new per-month message type) would be (requestId, player, platform, year_month) or equivalent; the worker processes one month per message.
- Queue implementation (in-memory, SQS, etc.) may need to support “enqueue if not already present” for (player, platform, year_month), or a separate “pending months” store that the worker claims from.

### Estimated Changes

- 3-4 files modified (DTOs, controllers)
- 1-2 new files (error handler, validation config)
- ~200 lines of code

---

## Phase 3 — Resilience & Retry

### Chess.com API Resilience

The `ChessClient` currently throws on non-200/404 responses. Add:

**Rate Limiting**
- chess.com API has undocumented rate limits (empirically ~10 req/s)
- Add a `RateLimiter` (token bucket, 5 req/s with burst of 10)
- Implementation: `java.util.concurrent.Semaphore` or Guava `RateLimiter`

**Retry with Exponential Backoff**
- Retry on HTTP 429 (Too Many Requests) and 5xx errors
- 3 retries, backoff: 1s → 2s → 4s, with jitter
- Implementation: simple retry loop in `ChessClient`, no external library needed

**Circuit Breaker**
- If consecutive failures exceed threshold (e.g., 5), open circuit for 60s
- Prevents hammering a down API and speeds up failure detection
- Implementation: state machine in a `CircuitBreaker` utility class

```java
public class CircuitBreaker {
    enum State { CLOSED, OPEN, HALF_OPEN }
    // Track failures, state transitions, cooldown timer
}
```

### Queue Retry

**Current**: If `IndexWorker.process()` fails, the message is lost (already dequeued).

**Phase 3 Changes**:
- Add `requeue(IndexMessage message, int attempt)` to `IndexQueue`
- On failure, requeue with incremented attempt count, up to `MAX_ATTEMPTS=3`
- After max attempts, mark request as FAILED with error details
- Add `attempt` field to `IndexMessage`

**Dead Letter Queue (DLQ)**:
- Messages that exceed max attempts go to a DLQ
- DLQ is a separate `LinkedBlockingQueue` (or SQS DLQ later)
- Admin endpoint `GET /admin/dlq` to inspect failed messages
- Admin endpoint `POST /admin/dlq/{id}/retry` to reprocess

### Per-Game Error Isolation

Currently a single game failure is caught and logged, but the overall request continues. Strengthen this:
- Track per-game errors in a `List<String>` on the request
- Store partial results even on overall failure
- Add `games_failed` counter to `indexing_requests`

### Estimated Changes

- 4-5 files modified (ChessClient, IndexWorker, IndexQueue, IndexMessage)
- 2-3 new files (RateLimiter, CircuitBreaker, retry utilities)
- ~400 lines of code

---

## Phase 4 — SQS Queue Migration

### Motivation

The `InMemoryIndexQueue` loses messages on process restart. SQS provides:
- Durability (messages survive restarts)
- Visibility timeout (automatic redelivery on consumer failure)
- Built-in DLQ support
- Multi-consumer scaling

### Implementation

New class `SqsIndexQueue implements IndexQueue`:

```java
public class SqsIndexQueue implements IndexQueue {
    // enqueue → sqs.sendMessage(queueUrl, serialize(message))
    // poll    → sqs.receiveMessage(queueUrl, waitTimeSeconds)
    //           + sqs.deleteMessage(receiptHandle) on success
    // size    → sqs.getQueueAttributes(ApproximateNumberOfMessages)
}
```

**Configuration**:
- `INDEXER_QUEUE_TYPE=memory|sqs` (default: memory)
- `INDEXER_SQS_QUEUE_URL` for SQS mode
- `INDEXER_SQS_DLQ_URL` for dead letter queue
- Visibility timeout: 300s (5 minutes, covers most indexing jobs)
- Max receive count: 3 (then routes to DLQ)

**IndexerModule Change**:
```java
@Context
public IndexQueue indexQueue(@Value("${indexer.queue.type:memory}") String type, ...) {
    return switch (type) {
        case "sqs" -> new SqsIndexQueue(sqsClient, queueUrl);
        default -> new InMemoryIndexQueue();
    };
}
```

### Dependencies

- `software.amazon.awssdk:sqs:2.x` added to `java.MODULE.bazel`
- AWS credentials via environment or IAM role

### Estimated Changes

- 1 file modified (IndexerModule, java.MODULE.bazel)
- 1-2 new files (SqsIndexQueue, SQS config)
- ~200 lines of code

---

## Phase 5 — Security

### Authentication

Add API key authentication via a Micronaut `HttpServerFilter`:

```java
@Filter("/**")
public class ApiKeyFilter implements HttpServerFilter {
    // Check X-API-Key header against configured keys
    // 401 if missing, 403 if invalid
    // Exempt /health endpoint
}
```

**Configuration**: `INDEXER_API_KEYS=key1,key2,key3`

### Authorization

Phase 1: Single-tier (any valid key has full access).
Phase 2: Role-based — `admin` keys can access /admin/*, `user` keys can access /v1/index and /v1/query.

### Rate Limiting

Per-key rate limiting on the /v1/query endpoint:
- 100 queries/minute per API key
- 429 response with `Retry-After` header
- In-memory counter with sliding window (Guava `Cache<String, AtomicInteger>`)
- Later: Redis-backed for multi-instance

### ChessQL Security

Already addressed:
- All values are parameterized (`?` placeholders)
- Field and motif names validated against whitelists
- No raw string interpolation

Additional hardening:
- Max query string length (4KB)
- Max AST depth (20 levels) to prevent stack overflow on deeply nested queries
- Query timeout at the DB level (`SET statement_timeout = '5s'`)

### Estimated Changes

- 2-3 new files (ApiKeyFilter, rate limiter)
- 1-2 files modified (QueryController for timeout, application.yml for config)
- ~250 lines of code

---

## Phase 6 — Data Retention & Cold Storage

### Problem

Game data grows linearly with indexing requests. Without lifecycle management:
- A single player-month can produce 500-2000 games
- Each game row is ~2-5KB (with PGN)
- 1M games ≈ 2-5GB in PostgreSQL
- Query performance degrades as table grows
- Storage costs grow unbounded

### Retention Policy

#### Tier 1: Hot Storage (PostgreSQL, 0-30 days)

All recently indexed games live in the main `game_features` table. Queries hit this table directly.

- Retention period: 30 days from `played_at` or `created_at` of the indexing request
- Full query capability via ChessQL
- Indexed boolean columns for fast motif queries

#### Tier 2: Warm Storage (PostgreSQL archive partition, 30-90 days)

Games older than 30 days are moved to a partitioned archive table with reduced indexing.

```sql
CREATE TABLE game_features_archive (
    LIKE game_features INCLUDING ALL
) PARTITION BY RANGE (played_at);

-- Monthly partitions
CREATE TABLE game_features_archive_2024_01
    PARTITION OF game_features_archive
    FOR VALUES FROM ('2024-01-01') TO ('2024-02-01');
```

- Queries can optionally span both tables (`include_archive=true` on QueryRequest)
- Reduced indexes (drop individual motif indexes, keep composite)
- PGN column retained for re-analysis

#### Tier 3: Cold Storage (S3/GCS, 90+ days)

Games older than 90 days are exported to object storage and deleted from PostgreSQL.

**Export Format**: Newline-delimited JSON (NDJSON), gzipped, partitioned by month:
```
s3://indexer-archive/games/2024/01/games-2024-01.ndjson.gz
s3://indexer-archive/games/2024/02/games-2024-02.ndjson.gz
```

- No direct query capability — must re-index to search
- Admin endpoint `POST /admin/restore?month=2024-01` to re-import from cold storage
- PGN preserved for full re-analysis with updated detectors

#### Tier 4: Deletion (365+ days)

Cold storage objects older than 1 year are deleted via S3 lifecycle policy. This is configurable per deployment.

### Implementation: Retention Worker

New `RetentionWorker` daemon (similar to `IndexWorkerLifecycle`), runs daily:

```java
public class RetentionWorker {
    // 1. Move hot → warm: INSERT INTO game_features_archive SELECT ... WHERE played_at < now() - interval '30 days'
    //                      DELETE FROM game_features WHERE played_at < now() - interval '30 days'
    // 2. Move warm → cold: Export to S3, DROP partition
    // 3. Log metrics: rows moved, bytes exported, partitions dropped
}
```

**Configuration**:

| Variable                          | Default | Description                     |
|-----------------------------------|---------|---------------------------------|
| `INDEXER_RETENTION_HOT_DAYS`      | 30      | Days in hot storage             |
| `INDEXER_RETENTION_WARM_DAYS`     | 90      | Days in warm storage            |
| `INDEXER_RETENTION_COLD_DAYS`     | 365     | Days in cold storage            |
| `INDEXER_ARCHIVE_BUCKET`          | —       | S3/GCS bucket for cold storage  |
| `INDEXER_RETENTION_ENABLED`       | false   | Master switch                   |
| `INDEXER_RETENTION_CRON`          | 0 3 * * * | Daily at 3 AM                |

### Migration Path

The retention system can be introduced incrementally:
1. Add `played_at` index to `game_features` (already exists as column)
2. Add archive table and partition scheme
3. Add retention worker for hot→warm
4. Add S3 export for warm→cold
5. Add restore endpoint

### Estimated Changes

- 3-4 new files (RetentionWorker, S3Archiver, archive migration, retention config)
- 2-3 files modified (Migration.java for archive table, QueryController for archive queries)
- ~500 lines of code

---

## Phase 7 — Observability

### Structured Logging

Replace ad-hoc log messages with structured key-value logging:

```java
LOG.info("index.game.processed", kv("requestId", id), kv("gameUrl", url), kv("motifs", count));
```

Use Logback's `LogstashEncoder` for JSON log output in production.

### Metrics

Expose Micrometer metrics via Micronaut's built-in support:

| Metric                          | Type      | Description                        |
|---------------------------------|-----------|------------------------------------|
| `indexer.requests.created`      | Counter   | Total indexing requests created     |
| `indexer.requests.completed`    | Counter   | Successfully completed              |
| `indexer.requests.failed`       | Counter   | Failed requests                     |
| `indexer.games.indexed`         | Counter   | Total games indexed                 |
| `indexer.games.motifs.detected` | Counter   | Motifs found (tagged by motif type) |
| `indexer.queue.size`            | Gauge     | Current queue depth                 |
| `indexer.query.duration`        | Timer     | ChessQL query execution time        |
| `indexer.chesscom.requests`     | Counter   | API calls to chess.com              |
| `indexer.chesscom.errors`       | Counter   | API errors (tagged by status code)  |
| `indexer.retention.moved`       | Counter   | Rows moved per tier transition      |

### Health Checks

```
GET /health          → {"status": "UP", "checks": {...}}
GET /health/liveness → 200 if process is alive
GET /health/readiness → 200 if DB is reachable and queue is functional
```

### Estimated Changes

- 2-3 new files (health check, metrics config)
- 5-6 files modified (add metrics to worker, controllers, ChessClient)
- ~300 lines of code

---

## Phase 8 — Lichess Support

### Motivation

The platform abstraction (`platform` column, `IndexMessage.platform`) was designed for multi-platform support from the start.

### Implementation

New `LichessClient` alongside `ChessClient`:
- Lichess API: `https://lichess.org/api/games/user/{username}?since=...&until=...`
- Returns PGN stream (not JSON) — needs streaming parser
- Rate limit: 20 req/s (more generous than chess.com)

**Platform Router**:
```java
public class GameFetcher {
    public List<PlayedGame> fetch(String player, String platform, YearMonth month) {
        return switch (platform) {
            case "chess.com" -> chessComClient.fetchGames(player, month);
            case "lichess" -> lichessClient.fetchGames(player, month);
            default -> throw new IllegalArgumentException("Unknown platform: " + platform);
        };
    }
}
```

### Dependencies

- `io.github.tors42:chariot` already supports Lichess API — evaluate using it directly
- Alternatively, use raw HTTP client for the PGN stream endpoint

### Estimated Changes

- 2-3 new files (LichessClient, GameFetcher)
- 2-3 files modified (IndexWorker, IndexerModule)
- ~300 lines of code

---

## Phase 9 — Additional Motifs & Re-Analysis

### New Motifs

| Motif               | Detection Strategy                                    |
|----------------------|------------------------------------------------------|
| Back rank mate       | Checkmate with king on 1st/8th rank, blocked by pawns |
| Smothered mate       | Knight checkmate, king surrounded by own pieces       |
| Sacrifice            | Piece captured where capturer is higher value         |
| Zugzwang             | Position where any move worsens the position (heuristic) |
| Double check         | Two pieces give check simultaneously                  |
| Interference         | Piece placed to block an enemy piece's line           |
| Overloaded piece     | Piece defending two or more targets simultaneously    |

### Re-Analysis Pipeline

When new motif detectors are added, existing games need re-analysis:

1. Admin endpoint: `POST /admin/reanalyze?motif=back_rank_mate`
2. Reads PGN from `game_features.pgn` column
3. Replays and runs only the new detector
4. Updates boolean column and `motifs_json`
5. Batched processing (1000 games per batch) to avoid memory pressure

### Schema Evolution

Adding a new motif column:
```sql
ALTER TABLE game_features ADD COLUMN has_back_rank_mate BOOLEAN DEFAULT FALSE;
ALTER TABLE game_features_archive ADD COLUMN has_back_rank_mate BOOLEAN DEFAULT FALSE;
```

Update `SqlCompiler.VALID_MOTIFS` and `VALID_COLUMNS` sets.

---

## Cost & Ops Complexity Estimates

### Infrastructure Costs (AWS, us-east-1, monthly)

#### Small Deployment (< 100K games)

| Resource                    | Spec                        | Estimated Cost |
|-----------------------------|-----------------------------|----------------|
| PostgreSQL (RDS)            | db.t4g.micro, 20GB gp3     | $15-20         |
| EC2 / ECS (app)             | t4g.small (2 vCPU, 2GB)    | $12-15         |
| S3 (cold storage)           | < 1GB                       | < $1           |
| SQS                         | < 1M messages/month         | < $1           |
| **Total**                   |                             | **~$30/month** |

Ops complexity: **Low**. Single instance, automated backups, no scaling concerns. Can run on a single t4g.small with embedded PostgreSQL for development.

#### Medium Deployment (100K - 1M games)

| Resource                    | Spec                        | Estimated Cost |
|-----------------------------|-----------------------------|----------------|
| PostgreSQL (RDS)            | db.t4g.medium, 100GB gp3   | $50-70         |
| EC2 / ECS (app, 2x)        | t4g.medium (2 vCPU, 4GB)   | $50-60         |
| S3 (cold storage)           | 10-50GB                     | $1-2           |
| SQS                         | < 10M messages/month        | < $5           |
| CloudWatch / monitoring     | Basic                       | $10-15         |
| **Total**                   |                             | **~$120-150/month** |

Ops complexity: **Medium**. Need connection pooling tuning, query performance monitoring, retention job scheduling. Recommend adding an ALB ($16/month) for health checks and graceful deploys. Consider read replicas if query load is high.

#### Large Deployment (1M+ games)

| Resource                    | Spec                        | Estimated Cost  |
|-----------------------------|-----------------------------|-----------------|
| PostgreSQL (RDS)            | db.r7g.large, 500GB gp3, read replica | $300-400  |
| ECS Fargate (app, 3x)      | 1 vCPU, 2GB                 | $80-100         |
| ECS Fargate (workers, 2x)  | 1 vCPU, 2GB                 | $50-70          |
| S3 (cold storage)           | 50-500GB                    | $5-15           |
| SQS + DLQ                   | Standard                    | $5-10           |
| ALB                         | Standard                    | $20-25          |
| CloudWatch + alarms         | Enhanced                    | $30-50          |
| **Total**                   |                             | **~$500-650/month** |

Ops complexity: **High**. Partition management for archive tables, retention job monitoring, S3 lifecycle policies, multi-worker coordination (SQS visibility timeout tuning), query performance (may need `EXPLAIN ANALYZE` review, index tuning). Consider:
- Splitting reads and writes to separate DB instances
- Connection pooling via PgBouncer
- Caching frequent ChessQL queries (Redis, ~$15/month for cache.t4g.micro)

### Storage Growth Model

| Metric                     | Per Game | 100K Games | 1M Games  |
|----------------------------|----------|------------|-----------|
| game_features row (no PGN) | ~500B    | ~50MB      | ~500MB    |
| PGN column                  | ~2-3KB   | ~250MB     | ~2.5GB    |
| motifs_json column          | ~200B    | ~20MB      | ~200MB    |
| **Total per game**          | ~3KB     | ~300MB     | ~3GB      |
| Indexes overhead            | ~30%     | ~100MB     | ~1GB      |
| **Total with indexes**      |          | ~400MB     | ~4GB      |

Cold storage (gzipped NDJSON) achieves ~5:1 compression, so 1M games ≈ 600MB in S3.

### Ops Complexity Summary

| Phase | Complexity | New Ops Burden                                          |
|-------|------------|--------------------------------------------------------|
| 1 (current) | Low   | Deploy, PostgreSQL backup                               |
| 2 (validation) | Low | None additional                                       |
| 3 (resilience) | Low | Monitor DLQ depth                                     |
| 4 (SQS) | Medium     | SQS queue monitoring, IAM roles, DLQ alarms            |
| 5 (security) | Medium | API key rotation, rate limit tuning                    |
| 6 (retention) | High | Partition management, S3 lifecycle, retention job monitoring, restore testing |
| 7 (observability) | Medium | Dashboard setup, alert thresholds, log aggregation |
| 8 (lichess) | Low    | Additional API rate limit monitoring                    |
| 9 (re-analysis) | Medium | Batch job monitoring, schema migration coordination |

### Recommended Implementation Order

```
Phase 2 (validation)     ← Low effort, high safety impact
Phase 5 (security)       ← Required before any public exposure
Phase 3 (resilience)     ← Required before production indexing load
Phase 7 (observability)  ← Required before diagnosing production issues
Phase 4 (SQS)            ← Required before multi-instance deployment
Phase 8 (lichess)        ← Feature expansion, independent of infra
Phase 6 (retention)      ← Required once storage exceeds ~10GB
Phase 9 (re-analysis)    ← Nice-to-have, depends on new motif demand
```

Each phase is independently deployable. No phase has a hard dependency on another, though the recommended order minimizes rework.
