# Chess Game Indexer — DataFusion + Parquet Query Engine

## Overview

Add a Rust service (`motif_query`) that uses Apache DataFusion to query
Parquet files containing game features and motif occurrences. The Java
indexing pipeline writes Parquet instead of (or in addition to) SQL rows.
A pre-indexing pipeline bulk-loads GM games from the Lichess open database
into the same Parquet format. ChessQL compiles to DataFusion SQL instead of
PostgreSQL SQL.

## Why DataFusion + Parquet

| Concern | PostgreSQL / H2 | DataFusion + Parquet |
|---------|-----------------|---------------------|
| Motif search (boolean filters) | B-tree index per flag, row-oriented scan | Columnar min/max pruning, vectorized filter pushdown |
| Aggregate queries (motif counts) | COUNT + GROUP BY on normalized table | Columnar aggregation, no join needed if denormalized |
| Storage cost | ~120 bytes/row uncompressed | ~15-25 bytes/row with Snappy, columnar compression |
| Bulk ingest (Lichess dump) | INSERT per row, index maintenance | Write sorted Parquet files offline, register instantly |
| Horizontal scaling | Read replicas, connection pooling | Stateless query over object storage (S3/local) |
| Schema evolution | ALTER TABLE migrations | Schema-on-read, additive columns are free |
| Cold/warm tiering | Manual partition management | Partition by month, drop old partitions = delete files |

The workload is write-once, read-many analytical queries over boolean flags
and low-cardinality string columns — the exact sweet spot for columnar
storage. The current `game_features` table has 11 boolean motif columns
plus metadata: a textbook columnar layout.

## Architecture

```
                                ┌──────────────────────┐
                                │   Lichess PGN dump    │
                                │  (monthly .zst file)  │
                                └──────────┬───────────┘
                                           │ offline bulk pipeline
                                           ▼
┌──────────────┐   index    ┌──────────────────────────┐
│  one_d4 Java │ ────────►  │   Parquet writer (Rust)  │
│  indexer     │  (gRPC)    │   arrow-rs + parquet-rs  │
└──────────────┘            └──────────┬───────────────┘
                                       │ writes
                                       ▼
                            ┌──────────────────────────┐
                            │  Parquet files on disk/S3 │
                            │                          │
                            │  /data/games/            │
                            │    platform=chess.com/    │
                            │      month=2024-03/      │
                            │        part-0000.parquet  │
                            │    platform=lichess/     │
                            │      month=2024-03/      │
                            │        part-0000.parquet  │
                            └──────────┬───────────────┘
                                       │ reads
                                       ▼
                            ┌──────────────────────────┐
                            │  motif_query (Rust)      │
                            │  DataFusion SessionCtx   │
                            │  + axum HTTP API         │
                            └──────────┬───────────────┘
                                       │
                            ┌──────────┴───────────────┐
                            │  one_d4 Java API         │
                            │  (proxies /v1/query to   │
                            │   motif_query, keeps     │
                            │   /v1/index as-is)       │
                            └──────────────────────────┘
```

### Component Responsibilities

| Component | Language | Role |
|-----------|----------|------|
| `one_d4` (existing) | Java | REST API, indexing orchestration, Chess.com client, motif detection, request tracking |
| `motif_query` (new) | Rust | DataFusion query engine, Parquet reads, ChessQL-to-SQL compilation |
| `parquet_writer` (lib in motif_query) | Rust | Arrow RecordBatch construction, Parquet file writing, partition management |
| `lichess_ingest` (bin in motif_query) | Rust | Bulk PGN parsing, motif detection, Parquet generation from Lichess dumps |

## Parquet Schema

### `game_features` table (one row per game)

```
game_url:           Utf8        (unique identifier)
platform:           Utf8        (partition column: "chess.com", "lichess")
month:              Utf8        (partition column: "2024-03")
white_username:     Utf8
black_username:     Utf8
white_elo:          Int32
black_elo:          Int32
time_class:         Utf8
eco:                Utf8
result:             Utf8
played_at:          TimestampMillisecond
num_moves:          Int32
has_pin:            Boolean
has_cross_pin:      Boolean
has_fork:           Boolean
has_skewer:         Boolean
has_discovered_attack: Boolean
has_discovered_check:  Boolean
has_check:          Boolean
has_checkmate:      Boolean
has_promotion:      Boolean
has_promotion_with_check:     Boolean
has_promotion_with_checkmate: Boolean
```

PGN text is **not** stored in the Parquet files — it bloats columnar scans
and is only needed for game replay, not motif search. See "Where does PGN
go?" below.

### `motif_occurrences` table (one row per motif firing)

```
game_url:           Utf8
platform:           Utf8        (partition column)
month:              Utf8        (partition column)
motif:              Utf8        (e.g. "PIN", "FORK")
ply:                Int32
side:               Utf8        ("white" or "black")
move_number:        Int32
description:        Utf8
```

### Partitioning Strategy

Hive-style partitioning: `{base_path}/{table}/platform={p}/month={m}/part-NNNN.parquet`

```
/data/
  game_features/
    platform=chess.com/
      month=2024-03/
        part-0000.parquet    # ~50K rows, ~2 MB
    platform=lichess/
      month=2024-01/
        part-0000.parquet    # ~500K rows, ~18 MB
        part-0001.parquet
  motif_occurrences/
    platform=lichess/
      month=2024-01/
        part-0000.parquet
```

DataFusion's `ListingTable` with `ListingOptions::new(ParquetFormat)` and
`table_partition_cols = ["platform", "month"]` automatically prunes
partitions from `WHERE platform = 'lichess' AND month >= '2024-01'`.

Target file size: ~50 MB uncompressed (~10-15 MB Snappy). This gives good
row group pruning without too many small files.

## ChessQL Compilation to DataFusion SQL

The current `SqlCompiler` generates PostgreSQL-dialect SQL. DataFusion
speaks standard SQL with some differences:

| Feature | PostgreSQL | DataFusion |
|---------|-----------|------------|
| Case-insensitive compare | `LOWER(col) = LOWER(?)` | `LOWER(col) = LOWER(?)` (same) |
| Boolean literals | `= TRUE` | `= TRUE` (same) |
| Parameterized queries | `?` placeholders via JDBC | Inline literals (DataFusion `ParamValues`) or string interpolation |
| Timestamp literals | `'2024-03-15'::timestamp` | `CAST('2024-03-15' AS TIMESTAMP)` |
| COALESCE | supported | supported |
| EXISTS subquery (sequence) | supported | supported |

The mapping is close enough that we can write a `DataFusionCompiler`
implementing the same `QueryCompiler<T>` interface, producing a SQL string
that DataFusion's `SessionContext::sql()` can execute. Since DataFusion
supports parameterized queries (`ParamValues`), we can keep the
bind-parameter approach.

### Compiler Implementation (Rust)

Rather than compiling in Java and sending SQL over the wire, the cleaner
approach is:

1. The Java `one_d4` API receives a ChessQL query string.
2. It forwards the raw ChessQL string to `motif_query` via HTTP.
3. `motif_query` has its own ChessQL parser + DataFusion compiler in Rust.
4. Results come back as JSON.

This means porting the ChessQL lexer/parser to Rust, which is
straightforward — the grammar is small (the Java parser is ~200 lines).
This keeps the query compilation and execution colocated, avoiding
cross-process SQL string passing.

Alternatively, if porting ChessQL to Rust is deferred: the Java side
compiles to SQL with a new `DataFusionCompiler` and sends the SQL string
to `motif_query` which executes it directly. This is simpler to start with
but couples the Java and Rust sides on SQL dialect.

**Recommendation:** Start with Option B (Java compiles SQL, Rust executes)
for the initial implementation. Port ChessQL to Rust later when the Rust
service stabilizes.

## motif_query Service

### Crate Structure

```
domains/games/apis/motif_query/
  Cargo.toml
  src/
    main.rs              # axum server + DataFusion setup
    catalog.rs           # Table registration, partition discovery
    query.rs             # Query execution, result serialization
    writer.rs            # Parquet file writing (used by ingest + index)
    ingest/
      mod.rs             # Lichess bulk ingest orchestrator
      pgn.rs             # PGN stream parser (Lichess NDJSON/PGN format)
      filter.rs          # GM game filtering (title, Elo threshold)
  BUILD.bazel
```

### Dependencies

```toml
[dependencies]
arrow = { version = "55" }
datafusion = { version = "46" }
parquet = { version = "55", features = ["snap"] }
axum = { workspace = true }
tokio = { workspace = true }
serde = { workspace = true }
serde_json = { workspace = true }
server_pal = { workspace = true }
uuid = { workspace = true }
chrono = { workspace = true }
zstd = "0.13"             # Lichess dumps are .zst compressed
pgn-reader = "0.26"       # PGN parsing (or custom, see below)
```

### API Endpoints

```
POST /motif_query/v1/query
  Body: { "sql": "SELECT ... FROM game_features WHERE ..." }
  Response: { "rows": [...], "row_count": N }

POST /motif_query/v1/query/chessql    # (phase 2, after Rust ChessQL port)
  Body: { "query": "white.elo >= 2500 AND motif(fork)" }
  Response: { "rows": [...], "row_count": N }

POST /motif_query/v1/write
  Body: { "table": "game_features", "rows": [...] }
  Response: { "files_written": 1 }

GET /motif_query/v1/partitions
  Response: { "game_features": ["chess.com/2024-03", ...], "motif_occurrences": [...] }

GET /health
  Response: 200 OK
```

### DataFusion Setup

```rust
use datafusion::prelude::*;

async fn create_session(data_dir: &str) -> SessionContext {
    let ctx = SessionContext::new();

    // Register game_features as a partitioned Parquet table
    let game_opts = ListingOptions::default()
        .with_file_extension("parquet")
        .with_table_partition_cols(vec![
            ("platform".to_string(), DataType::Utf8),
            ("month".to_string(), DataType::Utf8),
        ]);

    ctx.register_listing_table(
        "game_features",
        &format!("{data_dir}/game_features/"),
        game_opts,
        None,
        None,
    ).await.unwrap();

    // Register motif_occurrences similarly
    // ...

    ctx
}
```

## Lichess Bulk Ingest Pipeline

### Data Source

Lichess publishes monthly game databases at
`https://database.lichess.org/`. Each month is a single `.pgn.zst` file
(Zstandard-compressed PGN). Recent months are 15-25 GB compressed,
~150-250 GB uncompressed, containing 80-100M games.

### GM Game Filtering

We don't want 100M games per month. Filter to titled players and high-Elo
games:

```
Criteria (configurable):
  - At least one player has a title (GM, IM, WGM, WIM)
    OR
  - Both players rated >= 2200
  - Time control: standard, rapid, blitz (exclude bullet, ultrabullet)
```

This typically reduces the dataset to ~1-3% of total games (~1-3M
games/month), which is very manageable.

### Pipeline Steps

```
1. Download .pgn.zst file (streaming, don't buffer full file)
     ↓
2. Decompress zstd stream
     ↓
3. Parse PGN headers (extract players, Elo, title, time control, result)
     ↓
4. Filter: does this game meet GM/titled criteria?
     ↓  (skip ~97% of games here, before parsing moves)
5. Parse movetext for qualifying games
     ↓
6. Replay positions + run motif detectors
     ↓
7. Accumulate into Arrow RecordBatch (buffer ~50K games)
     ↓
8. Write Parquet partition file
     ↓
9. Register new partition with DataFusion catalog
```

### Motif Detection in Rust

The Java `FeatureExtractor` + `GameReplayer` + 11 `MotifDetector`
implementations need Rust equivalents. Options:

**Option A: Pure Rust reimplementation.** Use an existing Rust chess library
(`shakmaty` or `chess`) for move generation and board state. Port the
detector logic. The detectors are straightforward ray-casting and attack
checks — the Java implementations are each 30-80 lines.

**Option B: Call into Java for detection.** Run the Lichess ingest as a
batch job that calls `one_d4`'s feature extraction via HTTP. Simpler but
slow for bulk processing (HTTP overhead per game).

**Option C: Shared library.** Compile the Java detectors to a native
library via GraalVM native-image and call from Rust via FFI.

**Recommendation: Option A.** The `shakmaty` crate is mature and fast
(~10x faster than Java chess libs for move generation). The detector logic
is simple enough that porting it is a few days of work and the result is a
self-contained, high-performance pipeline. For 1M games/month with ~40
moves/game, this is ~40M positions to analyze — Rust will handle this in
minutes, not hours.

### CLI Interface

```
motif_query ingest \
  --input lichess_db_standard_rated_2024-01.pgn.zst \
  --output /data/ \
  --min-elo 2200 \
  --titles GM,IM,WGM,WIM \
  --time-controls standard,rapid,blitz \
  --batch-size 50000
```

## Integration with one_d4 Java API

### Indexing Flow (Chess.com games)

The existing Java pipeline continues to handle Chess.com indexing. After
motif detection, instead of SQL INSERT, the worker calls `motif_query`'s
write endpoint:

```
IndexWorker.process()
  ├─ ChessClient.fetchGames()           # unchanged
  ├─ FeatureExtractor.extract()          # unchanged
  └─ POST motif_query/v1/write           # NEW: replaces GameFeatureDao.insert()
       { "table": "game_features",
         "platform": "chess.com",
         "month": "2024-03",
         "rows": [ ... batch of GameFeatureRows ... ] }
```

The write endpoint buffers rows and flushes to Parquet when a batch
threshold is reached or on explicit flush.

### Query Flow

```
POST /v1/query { "query": "motif(fork) AND white.elo >= 2500" }
  ↓
one_d4 QueryController
  ↓
ChessQL Parser → SqlCompiler (new DataFusionCompiler) → SQL string
  ↓
POST motif_query/v1/query { "sql": "SELECT ... FROM game_features WHERE ..." }
  ↓
DataFusion executes over Parquet
  ↓
JSON results back to one_d4
  ↓
one_d4 formats response, returns to client
```

## Do We Keep H2/PostgreSQL?

**Short answer: Yes, but only for operational state — not for game/motif data.**

### What moves to Parquet (DataFusion)

- `game_features` — all game metadata and motif flags
- `motif_occurrences` — all motif firing details

These are the analytical query targets. Parquet is strictly better here:
faster scans, cheaper storage, no index maintenance, trivial partitioned
retention (delete a directory to drop a month).

### What stays in PostgreSQL/H2

| Table | Why it stays |
|-------|-------------|
| `indexing_requests` | Mutable operational state (PENDING → PROCESSING → COMPLETED). Parquet is append-only — you can't UPDATE a status field. This table is small (hundreds of rows), queried by primary key, and needs ACID transactions. |
| `indexed_periods` | Mutable cache of which (player, platform, month) combos have been fetched. Updated with `is_complete` flag. Same argument: small, mutable, transactional. |

### What about PGN storage?

PGN text is large and variable-length — storing it in Parquet columnar
files bloats every scan even when PGN isn't selected. Options:

1. **Separate Parquet table** with just `(game_url, pgn)` — only read when
   a user requests the actual PGN for a specific game.
2. **Keep in PostgreSQL** in a `game_pgns` table — simple key-value lookup.
3. **Object storage** — one file per game in S3, keyed by game_url hash.
4. **Don't store it** — for Lichess games, link to
   `https://lichess.org/game/export/{id}`. For Chess.com, link to the game
   URL.

**Recommendation: Option 4 for Lichess (link back), Option 1 for
Chess.com** (separate Parquet table, since Chess.com doesn't have a
reliable PGN export URL). This keeps the main game_features scans lean.

### Migration Path

```
Phase 1: Parquet writer + DataFusion query service (motif_query)
         Chess.com indexer dual-writes to both PostgreSQL and Parquet.
         Queries still go to PostgreSQL.

Phase 2: Query switchover.
         /v1/query routes to motif_query (DataFusion).
         PostgreSQL game_features/motif_occurrences become write-only backup.

Phase 3: Drop PostgreSQL game tables.
         Only indexing_requests and indexed_periods remain in PostgreSQL.
         Retention = delete partition directories on a schedule.

Phase 4: Lichess bulk ingest.
         Run motif_query ingest on historical Lichess dumps.
         Monthly cron to ingest new dumps as they're published.
```

## Implementation Plan

### Phase 1: motif_query crate scaffold (1-2 days)

- Create `domains/games/apis/motif_query/` with Cargo.toml, BUILD.bazel
- Add `datafusion`, `arrow`, `parquet` workspace dependencies
- Implement `catalog.rs`: register Parquet listing tables with partition columns
- Implement `query.rs`: accept SQL string, execute via DataFusion, return JSON
- Implement `writer.rs`: accept JSON rows, buffer, flush to partitioned Parquet
- axum server with `/v1/query`, `/v1/write`, `/health` endpoints
- Unit tests with in-memory Parquet roundtrips

### Phase 2: DataFusionCompiler in Java (1 day)

- New `DataFusionCompiler implements QueryCompiler<CompiledQuery>` in chessql
- Differences from `SqlCompiler`: inline string literals (escape properly),
  use DataFusion timestamp syntax, adjust sequence subquery if needed
- Wire into `QueryController` behind a feature flag / config toggle
- Tests mirroring `SqlCompilerTest`

### Phase 3: Dual-write from IndexWorker (1 day)

- Add HTTP client in `IndexWorker` to POST batches to `motif_query/v1/write`
- Continue writing to PostgreSQL for rollback safety
- Add config toggle: `MOTIF_QUERY_URL` (if set, dual-write is enabled)

### Phase 4: Lichess ingest pipeline (3-5 days)

- PGN stream parser: read `.pgn.zst`, extract headers without full parse
- GM/title filter: parse `WhiteTitle`/`BlackTitle` headers, Elo thresholds
- Port motif detectors to Rust using `shakmaty` crate:
  - `PinDetector` (ray cast from king)
  - `ForkDetector` (attack count on high-value targets)
  - `SkewerDetector` (ray through more valuable piece)
  - `DiscoveredAttackDetector` / `DiscoveredCheckDetector`
  - `CheckDetector` / `CheckmateDetector` (from board state)
  - `PromotionDetector` variants (from move type)
- Batch writer: accumulate RecordBatches, flush at threshold
- CLI binary: `motif_query ingest --input ... --output ...`
- Test against a small Lichess sample file

### Phase 5: Query switchover + PostgreSQL game table removal (1 day)

- Route `/v1/query` to DataFusion path
- Verify correctness against PostgreSQL results (shadow mode)
- Remove `game_features` and `motif_occurrences` from Migration.java
- Update `RetentionWorker` to delete old partition directories instead of SQL DELETE

### Phase 6: ChessQL Rust port (optional, 2-3 days)

- Port lexer, parser, AST to Rust
- DataFusion-native compilation (produce `LogicalPlan` directly instead of SQL string)
- Eliminates the Java→SQL→Rust→DataFusion hop

## Cost and Performance Estimates

### Storage

| Dataset | PostgreSQL | Parquet (Snappy) |
|---------|-----------|-----------------|
| 1M games + occurrences | ~800 MB | ~80-120 MB |
| 12 months Lichess GM games (~20M) | ~16 GB | ~1.5-2.5 GB |

### Query Performance (estimated, single-node)

| Query pattern | PostgreSQL (indexed) | DataFusion (Parquet) |
|--------------|---------------------|---------------------|
| `motif(fork) AND white.elo >= 2500` | ~50-200ms (index scan) | ~20-80ms (column pruning + predicate pushdown) |
| `motif(fork) AND motif(pin)` (2 booleans) | ~100-300ms | ~15-50ms (vectorized AND on boolean columns) |
| `ORDER BY motif_count(check)` | ~500ms+ (JOIN + COUNT) | ~100-300ms (columnar aggregation) |
| Full scan, no filters | ~2-5s (1M rows) | ~200-500ms (columnar, compressed) |

### Ingest Performance

| Operation | Estimated throughput |
|-----------|---------------------|
| Lichess PGN parse + filter (headers only) | ~500K games/sec |
| Full motif analysis (shakmaty + detectors) | ~5K-10K games/sec |
| Parquet write (50K row batches) | ~100K rows/sec |
| End-to-end: 1 month Lichess (100M games, ~2M qualifying) | ~5-10 minutes |

## Open Questions

1. **Object storage vs local disk?** Starting with local disk is simpler.
   DataFusion supports `object_store` for S3/GCS — add it later when
   deploying to cloud.

2. **Real-time vs batch for Chess.com games?** The write endpoint buffers
   rows — how long before flushing? Options: flush every N rows (e.g. 1000),
   flush every M seconds, or flush on explicit API call. Small Parquet files
   hurt scan performance, so buffering is important.

3. **Compaction?** Many small writes produce many small files. A background
   compaction job should periodically merge small files into larger ones per
   partition. DataFusion doesn't have built-in compaction, but it's
   straightforward: read partition → write single large file → swap.

4. **Do we need the motif_occurrences table at all in Parquet?** The
   `game_features` boolean flags handle most ChessQL queries. The
   occurrences table is only needed for `sequence()` queries and
   `ORDER BY motif_count()`. If those features are rarely used, we could
   defer the occurrences table and keep it in PostgreSQL temporarily.
