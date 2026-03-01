# Chess Game Indexer — DataFusion + Parquet Query Engine

## Overview

Add a Rust service (`motif_query`) that uses Apache DataFusion to query
Parquet files containing game features and motif occurrences. The Java
indexing pipeline continues to write to SQL as it does today. A periodic
batch job (weekly or monthly) exports SQL game data to Parquet for
analytical queries. Lichess bulk ingest writes directly to Parquet,
bypassing SQL entirely. A metadata table tracks which storage backend
holds each game's data, enabling the query router to dispatch to the
right backend.

**Dual-backend compilation.** The existing `SqlCompiler` handles
PostgreSQL queries unchanged. A new `DataFusionSqlCompiler` compiles
ChessQL to DataFusion SQL for the `motif_query` service. Both compilers
share the same ChessQL AST; dialect differences are isolated in each
compiler. Contract tests seed both backends with the same data and assert
result equivalence for all query patterns.

This design favors reliability over abstraction: two well-tested SQL
compilers with contract-test safety nets are simpler and more robust than
a Substrait IR layer whose toolchain has known gaps for the correlated
EXISTS + GROUP BY/HAVING patterns that ATTACK-derived motifs require.
See [Future Enhancement: Substrait](#future-enhancement-substrait) for
when to revisit.

## Why DataFusion + Parquet

| Concern | PostgreSQL / H2 | DataFusion + Parquet |
|---------|-----------------|---------------------|
| Motif search (EXISTS subqueries) | B-tree index on `motif` column, correlated subquery | Columnar scan of `motif_occurrences`, predicate pushdown on `motif` string column |
| Aggregate queries (motif counts) | COUNT + GROUP BY on normalized table | Columnar aggregation, vectorized filter |
| Storage cost | ~120 bytes/row uncompressed | ~15-25 bytes/row with Snappy, columnar compression |
| Bulk ingest (Lichess dump) | INSERT per row, index maintenance | Write sorted Parquet files offline, register instantly |
| Horizontal scaling | Read replicas, connection pooling | Stateless query over object storage (S3/local) |
| Schema evolution | ALTER TABLE migrations | Schema-on-read, additive columns are free |
| Cold/warm tiering | Manual partition management | Partition by month, drop old partitions = delete files |

The workload is write-once, read-many analytical queries. All motif
queries compile to correlated EXISTS subqueries (or GROUP BY/HAVING
variants) against the `motif_occurrences` table — a narrow, high-volume
table that is the ideal target for columnar storage and predicate
pushdown.

## Architecture

```
┌──────────────────────┐
│   Lichess PGN dump   │
│  (monthly .zst file) │
└──────────┬───────────┘
           │ offline batch (Java CLI)
           ▼
┌──────────────────────────────┐
│  lichess_ingest (Java)       │
│  - streaming PGN parse       │
│  - GM/title filter           │
│  - GameReplayer (chariot)    │   HTTP batch POST
│  - FeatureExtractor          │ ──────────────────┐
│  - all MotifDetectors        │                    │
└──────────────────────────────┘                    │
                                                    │
┌───────────────────────────────────────────────┐   │
│  one_d4 Java API                              │   │
│                                               │   │
│  /v1/index ─► FeatureExtractor ─► SQL INSERT  │   │
│                (unchanged — writes SQL only)   │   │
│                                               │   │
│  ParquetExportJob (weekly/monthly cron)        │   │
│    SELECT from SQL ─────────────────────────┐ │   │
│                                             │ │   │
│  /v1/query ─► Parser ─► ParsedQuery         │ │   │
│                    │                        │ │   │
│    ┌───────────────┤                        │ │   │
│    ▼               ▼                        │ │   │
│  ┌──────────┐  ┌─────────────────────┐      │ │   │
│  │ SqlComp. │  │ DataFusionSqlComp.  │      │ │   │
│  │ → JDBC   │  │ → DataFusion SQL    │      │ │   │
│  └────┬─────┘  └──────┬──────────────┘      │ │   │
│       │               │                     │ │   │
│  game_storage_backends table                │ │   │
│  (tracks which backend has each partition)  │ │   │
│                                             │ │   │
└───────┼───────────────┼─────────────────────┼─┘   │
        │               │                     │     │
        ▼               ▼                     ▼     ▼
┌────────────────┐   ┌──────────────────────────────────┐
│  PostgreSQL/H2 │   │  motif_query (Rust)              │
│  - game_features│   │  - /v1/query                     │
│  - motif_occ   │   │    (DataFusion SQL → LogicalPlan  │
│  - PGN text    │   │     → Parquet scan)               │
│  - index state │   │  - /v1/write                     │
│  - storage meta│   │    (JSON batch → Parquet file)   │
└────────────────┘   └───────────────┬──────────────────┘
                                     │ reads/writes
                                     ▼
                     ┌──────────────────────────────────┐
                     │  Parquet files on disk/S3        │
                     │                                  │
                     │  /data/games/                    │
                     │    game_features/                │
                     │      platform=chess.com/         │
                     │        month=2024-03/            │
                     │          part-0001.parquet       │
                     │    motif_occurrences/            │
                     │      platform=lichess/           │
                     │        month=2024-01/            │
                     │          part-0001.parquet       │
                     │    game_pgns/                    │
                     │      platform=lichess/           │
                     │        month=2024-01/            │
                     │          part-0001.parquet       │
                     └──────────────────────────────────┘
```

### Component Responsibilities

| Component | Language | Role |
|-----------|----------|------|
| `one_d4` (existing) | Java | REST API, indexing (SQL writes), motif detection (chariot), ChessQL parse, query routing, Parquet export job |
| `chessql` (existing lib) | Java | Parser, AST, `SqlCompiler` (existing → PostgreSQL), `DataFusionSqlCompiler` (new → DataFusion SQL) |
| `motif_query` (new) | Rust | DataFusion query engine via SQL, Parquet file writes (batch, not streaming) |
| `lichess_ingest` (new) | Java | Bulk PGN streaming, GM filtering, motif detection (reuses one_d4 detectors), batch POST to motif_query |

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
indexed_at:         TimestampMillisecond
```

PGN text is **not** stored in `game_features` — it bloats columnar scans
and is only needed for game replay, not motif search. For Chess.com games,
PGN stays in PostgreSQL. For Lichess games, PGN is stored in the
`game_pgns` Parquet table (see below).

### `motif_occurrences` table (one row per motif firing)

```
game_url:           Utf8
platform:           Utf8        (partition column)
month:              Utf8        (partition column)
motif:              Utf8        (e.g. "ATTACK", "PIN", "FORK")
ply:                Int32
side:               Utf8        ("white" or "black")
move_number:        Int32
description:        Utf8
attacker:           Utf8        (nullable — piece notation, e.g. "Nf3")
target:             Utf8        (nullable — piece or square, e.g. "Ke8")
is_discovered:      Boolean     (nullable — true for discovered attacks)
is_mate:            Boolean     (nullable — true for checkmate attacks)
```

The `attacker`, `target`, `is_discovered`, and `is_mate` columns are
**load-bearing** for ATTACK-derived motifs. `motif(fork)`, `motif(checkmate)`,
`motif(discovered_attack)`, `motif(discovered_check)`, and
`motif(double_check)` are all derived at query time from `ATTACK` rows
using these columns. Omitting them would break those five motifs in
DataFusion.

### `game_pgns` table (Lichess only — for re-analysis)

```
game_url:           Utf8
platform:           Utf8        (partition column — always "lichess")
month:              Utf8        (partition column)
pgn:                Utf8
```

Stored during initial Lichess ingest to enable re-analysis without
re-downloading the source dump files. Chess.com PGN stays in PostgreSQL.
See [Lichess Games — Parquet-Only Re-Analysis](#lichess-games--parquet-only-re-analysis).

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
  game_pgns/
    platform=lichess/
      month=2024-01/
        part-0000.parquet    # ~500K rows, ~1-2 GB (PGN text)
```

DataFusion's `ListingTable` with `ListingOptions::new(ParquetFormat)` and
`table_partition_cols = ["platform", "month"]` automatically prunes
partitions from `WHERE platform = 'lichess' AND month >= '2024-01'`.

Target file size: ~50 MB uncompressed (~10-15 MB Snappy). This gives good
row group pruning without too many small files.

## ChessQL Dual-Backend Compilation

### Two Compilers, One AST

The ChessQL parser produces a `ParsedQuery` (AST) that is backend-agnostic.
Two compiler implementations produce SQL strings from the same AST:

```
ChessQL string
     │
     ▼
  Parser (Java) → ParsedQuery (AST)
     │
     ├──► SqlCompiler → PostgreSQL SQL + JDBC bind params
     │         (existing path, unchanged)
     │
     └──► DataFusionSqlCompiler → DataFusion SQL string
              (new path, sent to motif_query/v1/query)
```

Both compilers implement `QueryCompiler<CompiledQuery>` (or a
`DataFusionCompiledQuery` variant). The dialect differences are small and
isolated:

| Construct | PostgreSQL SQL (`SqlCompiler`) | DataFusion SQL (`DataFusionSqlCompiler`) |
|-----------|-------------------------------|------------------------------------------|
| Bind parameters | `?` (JDBC positional) | Inline literals (DataFusion SQL parser does not use JDBC) |
| String equality | `LOWER(col) = LOWER(?)` | `lower(col) = lower('value')` |
| Timestamp literals | `CAST(? AS TIMESTAMP)` | `CAST('...' AS TIMESTAMP)` |
| EXISTS subqueries | Standard SQL — supported | Standard SQL — supported |
| GROUP BY / HAVING | Standard SQL — supported | Standard SQL — supported |
| Correlated EXISTS | Standard SQL — supported | Standard SQL — supported |
| `sequence()` self-JOINs | Standard SQL — supported | Standard SQL — supported |

DataFusion's SQL dialect is ANSI-compatible and handles the full query
surface of ChessQL including correlated EXISTS, GROUP BY/HAVING, and
arithmetic join keys (`sq2.ply = sq1.ply + 2`). These patterns are
well-exercised in DataFusion's SQL planner and do not require the
correlated subquery extensions that limit the Substrait toolchain.

### DataFusionSqlCompiler Implementation

New class: `DataFusionSqlCompiler implements QueryCompiler<DataFusionCompiledQuery>`
in the `chessql` library. It walks the same ChessQL AST as `SqlCompiler`
but produces DataFusion SQL with inline literals:

```java
public class DataFusionSqlCompiler implements QueryCompiler<DataFusionCompiledQuery> {

  @Override
  public DataFusionCompiledQuery compile(ParsedQuery pq) {
    String whereClause = compileExpr(pq.expr());

    OrderByClause orderBy = pq.orderBy();
    if (orderBy != null) {
      // Build LEFT JOIN + COUNT aggregate, same structure as SqlCompiler
      // Difference: inline literals, DataFusion function names
      ...
    }
    String sql = "SELECT g.* FROM game_features g WHERE " + whereClause
        + " ORDER BY g.played_at DESC";
    return new DataFusionCompiledQuery(sql);
  }

  private String compileMotif(MotifExpr motif) {
    return switch (motif.motifName()) {
      case "fork" ->
          "EXISTS (SELECT 1 FROM motif_occurrences mo"
              + " WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'"
              + " AND mo.is_discovered = false AND mo.attacker IS NOT NULL"
              + " GROUP BY mo.ply, mo.attacker HAVING COUNT(*) >= 2)";
      // ... same structure as SqlCompiler for all other motifs
    };
  }
}
```

The implementation mirrors `SqlCompiler` closely. When a new motif or
field is added to ChessQL, both compilers are updated together — guarded
by the contract test suite (see below).

### Contract Test Suite

Before routing any real queries to DataFusion, a contract test suite
validates result equivalence between backends:

- Seeds both PostgreSQL (H2 in-memory) and DataFusion (test Parquet files)
  with identical game + motif occurrence data
- Runs every ChessQL pattern against both backends:
  - All 16 motifs (`motif(pin)`, `motif(fork)`, `motif(double_check)`, ...)
  - `sequence(fork THEN pin)`, `sequence(checkmate THEN promotion)`
  - `ORDER BY motif_count(fork) DESC`
  - `AND`, `OR`, `NOT` combinations
  - `IN` expressions, numeric comparisons, string equality
- Asserts result-set equivalence (same game_urls, same ordering)
- Runs in CI on every change to `chessql` or `motif_query`

This is the structural safety net that makes a dual-compiler system
maintainable. A test failure in the contract suite flags a divergence
before it reaches production.

### Query Routing

The `QueryController` parses ChessQL once and routes based on storage
metadata:

```java
@POST
public QueryResponse query(QueryRequest request) {
  ParsedQuery parsed = Parser.parse(request.query());
  List<GameFeature> rows = queryRouter.route(parsed, request.limit(), request.offset());
  // ... format response
}
```

The `StorageAwareQueryRouter` uses time-based routing for Phase 1
(simplest, avoids fan-out):

```java
public class StorageAwareQueryRouter implements QueryRouter {

  List<GameFeature> route(ParsedQuery parsed, int limit, int offset) {
    // Phase 1: time-based shortcut
    // Current (incomplete) month → SQL; completed months → DataFusion
    if (queryTouchesCurrentMonth(parsed)) {
      CompiledQuery q = sqlCompiler.compile(parsed);
      return sqlDao.query(q, limit, offset);
    }
    DataFusionCompiledQuery q = dataFusionSqlCompiler.compile(parsed);
    return dataFusionClient.query(q, limit, offset);
  }
}
```

**Phase 1 routing guarantee:** queries are eventually consistent.
Recently indexed Chess.com games appear in SQL-backed results immediately
and in DataFusion-backed results after the next export. Lichess data
appears in DataFusion only (there is no "current month" for Lichess since
ingest is always after month-end).

**Phase 2 routing (partition-level):** after shadow mode validation, use
`game_storage_backends` to decide per-partition:

| `backend` | `parquet_stale` | Query route |
|-----------|-----------------|-------------|
| `sql` | n/a | SQL |
| `parquet` | `false` | DataFusion |
| `parquet` | `true` | DataFusion (stale warning logged) |
| `both` | `false` | DataFusion |
| `both` | `true` | SQL (until re-export) |

The fan-out (mixed) case — some partitions in SQL, some in Parquet — is
deliberately deferred. Merging paginated results and `ORDER BY motif_count()`
aggregates across two backends requires a federated query coordinator.
Time-based routing avoids this entirely for the common case. If the need
arises, it can be added in a follow-on phase.

## motif_query Service

### Crate Structure

```
domains/games/apis/motif_query/
  Cargo.toml
  src/
    main.rs              # axum server + DataFusion setup
    catalog.rs           # Table registration, partition discovery
    query.rs             # Query execution, result serialization
    writer.rs            # Parquet file writing (accepts complete batches)
  BUILD.bazel
```

Note: no chess logic, PGN parsing, buffering, or compaction in Rust.
Motif detection and Lichess ingest live in Java. The write endpoint
receives complete batches (from the export job or Lichess ingest) and
writes one Parquet file per call — no in-memory buffer needed.

### Dependencies

```toml
[dependencies]
arrow = { version = "57" }
datafusion = { version = "52" }
parquet = { version = "57", features = ["snap"] }
axum = { workspace = true }
tokio = { workspace = true }
serde = { workspace = true }
serde_json = { workspace = true }
server_pal = { workspace = true }
uuid = { workspace = true }
chrono = { workspace = true }
```

### API Endpoints

```
POST /motif_query/v1/query
  Body: { "sql": "SELECT g.* FROM game_features g WHERE ...", "limit": 50, "offset": 0 }
  Content-Type: application/json
  Response: { "rows": [...], "row_count": N }

POST /motif_query/v1/write
  Body: { "table": "game_features", "platform": "chess.com", "month": "2024-03", "rows": [...] }
  Response: { "file": "part-0001.parquet", "rows_written": 8472 }
  Note: writes one Parquet file per call — no buffering, caller sends complete batch

GET /motif_query/v1/partitions
  Response: { "game_features": ["chess.com/2024-03", ...], "motif_occurrences": [...] }

GET /health
  Response: 200 OK
```

The query endpoint accepts a DataFusion SQL string produced by
`DataFusionSqlCompiler`. DataFusion parses and plans this against
registered Parquet listing tables. No SQL strings cross the wire as
user-controlled input — the SQL is always compiler-generated from a
validated ChessQL AST.

### DataFusion Setup + Query Execution

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

    // Register motif_occurrences and game_pgns similarly
    // ...

    ctx
}

/// Execute a DataFusion SQL query produced by DataFusionSqlCompiler.
async fn execute_sql(
    ctx: &SessionContext,
    sql: &str,
) -> datafusion::error::Result<Vec<RecordBatch>> {
    let df = ctx.sql(sql).await?;
    df.collect().await
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

### Motif Detection — Java Only (chariot)

Motif detection stays in Java. The current detectors use chariot
(`io.github.tors42:chariot`) via `GameReplayer` for board state and FEN
generation. Issue #1049 (Phase 9) plans to deepen the chariot integration:

- **Check attribution**: distinguish promoted-piece check vs discovered
  check vs double check, requiring chariot's board model to identify which
  piece delivers the check.
- **New motifs**: back rank mate, smothered mate, zugzwang, double check,
  overloaded piece — several of these need full board state analysis that
  goes well beyond FEN string parsing.
- **Re-analysis pipeline**: admin endpoint to reprocess existing games with
  new/improved detectors.

Porting detectors to Rust would mean duplicating all of this work against
a different chess library (`shakmaty`), maintaining two implementations in
lockstep, and losing the chariot-specific APIs that Phase 9 depends on.
Not worth it.

**The Lichess ingest pipeline therefore runs in Java**, not Rust. The Rust
`motif_query` service handles only Parquet writes and DataFusion queries —
no chess logic.

### Lichess Ingest Architecture (Java)

The bulk ingest is a standalone Java CLI jar:

```
java -jar lichess_ingest.jar \
  --input lichess_db_standard_rated_2024-01.pgn.zst \
  --motif-query-url http://localhost:8081 \
  --min-elo 2200 \
  --titles GM,IM,WGM,WIM \
  --time-controls standard,rapid,blitz \
  --batch-size 1000
```

A standalone CLI jar keeps the long-running bulk workload isolated from
the API server. It shares the same motif detection code via library
targets and can run as a cron job or one-off invocation.

### Ingest Pipeline Steps

```
1. Download/read .pgn.zst file (streaming via zstd-jni)
     ↓
2. Parse PGN headers (extract players, Elo, title, time control, result)
     ↓
3. Filter: does this game meet GM/titled criteria?
     ↓  (skip ~97% of games here, before parsing moves)
4. Parse movetext for qualifying games
     ↓
5. GameReplayer + FeatureExtractor + MotifDetectors (Java, chariot)
     ↓
6. Batch results (1000 games)
     ↓
7a. POST batch to motif_query/v1/write → game_features Parquet
7b. POST batch to motif_query/v1/write → motif_occurrences Parquet
7c. POST batch to motif_query/v1/write → game_pgns Parquet  ← new
     ↓
8. motif_query writes partitioned Parquet files
```

Step 7c writes PGN to a separate `game_pgns` Parquet table during initial
ingest. The incremental cost is ~2-4 GB/month of Parquet storage (PGN
compresses well with dictionary encoding). This enables re-analysis
without re-downloading source dumps (see below).

### Performance Considerations

Java motif detection is slower than a hypothetical Rust implementation,
but still fast enough for the filtered dataset:

| Operation | Estimated throughput |
|-----------|---------------------|
| PGN header parse + filter (Java, streaming) | ~100-200K games/sec |
| Full motif analysis (chariot + detectors) | ~2-5K games/sec |
| HTTP batch write to motif_query | ~10K rows/sec |
| End-to-end: 1 month Lichess (100M games, ~2M qualifying) | ~15-30 minutes |

15-30 minutes per monthly dump is acceptable for a batch job. If it
becomes a bottleneck, parallelize across multiple worker threads — the
PGN stream can be partitioned by game boundaries.

## Integration with one_d4 Java API

### Indexing Flow — Unchanged

The `IndexWorker` continues to write to SQL exactly as it does today.
**No changes to the index path.** Chess.com games flow through the
existing pipeline:

```
IndexWorker.process(message)
  for each month in [startMonth, endMonth]:
    games = ChessClient.fetchGames(player, month)  // 10-100 games per player-month
    for each game in games:
      features = FeatureExtractor.extract(game.pgn())
      GameFeatureDao.insert(row)                    // SQL INSERT — unchanged
      GameFeatureDao.insertOccurrences(...)          // SQL batch — unchanged
```

SQL handles this write rate trivially. At 10K-100K games/month
(~300-3,300 games/day), each INSERT is ~1ms and the Chess.com API is
the bottleneck (~200ms/request). No reason to complicate the write
path with real-time Parquet writes, buffering, or dual-write logic.

### Two Write Paths, Not One

Instead of making the indexer write to Parquet in real-time, we separate
the write paths by data source:

| Data source | Write target | When | How |
|-------------|-------------|------|-----|
| Chess.com (index requests) | SQL | Real-time (on each index) | Existing `IndexWorker` — unchanged |
| Chess.com (Parquet export) | Parquet | Periodic batch (weekly/monthly) | New export job: `SELECT` from SQL → write Parquet |
| Lichess (bulk ingest) | Parquet | One-time + monthly cron | `lichess_ingest` Java CLI → `motif_query /v1/write` |

This means:

1. **Fresh Chess.com games live in SQL first.** Queries against recently
   indexed games hit PostgreSQL. This is the current behavior — no
   regression.
2. **A periodic batch job exports SQL → Parquet.** Once exported, those
   games become queryable via DataFusion. The batch job produces clean,
   well-sized Parquet files in a single pass — no buffering, no
   compaction, no small-file problem.
3. **Lichess games go straight to Parquet.** No SQL intermediate. The
   bulk ingest job writes large files directly — the natural batch
   boundary is one month of Lichess data.

### Periodic Parquet Export Job

A batch job (cron or admin-triggered) exports Chess.com game data from
SQL to Parquet:

```
ParquetExportJob (runs weekly or monthly)

for each (platform, month) partition needing export or re-export:
  rows = SELECT * FROM game_features WHERE platform = ? AND month = ?
  occurrences = SELECT * FROM motif_occurrences WHERE platform = ? AND month = ?

  POST motif_query/v1/write { "table": "game_features", rows }
  POST motif_query/v1/write { "table": "motif_occurrences", occurrences }

  UPDATE game_storage_backends
    SET backend = 'both',
        parquet_exported_at = now(),
        parquet_stale = false
    WHERE platform = ? AND month = ?
```

The export job reads the **complete partition** in one SELECT and writes
exactly one Parquet file per partition — a full overwrite of any previous
file. No partial writes, no "already exported" per-game tracking, no
compaction. A partition needs re-export when any of its games have
`indexed_at > parquet_exported_at` (staleness check) or when
`parquet_stale = true` (re-analysis).

#### Export Job Characteristics

| Property | Value |
|----------|-------|
| Frequency | Weekly or monthly (configurable) |
| Batch size | All games for a (platform, month) partition |
| Rows per export | 1K-10K per partition (10K-100K games/month ÷ ~12 months) |
| Parquet file size | ~50 KB - 500 KB per partition (Snappy compressed) |
| Files per partition | **Exactly 1** (full partition written in one pass) |
| Duration | Seconds (SQL SELECT is fast, Parquet write is fast) |
| Compaction needed | **No** — one file per partition, no small-file problem |

#### Re-export After Re-analysis

When new motif detectors are added (issue #1049 Phase 9), an admin
endpoint triggers re-analysis of existing games. After re-analysis
updates the SQL rows, the export job re-runs for affected partitions:

```
1. Admin triggers re-analysis → SQL rows updated with new motif flags
2. game_storage_backends.parquet_stale = true for affected partitions
3. Export job detects stale partitions (parquet_stale = true)
4. Re-export: read all rows for partition → overwrite Parquet file
5. Update game_storage_backends.parquet_stale = false
```

Since we write one file per partition, re-export is a simple overwrite —
no merge logic needed.

### Re-Analysis and Schema Evolution

#### Step 1: Schema Evolution

New motif columns must be added to both stores:

**SQL:**
```sql
ALTER TABLE game_features ADD COLUMN has_back_rank_mate BOOLEAN DEFAULT FALSE;
-- ... repeat for each new motif
```

**Parquet:** No migration needed. Parquet is schema-on-read — the next
export/ingest writes files with the new columns. Old Parquet files
missing the column return NULL, which DataFusion treats as FALSE for
boolean filters. New Parquet files include the column with real values.

**Code changes:**
- `GameFeatureDao` INSERT/MERGE statements: add new columns
- `GameFeature` record: add new fields
- `FeatureExtractor` / `MotifDetector`: add new detectors
- `SqlCompiler.VALID_MOTIFS`: add new motif names
- `DataFusionSqlCompiler.VALID_MOTIFS`: add new motif names
- Parquet schema in `motif_query` `catalog.rs`: add new columns

#### Step 2: Re-Analysis Pipeline (Chess.com games in SQL)

```
POST /admin/reanalyze
  Query params: ?motifs=back_rank_mate,smothered_mate
                &platform=chess.com
                &months=2024-01,2024-02,...  (optional, default=all)

Pipeline:
  for each (platform, month) in scope:
    batch = SELECT game_url, pgn FROM game_features
            WHERE platform = ? AND month = ?
            ORDER BY played_at
            LIMIT 1000 OFFSET ?

    for each game in batch:
      features = featureExtractor.extract(game.pgn())
      UPDATE game_features
        SET has_back_rank_mate = ?,
            has_smothered_mate = ?,
            ...,
            indexed_at = now()
        WHERE game_url = ?

      // Re-insert occurrences for new motifs
      DELETE FROM motif_occurrences
        WHERE game_url = ? AND motif IN ('BACK_RANK_MATE', 'SMOTHERED_MATE', ...)
      INSERT INTO motif_occurrences ...

    // Mark partition as needing re-export
    UPDATE game_storage_backends
      SET parquet_stale = true
      WHERE platform = ? AND month = ?
```

#### Lichess Games — Parquet-Only Re-Analysis

Lichess games are `backend='parquet'` — they were never in SQL and there
is no PGN to re-read from the database. Re-analysis reads PGN from the
`game_pgns` Parquet table:

```
POST /admin/reanalyze/lichess
  Query params: ?motifs=back_rank_mate&months=2024-01,...

Pipeline:
  for each (platform, month) in scope:
    batch = DataFusion: SELECT game_url, pgn FROM game_pgns
            WHERE platform = 'lichess' AND month = ?
            LIMIT 1000 OFFSET ?

    for each game in batch:
      features = featureExtractor.extract(game.pgn())
      // Write updated game_features and motif_occurrences to Parquet
      // Overwrite the affected partition file
```

This is fast (minutes per partition, no download needed) and does not
require any SQL involvement for Lichess data.

**Why not re-ingest from the Lichess dump (Option A)?** With 36 months
of history, re-analysis would require downloading ~700 GB of compressed
PGN, decompressing ~3.6B games, and filtering for ~60-100M qualifying
games — 18-45 hours of wall-clock time. Storing PGN in a dedicated
`game_pgns` Parquet table during initial ingest (~2-4 GB/month, stored
once) eliminates this cost entirely.

### Storage Routing — `game_storage_backends` Table

```sql
CREATE TABLE game_storage_backends (
  platform            TEXT NOT NULL,
  month               TEXT NOT NULL,      -- "2024-03"
  backend             TEXT NOT NULL,      -- 'sql', 'parquet', 'both'
  games_in_sql        INTEGER DEFAULT 0,
  games_in_parquet    INTEGER DEFAULT 0,
  parquet_exported_at TIMESTAMP,
  parquet_stale       BOOLEAN DEFAULT FALSE,  -- true after re-analysis
  last_reanalyzed_at  TIMESTAMP,               -- when re-analysis last ran
  PRIMARY KEY (platform, month)
);
```

The query router uses this table for Phase 2 routing (partition-level).
Phase 1 uses time-based routing (current month → SQL, completed months →
DataFusion) and does not require this table for routing decisions.

### PGN and Game Metadata — SQL Only (Chess.com)

| Data | Storage | Reason |
|------|---------|--------|
| Motif queries (`motif(pin)`, `motif(fork)`, ...) | SQL EXISTS subqueries + Parquet join (after export) | Analytical queries |
| Game metadata (Elo, ECO, result, ...) | SQL + Parquet (after export) | Needed for ChessQL filters |
| PGN text (Chess.com) | SQL only | Large, variable-length, not scanned by ChessQL |
| PGN text (Lichess) | `game_pgns` Parquet table | Needed for re-analysis without re-download |
| Motif occurrences (ply, side, attacker, ...) | SQL + Parquet (after export) | Needed for derived motifs and `sequence()` |
| `indexing_requests` | SQL only | Mutable operational state |
| `indexed_periods` | SQL only | Mutable cache |
| `game_storage_backends` | SQL only | Routing metadata |

### Query Flow

```
POST /v1/query { "query": "motif(fork) AND white.elo >= 2500" }
  ↓
one_d4 QueryController
  ↓
Parser.parse() → ParsedQuery (AST)
  ↓
StorageAwareQueryRouter.route(parsed)
  │
  ├─ Phase 1: time-based check
  │     current month? → SqlCompiler → JDBC → PostgreSQL → ResultSet
  │
  └─ completed months? → DataFusionSqlCompiler → SQL string
        → POST motif_query/v1/query { "sql": "...", "limit": 50 }
        → DataFusion optimizer → PhysicalPlan → Parquet scan
        → JSON results back to one_d4
  ↓
one_d4 formats response, returns to client
```

### Why This Is Simpler Than Real-Time Parquet Writes

| Concern | Real-time Parquet writes | SQL-first + periodic export |
|---------|------------------------|---------------------------|
| IndexWorker changes | New HTTP client, batching, error handling | **None** |
| Parquet buffering | In-memory buffer, flush thresholds, crash recovery | **None** — export job writes complete files |
| Small-file problem | Many small files from trickle writes | **None** — one file per partition per export |
| Compaction | Background job to merge small files | **Not needed** |
| Read-your-writes | Buffered rows invisible until flush | **Not an issue** — SQL is always current |
| Dual-write consistency | SQL + Parquet must agree during migration | **Not an issue** — SQL is source of truth, Parquet is derived |
| motif_query complexity | Buffer management, flush endpoint, compaction | **Query engine only** (+ simple write endpoint for Lichess) |
| Data freshness in Parquet | Seconds-minutes (buffer flush lag) | Weekly/monthly (export schedule) |

The only tradeoff is data freshness in Parquet — recently indexed
Chess.com games are SQL-only until the next export runs. The SQL backend
handles these queries today, and the export frequency is configurable.
For users who just indexed their games, queries hit SQL immediately.

## Do We Keep H2/PostgreSQL?

**Short answer: Yes — PostgreSQL is the primary write store for Chess.com
games and keeps all data that Parquet doesn't need.**

### What lives where

| Data | PostgreSQL | Parquet | Notes |
|------|-----------|---------|-------|
| `game_features` (motif flags, metadata) | Always (source of truth for Chess.com) | After export job / Lichess ingest | Parquet is derived for Chess.com, primary for Lichess |
| `motif_occurrences` | Always (source of truth for Chess.com) | After export job / Lichess ingest | Same as above |
| PGN text (Chess.com) | Always | Never | Large, variable-length, not needed for analytical queries |
| PGN text (Lichess) | Never | `game_pgns` table | For re-analysis; Chess.com PGN stays in SQL |
| `indexing_requests` | Always | Never | Mutable operational state |
| `indexed_periods` | Always | Never | Mutable cache of fetched (player, platform, month) combos |
| `game_storage_backends` | Always | Never | Routing metadata |

### When Can We Drop SQL Game Tables?

With the SQL-first approach, **we don't drop them** — PostgreSQL remains
the write store for Chess.com games and the source of PGN text. The
analytical query load shifts to DataFusion/Parquet for historical data,
but the tables stay.

If we later want to stop storing Chess.com game data in SQL (e.g. to
reduce PostgreSQL storage), we'd need to solve PGN storage separately
(object storage or a dedicated Parquet table). That's a future
optimization, not a near-term goal.

### Migration Path

```
Phase 1: DataFusionSqlCompiler + motif_query crate scaffold.
         DataFusionSqlCompiler produces DataFusion SQL from the same
         ChessQL AST as SqlCompiler. motif_query Rust service accepts
         DataFusion SQL strings, queries Parquet, returns JSON.
         Contract tests validate result equivalence with PostgreSQL.
         IndexWorker is unchanged — still writes to SQL.

Phase 2: Parquet export job + time-based routing.
         Export job runs weekly/monthly: SELECT from SQL → write
         Parquet via motif_query/v1/write. game_storage_backends
         tracks which partitions have Parquet data.
         StorageAwareQueryRouter dispatches using time-based rule:
         current month → SqlCompiler + JDBC; older months → DataFusion.

Phase 3: Shadow mode validation.
         For partitions with Parquet data, run queries on both
         backends in parallel, compare results, log mismatches.
         SQL results are returned to client.

Phase 4: DataFusion primary for exported partitions.
         After shadow mode shows 100% result parity, queries for
         completed months route to DataFusion. Only current-month
         (unexported) queries hit SQL.

Phase 5: Lichess bulk ingest (after Phase 9 / issue #1049 lands).
         Run lichess_ingest Java CLI on historical Lichess dumps.
         All 16 motif detectors are included in bulk processing.
         Lichess partitions are 'parquet' only.
         game_pgns Parquet table written during ingest for future
         re-analysis.
```

## Implementation Plan

### Phase 1: DataFusionSqlCompiler + motif_query scaffold (3-4 days)

**Java — DataFusionSqlCompiler in chessql library:**
- New `DataFusionSqlCompiler implements QueryCompiler<DataFusionCompiledQuery>`
  - Walk ChessQL AST → DataFusion SQL with inline literals
  - Handle all 16 motifs: stored motifs by name, ATTACK-derived motifs
    by the same GROUP BY / HAVING patterns as `SqlCompiler`
  - Handle `SequenceExpr` → correlated EXISTS + JOIN
  - Handle `OrderByClause` → LEFT JOIN + COUNT aggregate + ORDER BY
  - Handle `InExpr`, `ComparisonExpr` → inline literals (no bind params)
  - String case-insensitivity via `lower()` inline
- Unit tests: `DataFusionSqlCompilerTest` — verify SQL output for each
  AST node type, including all ATTACK-derived motifs and `sequence()`
- Keep `SqlCompiler` and all existing tests passing — no changes to
  current production path

**Rust — motif_query crate:**
- Create `domains/games/apis/motif_query/` with Cargo.toml, BUILD.bazel
- Add `datafusion`, `arrow`, `parquet` workspace dependencies
- Implement `catalog.rs`: register `game_features`, `motif_occurrences`,
  `game_pgns` as Parquet listing tables with partition columns
  (include `attacker`, `target`, `is_discovered`, `is_mate` in
  `motif_occurrences` schema)
- Implement `query.rs`: accept `{ "sql": "...", "limit": N, "offset": N }`,
  execute via `ctx.sql()`, return JSON rows
- Implement `writer.rs`: accept JSON batch, write directly to partitioned
  Parquet (no buffering needed — callers send complete batches)
- axum server with `/v1/query`, `/v1/write`, `/v1/partitions`, `/health`
- Unit tests: in-memory Parquet roundtrips for all derived motif patterns
  (especially fork, double_check, sequence)

**Contract tests (critical):**
- `DataFusionContractTest` in one_d4 test suite
- Seeds H2 (in-memory) and test Parquet files with identical data
- Runs all 16 motifs + sequence + ORDER BY + IN expressions against both
- Asserts result-set equivalence
- Runs in CI via Bazel

### Phase 2: Parquet export job + time-based routing (2-3 days)

- New `game_storage_backends` SQL table + DAO
- New `ParquetExportJob`:
  - SELECT all `game_features` + `motif_occurrences` for each (platform, month)
    partition that has `indexed_at > parquet_exported_at` or `parquet_stale = true`
  - POST complete partition to `motif_query/v1/write` (full overwrite, no
    per-game tracking)
  - UPDATE `game_storage_backends` to `backend='both'`, `parquet_stale=false`,
    `parquet_exported_at=now()`
- Admin endpoint: `POST /admin/export/parquet?platform=X&month=Y`
- Cron scheduling: weekly or monthly via config
- `StorageAwareQueryRouter` (Phase 1 implementation):
  - Time-based: is the query asking for only the current month?
    → SqlCompiler + JDBC; otherwise → DataFusionSqlCompiler + motif_query
- `DataFusionQueryClient`: serialize compiled SQL → HTTP POST to
  `motif_query/v1/query` → deserialize JSON response

### Phase 3: Shadow mode + partition-level routing (1-2 days)

- For completed partitions (backend='both'), run both backends, compare
  results, log mismatches to a `query_shadow_mismatches` table
- After N days with zero mismatches, enable DataFusion as primary for
  those partitions
- Upgrade `StorageAwareQueryRouter` to use `game_storage_backends` lookup
  instead of time-based shortcut

### Phase 4: Lichess ingest pipeline — Java (3-5 days)

- New `lichess_ingest` binary target in `domains/games/apis/one_d4/`
- Streaming PGN parser: read `.pgn.zst` via `zstd-jni`, extract headers
- GM/title filter: parse `WhiteTitle`/`BlackTitle` headers, Elo thresholds
- Reuse existing Java motif detectors (GameReplayer + FeatureExtractor +
  all MotifDetector implementations — no porting needed)
- HTTP client to batch-POST results to `motif_query/v1/write`
  (game_features, motif_occurrences, and game_pgns in separate calls)
- CLI interface: `java -jar lichess_ingest.jar --input ... --motif-query-url ...`
- Insert `game_storage_backends` rows with `backend='parquet'` for Lichess
  partitions
- Test against a small Lichess sample file
- **Dependency:** Should run after Phase 9 (issue #1049) lands so that
  all chariot-based motifs are included in the bulk ingest

### Phase 5: Remove SqlCompiler (after shadow mode proves parity)

- All compilation goes through `DataFusionSqlCompiler` for DataFusion
  queries; `SqlCompiler` stays for the PostgreSQL path indefinitely
- Remove `USE_DATAFUSION` feature flag (always on for completed partitions)

## Cost and Performance Estimates

### Storage

| Dataset | PostgreSQL | Parquet (Snappy) |
|---------|-----------|-----------------|
| 1 month Chess.com (50K games) | ~40 MB | ~2.5-5 MB |
| 12 months Chess.com (600K games) | ~480 MB | ~30-60 MB |
| 12 months Lichess GM games (~20M) | ~16 GB | ~1.5-2.5 GB |
| Lichess game_pgns (12 months) | — | ~24-48 GB (PGN text) |
| Combined (12 mo Chess.com + Lichess) | ~16.5 GB | ~26-51 GB |

### Query Performance (estimated, single-node)

All motif queries execute as EXISTS subqueries or GROUP BY/HAVING on
`motif_occurrences`. The DataFusion advantage comes from columnar scan
and predicate pushdown on the `motif` string column, not from boolean
flag scans.

| Query pattern | PostgreSQL (indexed) | DataFusion (Parquet) |
|--------------|---------------------|---------------------|
| `motif(pin)` (EXISTS) | ~50-200ms (index on motif column) | ~20-80ms (column pruning + predicate pushdown) |
| `motif(fork) AND motif(pin)` (2 EXISTS) | ~100-300ms | ~15-50ms (parallel column scans) |
| `ORDER BY motif_count(check)` | ~500ms+ (JOIN + COUNT) | ~100-300ms (columnar aggregation) |
| Full scan, no filters | ~2-5s (1M rows) | ~200-500ms (columnar, compressed) |

### Ingest Performance

| Operation | Estimated throughput |
|-----------|---------------------|
| Lichess PGN header parse + filter (Java, streaming) | ~100-200K games/sec |
| Full motif analysis (chariot + Java detectors) | ~2-5K games/sec |
| HTTP batch write to motif_query | ~10K rows/sec |
| End-to-end: 1 month Lichess (100M games, ~2M qualifying) | ~15-30 minutes |

## Open Questions

1. **Object storage vs local disk?** Starting with local disk is simpler.
   DataFusion supports `object_store` for S3/GCS — add it later when
   deploying to cloud.

2. **Do we need the motif_occurrences table in Parquet for Phase 1?** For
   most ChessQL queries, only `game_features` is needed. The occurrences
   table is required for `sequence()` and `ORDER BY motif_count()`. If
   those features are used infrequently, they could stay SQL-only while
   the rest migrates to DataFusion. Export `motif_occurrences` in Phase 2
   alongside `game_features`.

3. **Phase 9 before or after Lichess ingest?** Ideally Phase 9 (issue
   #1049 — chariot-based motifs) lands before the first Lichess bulk
   ingest, so all 16 motifs are captured in one pass. If Phase 9 is
   delayed, we could ingest with the current detectors and re-analyze
   later using the `game_pgns` Parquet table, but that doubles processing
   cost.

4. **Java Parquet writer alternative?** Instead of HTTP POST to the Rust
   `motif_query` service, the export job and `lichess_ingest` CLI could
   write Parquet directly using `org.apache.parquet:parquet-avro` or
   `org.apache.arrow:arrow-dataset`. This eliminates the HTTP hop but
   means two codepaths for Parquet writing (Java for writes, Rust for
   reads). The Rust write endpoint is simpler to keep consistent with
   the query schema.

5. **Export frequency — weekly vs monthly?** Weekly exports mean fresher
   Parquet data and faster query routing to DataFusion for recent games.
   Monthly exports are simpler and produce cleaner partition boundaries.
   At 10K-100K games/month, the difference is small. Could also be
   event-driven: export when a partition reaches N games or on admin
   trigger.

6. **Mixed-backend query fan-out** (deferred): When a query spans the
   current month (SQL) and older months (DataFusion), the router must
   either route entirely to SQL or fan out to both and merge. For Phase 1,
   time-based routing sends the whole query to SQL when the current month
   is involved. This is correct but means historical DataFusion data is
   not queried in the same call as current data. Revisit in Phase 3 once
   shadow mode validates result parity.

7. **game_pgns storage cost**: At ~2-4 GB/month of Parquet, 36 months of
   Lichess history ≈ 72-144 GB. This is acceptable and much cheaper than
   re-downloading the raw dumps. Monitor actual PGN compression ratios
   during the first Lichess ingest.

## Future Enhancement: Substrait

The current design uses two SQL compilers (PostgreSQL and DataFusion
dialects) rather than a shared Substrait IR. This is a deliberate
trade-off based on the state of the Substrait toolchain in early 2026.

**Why Substrait was deferred:**

The `sequence()` and ATTACK-derived motif patterns require correlated
EXISTS subqueries with GROUP BY/HAVING inside the correlated scope:

```sql
-- motif(fork): correlated EXISTS over an aggregate
EXISTS (SELECT 1 FROM motif_occurrences mo
  WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK'
  AND mo.is_discovered = FALSE AND mo.attacker IS NOT NULL
  GROUP BY mo.ply, mo.attacker HAVING COUNT(*) >= 2)
```

In Substrait terms, this requires `SetPredicateRel(EXISTS,
AggregateRel(...))` where the inner scan is correlated to the outer
relation. `datafusion-substrait` 52.x still explicitly documents that
"Substrait does not (yet) support the full range of plans and expressions
that DataFusion offers" — correlated EXISTS with aggregates is one of the
known gaps. DataFusion decorrelates subqueries during planning, which
works for simple EXISTS but has known limitations when the correlated
scope contains GROUP BY/HAVING.

Additionally, `substrait-java`'s `SubstraitToSql` produces ANSI SQL with
inline literals rather than JDBC `?` bind parameters. Replacing
`SqlCompiler`'s parameterized queries with Substrait-generated SQL would
be a regression in query safety without significant work to re-extract
parameters.

**When to revisit:**

Consider Substrait as the query IR when:

1. `datafusion-substrait` (currently 52.x) removes its "does not yet
   support the full range of plans" caveat and demonstrates reliable
   support for correlated EXISTS + GROUP BY/HAVING. Test against all 5
   ATTACK-derived motifs and `sequence()` before adopting.
2. A third query backend is needed (e.g. DuckDB, ClickHouse, Velox),
   making two-compiler maintenance genuinely costly.
3. The `substrait-java` `isthmus` `SubstraitToSql` produces
   parameterized output (or a safe inline-literal model is standardized).

**Migration path if/when Substrait is adopted:**

```
1. Spike: manually construct Substrait protobuf for motif(fork) and
   sequence(fork THEN pin). Verify round-trip through substrait-java
   SubstraitToSql (PostgreSQL path) and datafusion-substrait
   from_substrait_plan (DataFusion path). Both must produce correct
   results against test data.

2. Add SubstraitCompiler alongside SqlCompiler and
   DataFusionSqlCompiler. Route a small percentage of queries through
   all three, compare results.

3. Once SubstraitCompiler achieves parity on all query patterns,
   deprecate SqlCompiler and DataFusionSqlCompiler in favor of the
   single Substrait compilation path.

4. Benefits realized: one compilation path, backend portability, cleaner
   optimizer visibility into query structure.
```

The two-compiler approach with contract tests is explicitly designed to
be replaced by Substrait when the toolchain matures without requiring
a big-bang migration.
