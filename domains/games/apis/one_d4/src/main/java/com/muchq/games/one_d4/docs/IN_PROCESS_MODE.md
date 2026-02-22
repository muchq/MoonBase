# Chess Game Indexer — In-Process Mode

## Quick Start

```bash
# Start the service (H2 in-memory, no external dependencies)
INDEXER_DB_URL="jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1" bazel run //domains/games/apis/one_d4:indexer

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

**In-process mode is the default.** The indexer runs with no external dependencies — no PostgreSQL, no SQS, no S3. Everything lives in-process using an in-memory queue and an H2 in-memory database.

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
│  │ /v1/index ├──►│  (LinkedBlockingQueue) │    │
│  │ /v1/query │   └──────────┬───────────┘    │
│  └─────┬─────┘              │                │
│        │              ┌─────▼──────┐         │
│        │              │IndexWorker │         │
│        │              └─────┬──────┘         │
│        │                    │                │
│        │    ┌───────────────▼──────────────┐ │
│        └───►│       H2 (in-memory)         │ │
│             │  indexing_requests            │ │
│             │  game_features               │ │
│             └──────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

Zero external processes. Start the JAR, use it, stop it. Data lives only for the lifetime of the process.

## Implementation Plan

### 1. Add H2 Dependency

H2 is a pure-Java SQL database that runs embedded. It supports a large subset of PostgreSQL syntax.

```
# bazel/java.MODULE.bazel
"com.h2database:h2:2.2.224",
```

Bazel label: `@maven//:com_h2database_h2`

### 2. Adapt DataSourceFactory

```java
public class DataSourceFactory {
    public static DataSource create(String jdbcUrl, String username, String password) {
        // Existing HikariCP path — works for both H2 and PostgreSQL
        HikariConfig config = new HikariConfig();
        config.setJdbcUrl(jdbcUrl);
        config.setUsername(username);
        config.setPassword(password);
        config.setMaximumPoolSize(10);
        config.setMinimumIdle(2);
        return new HikariDataSource(config);
    }

    public static DataSource createInMemory() {
        return create("jdbc:h2:mem:indexer;MODE=PostgreSQL;DB_CLOSE_DELAY=-1", "sa", "");
    }
}
```

Key H2 JDBC URL flags:
- `mem:indexer` — named in-memory database
- `MODE=PostgreSQL` — PostgreSQL compatibility (boolean handling, function names)
- `DB_CLOSE_DELAY=-1` — keep DB alive as long as the JVM runs (default is to drop when last connection closes)

### 3. Adapt Migration.java for H2 Compatibility

The existing DDL uses `gen_random_uuid()` and `JSONB`, which H2 does not support in PostgreSQL mode. The migration needs dialect-aware DDL:

```java
public class Migration {
    private final DataSource dataSource;
    private final boolean isH2;

    public Migration(DataSource dataSource) {
        this.dataSource = dataSource;
        this.isH2 = detectH2(dataSource);
    }

    public void run() {
        if (isH2) {
            runH2Schema();
        } else {
            runPostgresSchema();
        }
    }

    private void runH2Schema() {
        // UUID PRIMARY KEY DEFAULT random_uuid()
        // TEXT instead of JSONB
        // No gen_random_uuid() — use random_uuid()
    }

    private boolean detectH2(DataSource ds) {
        try (Connection conn = ds.getConnection()) {
            return conn.getMetaData().getDatabaseProductName().contains("H2");
        }
    }
}
```

**Schema Differences**:

| Feature         | PostgreSQL              | H2 (PostgreSQL mode)        |
|-----------------|-------------------------|-----------------------------|
| UUID default    | `gen_random_uuid()`     | `random_uuid()`             |
| JSON column     | `JSONB`                 | `TEXT`                      |
| `?::jsonb` cast | Supported               | Use plain `TEXT` parameter  |
| `ON CONFLICT`   | Supported               | `MERGE INTO` or `INSERT IGNORE` — H2 2.x supports `ON CONFLICT` in PostgreSQL mode |
| `RETURNING`     | Supported               | Not supported — use `CALL IDENTITY()` or `getGeneratedKeys()` |

### 4. Adapt DAOs for Dialect Differences

The `IndexingRequestDao.create()` method uses `RETURNING id`, which H2 doesn't support. Use JDBC's `getGeneratedKeys()` instead — this works for both databases:

```java
public UUID create(String player, String platform, String startMonth, String endMonth) {
    String sql = "INSERT INTO indexing_requests (id, player, platform, start_month, end_month) VALUES (?, ?, ?, ?, ?)";
    UUID id = UUID.randomUUID();
    try (Connection conn = dataSource.getConnection();
         PreparedStatement ps = conn.prepareStatement(sql)) {
        ps.setObject(1, id);
        ps.setString(2, player);
        ps.setString(3, platform);
        ps.setString(4, startMonth);
        ps.setString(5, endMonth);
        ps.executeUpdate();
        return id;
    }
}
```

This approach generates the UUID in Java, which works identically on both databases and avoids the `RETURNING` clause entirely.

The `GameFeatureDao.insert()` method uses `?::jsonb` for the motifs column. In H2 mode, this becomes a plain string parameter.

```java
public void insert(GameFeatureRow row) {
    String sql = isH2
        ? "INSERT INTO game_features (..., motifs_json, ...) VALUES (..., ?, ...) ON CONFLICT (game_url) DO NOTHING"
        : "INSERT INTO game_features (..., motifs_json, ...) VALUES (..., ?::jsonb, ...) ON CONFLICT (game_url) DO NOTHING";
    // ...
}
```

### 5. IndexerModule Wiring

```java
@Factory
public class IndexerModule {

    @Context
    public DataSource dataSource(
            @Value("${indexer.mode:postgres}") String mode,
            @Value("${indexer.db.url:jdbc:postgresql://localhost:5432/indexer}") String jdbcUrl,
            @Value("${indexer.db.username:indexer}") String username,
            @Value("${indexer.db.password:indexer}") String password) {
        return switch (mode) {
            case "in-process" -> DataSourceFactory.createInMemory();
            default -> DataSourceFactory.create(jdbcUrl, username, password);
        };
    }

    // Queue is already InMemoryIndexQueue by default — no change needed
}
```

### 6. Configuration

Environment variables control the mode (H2 in-memory is the default):

| Variable              | Default Value                            | Effect                                      |
|-----------------------|------------------------------------------|---------------------------------------------|
| `INDEXER_DB_URL`      | `jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1`  | H2 in-memory (default). No external deps.   |
| `INDEXER_DB_URL`      | `jdbc:postgresql://localhost:5432/...`   | PostgreSQL mode. Requires external database.|
| `INDEXER_DB_USERNAME` | `sa`                                     | Database username.                          |
| `INDEXER_DB_PASSWORD` | (empty)                                  | Database password.                          |

The system auto-detects H2 vs PostgreSQL from the JDBC URL and uses the appropriate SQL dialect.

## Operational Characteristics

### What Works

- All API endpoints (`POST /v1/index`, `GET /v1/index/{id}`, `POST /v1/query`)
- Full ChessQL query support
- Full motif detection pipeline
- chess.com API fetching (still makes real HTTP calls)
- Concurrent indexing requests via the queue

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

In-process mode makes integration tests trivial:

```java
public class IntegrationTest {
    private DataSource ds;
    private GameFeatureDao dao;
    private SqlCompiler compiler;

    @Before
    public void setUp() {
        ds = DataSourceFactory.createInMemory();
        new Migration(ds).run();
        dao = new GameFeatureDao(ds);
        compiler = new SqlCompiler();
    }

    @Test
    public void testEndToEndQuery() {
        // Insert test data directly
        dao.insert(testRow("game1", 2500, 2400, true, false, false, false, false));
        dao.insert(testRow("game2", 2100, 2000, false, true, false, false, false));

        // Query via ChessQL
        Expr expr = Parser.parse("white.elo >= 2500 AND motif(pin)");
        CompiledQuery cq = compiler.compile(expr);
        List<GameFeatureDao.GameFeatureRow> results = dao.query(cq, 10, 0);

        assertThat(results).hasSize(1);
        assertThat(results.get(0).whiteElo()).isEqualTo(2500);
    }
}
```

No test containers. No Docker. No database setup. Sub-second test execution.

## Files Modified

| File | Description |
|------|-------------|
| `bazel/java.MODULE.bazel` | Added `com.h2database:h2:2.2.224` |
| `Migration.java` | H2 dialect detection + compatible DDL (separate SQL for H2 vs PostgreSQL) |
| `GameFeatureDao.java` | Dialect-aware `jsonb` cast and `MERGE` vs `ON CONFLICT` |
| `IndexerModule.java` | Default to H2 in-memory, auto-detect dialect from JDBC URL |
| `db/BUILD.bazel` | Added H2 runtime dep |

The entire in-process mode is a configuration default with ~100 lines of dialect adaptation across existing files.

## Comparison with Alternatives

| Approach           | Startup | External Deps | SQL Compat | JSONB | Effort |
|--------------------|---------|---------------|------------|-------|--------|
| **H2 in-memory**   | ~2s     | None          | High       | Partial (TEXT fallback) | Low |
| SQLite (via JDBC)  | ~2s     | Native lib    | Medium     | No    | Medium |
| HSQLDB in-memory   | ~2s     | None          | Medium     | No    | Medium |
| Pure Java maps     | ~1s     | None          | None       | N/A   | High (rewrite DAOs) |
| Testcontainers PG  | ~10s    | Docker        | Perfect    | Yes   | Low    |

H2 is the best tradeoff: pure Java (no native libs, works in Bazel sandbox), high PostgreSQL compatibility, minimal code changes, and sub-second database startup.
