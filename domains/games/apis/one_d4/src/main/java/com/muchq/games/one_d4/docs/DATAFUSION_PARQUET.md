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

**Query IR: Substrait.** ChessQL compiles to [Substrait](https://substrait.io/)
relational algebra plans (protobuf) instead of SQL strings. A `QueryRouter`
dispatches Substrait plans to either PostgreSQL (via `substrait-java`
SQL conversion) or DataFusion (via `datafusion-substrait` plan consumer),
based on a feature flag, storage metadata, or cost-based routing. This
gives us backend portability without maintaining dialect-specific SQL
compilers.

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
│  /v1/query ─► SubstraitCompiler ─► Plan     │ │   │
│                    │                        │ │   │
│    ┌───────────────┤                        │ │   │
│    ▼               ▼                        │ │   │
│  ┌──────────┐  ┌──────────────┐             │ │   │
│  │ SQL      │  │ DataFusion   │             │ │   │
│  │ Router   │  │ Router       │             │ │   │
│  └────┬─────┘  └──────┬───────┘             │ │   │
│       │               │                     │ │   │
│  game_storage_backends table                │ │   │
│  (tracks which backend has each partition)  │ │   │
│                                             │ │   │
└───────┼───────────────┼─────────────────────┼─┘   │
        │               │                     │     │
        ▼               ▼                     ▼     ▼
┌────────────────┐   ┌──────────────────────────────────┐
│  PostgreSQL/H2 │   │  motif_query (Rust)              │
│  - game_features│   │  - /v1/query/substrait           │
│  - motif_occ   │   │    (datafusion-substrait →       │
│  - PGN text    │   │     LogicalPlan → Parquet scan)  │
│  - index state │   │  - /v1/write                     │
│  - storage meta│   │    (JSON batch → Parquet file)   │
└────────────────┘   └───────────────┬──────────────────┘
                                     │ reads/writes
                                     ▼
                     ┌──────────────────────────────────┐
                     │  Parquet files on disk/S3        │
                     │                                  │
                     │  /data/games/                    │
                     │    platform=chess.com/           │
                     │      month=2024-03/             │
                     │        part-0001.parquet         │
                     │    platform=lichess/             │
                     │      month=2024-01/             │
                     │        part-0001.parquet         │
                     └──────────────────────────────────┘
```

### Component Responsibilities

| Component | Language | Role |
|-----------|----------|------|
| `one_d4` (existing) | Java | REST API, indexing (SQL writes), motif detection (chariot), ChessQL parse + Substrait compile, query routing, Parquet export job |
| `chessql` (existing lib) | Java | Parser, AST, `SubstraitCompiler` (new), `SqlCompiler` (existing) |
| `motif_query` (new) | Rust | DataFusion query engine via Substrait consumer, Parquet file writes (batch, not streaming) |
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
pgn:                Utf8
```

Motif data is exported from `motif_occurrences` as a separate Parquet table and joined at query time.

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

## ChessQL Compilation via Substrait IR

### The Problem

The current `SqlCompiler` compiles ChessQL AST → PostgreSQL SQL. Adding
DataFusion requires a second compiler targeting DataFusion SQL dialect.
While the dialects are similar, maintaining two SQL compilers creates
drift, and tightly couples ChessQL to specific SQL dialects:

- PostgreSQL uses `?` bind parameters; DataFusion uses `ParamValues` or
  inline literals
- PostgreSQL uses `::timestamp` casts; DataFusion uses
  `CAST(... AS TIMESTAMP)`
- `sequence()` queries use correlated subqueries that differ in optimizer
  behavior between backends
- Adding a third backend (e.g. DuckDB, ClickHouse) would require yet
  another SQL dialect compiler

### Substrait as Query IR

[Substrait](https://substrait.io/) is a cross-language specification for
relational algebra plans. It defines a protobuf-based format for query
plans that multiple engines can consume. The compilation pipeline becomes:

```
ChessQL string
     │
     ▼
  Parser (Java) → ParsedQuery (AST)
     │
     ▼
  SubstraitCompiler (Java) → Substrait Plan (protobuf)
     │
     ├──► PostgreSQL backend: substrait-java consumer → JDBC SQL
     │         (current path, keeps working)
     │
     └──► DataFusion backend: datafusion-substrait consumer → LogicalPlan
              (new path, Parquet query engine)
```

This gives us:

1. **Single compilation step.** ChessQL compiles to Substrait once. No
   SQL dialect-specific compilers.
2. **Backend portability.** The same Substrait plan executes on PostgreSQL,
   DataFusion, or any future engine that consumes Substrait.
3. **Feature flag / cost-based routing.** The `QueryController` can route
   plans to SQL or DataFusion based on configuration, query complexity,
   or data locality.
4. **No ChessQL port to Rust.** The Java side owns parsing and compilation.
   The Rust side only needs to consume Substrait plans, which DataFusion
   already supports via the `datafusion-substrait` crate.

### SubstraitCompiler Implementation

New class: `SubstraitCompiler implements QueryCompiler<Plan>` where `Plan`
is `io.substrait.proto.Plan` from the `substrait-java` library.

The compiler walks the ChessQL AST and produces Substrait relational
algebra nodes:

| ChessQL construct | Substrait equivalent |
|-------------------|---------------------|
| `motif(fork)` | `Filter(ReadRel("game_features"), ExistsSubquery(Filter(ReadRel("motif_occurrences"), and(equal(FieldRef("motif"), "ATTACK"), equal(FieldRef("is_discovered"), false), isNotNull(FieldRef("attacker"))), GroupBy(ply, attacker), Having(gte(count, 2))))` |
| `white.elo >= 2500` | `Filter(ReadRel, ScalarFunction(gte, FieldRef("white_elo"), I32Literal(2500)))` |
| `AND` / `OR` / `NOT` | `ScalarFunction(and/or/not, ...)` |
| `eco IN ["B90", "C65"]` | `ScalarFunction(or, equal(eco, "B90"), equal(eco, "C65"))` or `SingularOrList` |
| `ORDER BY motif_count(fork) DESC` | `SortRel(AggregateRel(JoinRel(game_features, motif_occurrences), count), SortField(desc))` |
| `sequence(fork THEN pin)` | `Filter(ReadRel, ExistsSubquery(JoinRel(motif_occurrences aliases, ply constraints)))` |
| String case-insensitivity | `ScalarFunction(equal, ScalarFunction(lower, FieldRef), ScalarFunction(lower, Literal))` |

The Substrait plan includes the full schema (named struct) for
`game_features` and `motif_occurrences`, so consumers know the table
layout without out-of-band schema exchange.

```java
public class SubstraitCompiler implements QueryCompiler<Plan> {

  private final SubstraitSchema schema;  // table + column definitions

  @Override
  public Plan compile(ParsedQuery pq) {
    Rel baseRel = namedScan("game_features", schema.gameFeaturesCols());
    Expression filter = compileExpr(pq.expr());
    Rel filtered = filterRel(baseRel, filter);

    if (pq.orderBy() != null) {
      // Join with motif_occurrences, aggregate count, sort
      filtered = compileOrderBy(filtered, pq.orderBy());
    } else {
      filtered = sortRel(filtered, fieldRef("played_at"), DESCENDING);
    }

    return Plan.newBuilder()
        .addRelations(PlanRel.newBuilder().setRoot(
            RelRoot.newBuilder().setInput(filtered).build()
        )).build();
  }
}
```

### Query Routing

The `QueryController` chooses which backend executes the Substrait plan:

```java
@POST
public QueryResponse query(QueryRequest request) {
  ParsedQuery parsed = Parser.parse(request.query());
  Plan plan = substraitCompiler.compile(parsed);

  // Route based on configuration or query characteristics
  List<GameFeature> rows = queryRouter.execute(plan, request.limit(), request.offset());
  // ... format response
}
```

The `QueryRouter` decides the backend:

```java
public interface QueryRouter {
  List<GameFeature> execute(Plan plan, int limit, int offset);
}
```

Implementations:

| Strategy | Description |
|----------|-------------|
| `SqlQueryRouter` | Convert Substrait → SQL via `substrait-java` `SubstraitToSql`, execute on PostgreSQL/H2 via JDBC. Drop-in replacement for current `SqlCompiler` path. |
| `DataFusionQueryRouter` | Serialize Substrait plan to protobuf bytes, POST to `motif_query/v1/query/substrait`. DataFusion deserializes and executes. |
| `ConfigQueryRouter` | Feature flag: `QUERY_BACKEND=sql|datafusion`. Simple toggle for migration. |
| `CostQueryRouter` | Inspect the plan: if it touches only `game_features` (metadata filters, no motif predicates), route to DataFusion (fast columnar scan). Motif predicates always join `motif_occurrences`; route `sequence()` and complex motif queries to SQL (mature optimizer) or DataFusion depending on dataset size. |

### How Substrait Flows to Each Backend

**PostgreSQL path (existing, adapted):**

```
Plan (protobuf)
  → substrait-java SqlConverter → SQL string + bind params
  → JDBC PreparedStatement → PostgreSQL
  → ResultSet → GameFeature list
```

The `substrait-java` library includes `SubstraitToSql` which converts
plans to ANSI SQL. For PostgreSQL-specific syntax, we extend the converter
or post-process the SQL. This replaces `SqlCompiler` — same output, but
generated from Substrait rather than directly from the AST.

**DataFusion path (new):**

```
Plan (protobuf bytes)
  → HTTP POST to motif_query/v1/query/substrait
  → datafusion-substrait::from_substrait_plan() → LogicalPlan
  → DataFusion optimizer → PhysicalPlan
  → execute over Parquet → RecordBatches
  → JSON response
```

DataFusion's Substrait consumer is mature — it handles filters, projections,
joins, aggregations, and sort. The `datafusion-substrait` crate translates
directly to `LogicalPlan` without an intermediate SQL string, giving the
optimizer full visibility.

### Why Not Just Two SQL Compilers?

The direct approach (write a `DataFusionSqlCompiler` alongside `SqlCompiler`)
is simpler for the initial step. But:

1. **Drift.** Every ChessQL feature (new motif, new field, new operator)
   must be implemented in both compilers and tested independently.
2. **SQL is lossy.** SQL strings discard semantic information that
   optimizers could use. Substrait preserves the relational algebra
   structure — DataFusion can optimize better from a `LogicalPlan` than
   from re-parsing a SQL string.
3. **Testing.** With Substrait, you test one compilation (ChessQL → plan)
   and two consumers. With two SQL compilers, you test two compilations.
4. **Future backends.** DuckDB, Velox, Acero all consume Substrait. SQL
   compatibility varies per engine.

### Dependencies

**Java (substrait-java):**

```
maven_install:
  io.substrait:core:0.42.0        # Substrait protobuf types + plan builder
  io.substrait:isthmus:0.42.0     # SQL ↔ Substrait conversion (for SqlQueryRouter)
```

The `isthmus` module provides `SubstraitToSql` for the PostgreSQL path
and `SqlToSubstrait` if we ever want to go the other direction.

**Rust (datafusion-substrait):**

```toml
[dependencies]
datafusion-substrait = { version = "46" }  # matches datafusion version
```

This crate provides `from_substrait_plan()` which converts a Substrait
`Plan` protobuf into a DataFusion `LogicalPlan`.

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
arrow = { version = "55" }
datafusion = { version = "46" }
datafusion-substrait = { version = "46" }  # Substrait plan → LogicalPlan
parquet = { version = "55", features = ["snap"] }
prost = "0.13"                              # protobuf deserialization
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
POST /motif_query/v1/query/substrait
  Body: <Substrait Plan protobuf bytes>
  Content-Type: application/x-substrait-plan
  Query params: ?limit=50&offset=0
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

Note: the `/v1/query` SQL endpoint is removed. The Java side compiles
ChessQL → Substrait and sends the plan bytes directly. No SQL strings
cross the wire.

### DataFusion Setup + Substrait Execution

```rust
use datafusion::prelude::*;
use datafusion_substrait::logical_plan::consumer::from_substrait_plan;
use substrait::proto::Plan;
use prost::Message;

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

/// Execute a Substrait plan received from the Java SubstraitCompiler.
async fn execute_substrait(
    ctx: &SessionContext,
    plan_bytes: &[u8],
) -> datafusion::error::Result<Vec<RecordBatch>> {
    // Deserialize protobuf → Substrait Plan
    let plan = Plan::decode(plan_bytes)
        .map_err(|e| DataFusionError::External(Box::new(e)))?;

    // Convert Substrait → DataFusion LogicalPlan
    let logical_plan = from_substrait_plan(ctx, &plan).await?;

    // Execute (DataFusion optimizes → physical plan → Parquet scan)
    let df = ctx.execute_logical_plan(logical_plan).await?;
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

### Motif Detection — Java Only (chariot)

Motif detection stays in Java. The current detectors use chariot
(`io.github.tors42:chariot`) via `GameReplayer` for board state and FEN
generation. Issue #1049 (Phase 9) plans to deepen the chariot integration:

- **Check attribution**: distinguish promoted-piece check vs discovered
  check vs double check, requiring chariot's board model to identify which
  piece delivers the check.
- **5 new motifs**: back rank mate, smothered mate, zugzwang,
  double check, overloaded piece — several of these need
  full board state analysis that goes well beyond FEN string parsing.
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

The bulk ingest is a Java batch job, either as:

- **Option A: Standalone CLI jar** — a new `lichess_ingest` binary target
  in `domains/games/apis/one_d4/` that reuses `GameReplayer`,
  `FeatureExtractor`, and all `MotifDetector` implementations. Streams
  `.pgn.zst` files, filters to titled/high-Elo games, runs detection,
  and writes results to `motif_query`'s Parquet write endpoint.

- **Option B: Admin endpoint in one_d4** — `POST /admin/ingest/lichess`
  accepts a URL or file path, streams + filters + detects in-process,
  and writes Parquet batches. Simpler deployment but ties the batch
  workload to the API server's resources.

**Recommendation: Option A.** A standalone CLI jar keeps the long-running
bulk workload isolated from the API server. It shares the same motif
detection code via library targets. The CLI can run as a cron job or
one-off invocation.

```
java -jar lichess_ingest.jar \
  --input lichess_db_standard_rated_2024-01.pgn.zst \
  --motif-query-url http://localhost:8081 \
  --min-elo 2200 \
  --titles GM,IM,WGM,WIM \
  --time-controls standard,rapid,blitz \
  --batch-size 1000
```

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
7. POST batch to motif_query/v1/write → Parquet
     ↓
8. motif_query flushes to partitioned Parquet files
```

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

### Expected Write Throughput

| Metric | Value |
|--------|-------|
| Games per month (total, Chess.com) | 10,000 - 100,000 |
| Games per day (avg) | ~300 - 3,300 |
| Games per player-month (from API) | 10 - 100 |
| Index requests per day (estimated) | ~10 - 50 |
| Motif occurrences per game (avg) | ~5 - 20 |

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

for each (platform, month) with unexported games:
  rows = SELECT * FROM game_features
         WHERE platform = ? AND month = ?
         AND game_url NOT IN (already exported)
  occurrences = SELECT * FROM motif_occurrences
                WHERE game_url IN (row.game_url for row in rows)

  POST motif_query/v1/write
    { "table": "game_features", rows }
  POST motif_query/v1/write
    { "table": "motif_occurrences", occurrences }

  UPDATE game_storage_backends
    SET backend = 'both', parquet_exported_at = now()
    WHERE platform = ? AND month = ?
```

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

This is the key advantage over real-time Parquet writes: the export job
reads the complete partition in one SELECT and writes exactly one
well-formed Parquet file. No buffering, no tail flushes, no compaction.

#### Re-export After Re-analysis

When new motif detectors are added (issue #1049 Phase 9), an admin
endpoint triggers re-analysis of existing games. After re-analysis
updates the SQL rows, the export job re-runs for affected partitions:

```
1. Admin triggers re-analysis → SQL rows updated with new motif flags
2. Export job detects stale Parquet (SQL rows newer than parquet_exported_at)
3. Re-export: read all rows for partition → overwrite Parquet file
4. Update game_storage_backends.parquet_exported_at
```

Since we write one file per partition, re-export is a simple overwrite —
no merge logic needed.

### Re-Analysis and Schema Evolution

Adding new motifs or correcting existing detectors requires updating
stored game data in both SQL and Parquet. This is a two-step process:
first update SQL (source of truth for Chess.com games), then re-export
to Parquet. Lichess-only games need a different path since they're never
in SQL.

#### Step 1: Schema Evolution

New motif columns must be added to both stores:

**SQL:**
```sql
ALTER TABLE game_features ADD COLUMN has_back_rank_mate BOOLEAN DEFAULT FALSE;
ALTER TABLE game_features ADD COLUMN has_smothered_mate BOOLEAN DEFAULT FALSE;
-- ... repeat for each new motif
```

**Parquet:** No migration needed. Parquet is schema-on-read — the next
export/ingest writes files with the new columns. Old Parquet files
missing the column return NULL, which DataFusion treats as FALSE for
boolean filters. New Parquet files include the column with real values.

This asymmetry is a strength: SQL requires `ALTER TABLE`, but Parquet
files are simply rewritten with the updated schema on the next export.
No downtime, no backfill-then-migrate sequencing.

**Code changes:**
- `GameFeatureDao` INSERT/MERGE statements: add new columns
- `GameFeature` record: add new fields
- `FeatureExtractor` / `MotifDetector`: add new detectors
- `SqlCompiler.VALID_MOTIFS`: add new motif names
- `SubstraitCompiler`: schema `NamedStruct` includes new columns
- Parquet schema in `motif_query` `catalog.rs`: add new columns

#### Step 2: Re-Analysis Pipeline (Chess.com games in SQL)

The re-analysis pipeline from ROADMAP.md Phase 9:

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
      // Only run specified detectors, or all if not specified
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

Key point: **PGN is already in SQL** for Chess.com games. Re-analysis
reads it, re-runs detection, and UPDATEs the rows in place. The existing
`ON CONFLICT` upsert in `GameFeatureDao` only updates `indexed_at` and
`request_id` — re-analysis needs a dedicated UPDATE path that sets
motif columns.

#### Step 3: Re-Export Stale Partitions

After re-analysis updates SQL rows, the export job detects stale
Parquet:

```java
// ParquetExportJob — modified to handle staleness
List<StorageBackend> stale = storageBackendStore.findStalePartitions();
// Returns partitions where parquet_stale = true
// OR where parquet_exported_at < max(indexed_at) for games in partition

for (StorageBackend sb : stale) {
  exportPartition(sb.platform(), sb.month());  // SELECT → write Parquet
  storageBackendStore.markExported(sb.platform(), sb.month());
}
```

Since the export writes one complete file per partition (overwriting the
old one), re-export is identical to first export. No incremental merge.

#### Lichess Games — Parquet-Only Re-Analysis

Lichess games are `backend='parquet'` — they were never in SQL and
there's no PGN to re-read from the database. Re-analysis needs a
different approach:

**Option A: Re-ingest from Lichess dump.** Download the original
`.pgn.zst` file for the month, re-run the `lichess_ingest` pipeline
with updated detectors, overwrite the Parquet files. This is the
cleanest path — same code as initial ingest, just with new detectors.
Cost: ~15-30 minutes per monthly dump.

**Option B: Store Lichess PGN in SQL for re-analysis.** During initial
ingest, also INSERT the PGN into a `lichess_pgns` SQL table (or the
existing `game_features.pgn` column). This makes Lichess games
re-analyzable the same way as Chess.com games. Downside: 2M PGN
strings/month in SQL (~2-4 GB/month, mostly PGN text). This defeats
one of the benefits of the Parquet-only path.

**Option C: Store Lichess PGN in a separate Parquet table.** A
`game_pgns` Parquet table with just `(game_url, pgn)`. The re-analysis
pipeline reads PGN from this table via DataFusion, re-runs detectors,
and writes updated `game_features` Parquet files. This keeps PGN out
of SQL while enabling re-analysis.

**Recommendation: Option A for now, Option C if re-analysis frequency
is high.** Re-analysis is a rare event (when new detectors are added).
Re-ingesting from the Lichess dump is simple and doesn't require
storing PGN. If re-analysis becomes frequent (e.g. detector accuracy
improvements every month), storing PGN in a separate Parquet table is
worth the storage cost.

#### Update to `game_storage_backends`

Add a `parquet_stale` flag to track when SQL data has been updated but
Parquet hasn't been re-exported yet:

```sql
CREATE TABLE game_storage_backends (
  platform            TEXT NOT NULL,
  month               TEXT NOT NULL,
  backend             TEXT NOT NULL,      -- 'sql', 'parquet', 'both'
  games_in_sql        INTEGER DEFAULT 0,
  games_in_parquet    INTEGER DEFAULT 0,
  parquet_exported_at TIMESTAMP,
  parquet_stale       BOOLEAN DEFAULT FALSE,  -- true after re-analysis
  last_reanalyzed_at  TIMESTAMP,               -- when re-analysis last ran
  PRIMARY KEY (platform, month)
);
```

The query router uses `parquet_stale` to decide routing during the
window between re-analysis and re-export:

```
if backend = 'both' AND parquet_stale = true:
  → route to SQL (Parquet data is outdated)
if backend = 'both' AND parquet_stale = false:
  → route to DataFusion (Parquet is current)
if backend = 'parquet' AND parquet_stale = true:
  → route to DataFusion anyway (no SQL alternative),
    but log warning — Lichess data needs re-ingest
```

#### Timeline: Re-Analysis → Re-Export

```
t=0:  Admin triggers POST /admin/reanalyze
      Re-analysis begins processing games in SQL.
      game_storage_backends.parquet_stale = true for affected partitions.
      Query router falls back to SQL for stale partitions.

t=N:  Re-analysis completes (minutes for 10K-100K games).
      SQL rows now have updated motif flags.
      Queries hitting SQL return correct results immediately.

t=N+1: ParquetExportJob runs (next scheduled cron, or admin-triggered).
       Reads updated SQL rows, writes new Parquet files.
       game_storage_backends.parquet_stale = false.
       Query router resumes routing to DataFusion.
```

For Lichess partitions (Option A), the window is longer — re-ingest
takes 15-30 minutes per monthly dump, and must be triggered manually.
During this window, queries against Lichess data return stale motif
results for the new detectors (old detectors still correct).

### Storage Routing — `game_storage_backends` Table

A metadata table in PostgreSQL tracks which backend holds motif data for
each partition:

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

| `backend` value | `parquet_stale` | Meaning | Query route |
|-----------------|-----------------|---------|-------------|
| `sql` | n/a | Games only in SQL, not yet exported | SQL |
| `parquet` | `false` | Lichess games, Parquet is current | DataFusion |
| `parquet` | `true` | Lichess games, needs re-ingest | DataFusion (stale warning) |
| `both` | `false` | Exported, Parquet is current | DataFusion |
| `both` | `true` | Re-analyzed in SQL, Parquet outdated | SQL (until re-export) |

#### How the Query Router Uses This

```java
public class StorageAwareQueryRouter implements QueryRouter {

  List<GameFeature> execute(Plan plan, int limit, int offset) {
    // Extract platform/month predicates from the Substrait plan
    Set<PartitionKey> partitions = extractPartitions(plan);

    Set<String> backends = partitions.stream()
        .map(p -> storageBackendStore.getBackend(p.platform(), p.month()))
        .collect(Collectors.toSet());

    if (backends.equals(Set.of("parquet")) || backends.equals(Set.of("both"))) {
      // All partitions have Parquet data — use DataFusion
      return dataFusionRouter.execute(plan, limit, offset);
    } else if (backends.contains("sql") && !backends.contains("parquet")) {
      // Some partitions only in SQL — use PostgreSQL
      return sqlRouter.execute(plan, limit, offset);
    } else {
      // Mixed: some partitions in SQL only, some in Parquet only.
      // Fan out to both backends, merge results.
      return mergeResults(
          sqlRouter.execute(restrictToPlatformMonths(plan, sqlOnlyPartitions), limit, offset),
          dataFusionRouter.execute(restrictToPlatformMonths(plan, parquetPartitions), limit, offset),
          limit, offset
      );
    }
  }
}
```

In practice, the mixed case is rare — it only happens for the current
month (Chess.com games indexed but not yet exported) when combined with
Lichess data. Most queries will hit either all-SQL (recent Chess.com
only) or all-Parquet (historical data after export + Lichess).

#### Simpler Alternative: Time-Based Routing

If partition-level routing is too complex for Phase 1, use a simpler
time-based rule:

```
if query touches only months older than EXPORT_CUTOFF (e.g. last month):
  → DataFusion (Parquet)  // all historical data is exported
else:
  → SQL (PostgreSQL)       // current month is still in SQL
```

This works because the export job runs monthly. All completed months
have Parquet data. Only the current (incomplete) month is SQL-only.

### PGN and Game Metadata — SQL Only

PGN text stays in PostgreSQL. It's large, variable-length, and only
needed for game replay — not for motif search. The Parquet files contain
only the columns needed for ChessQL analytical queries (motif flags,
player info, Elo, time class, ECO, result).

| Data | Storage | Reason |
|------|---------|--------|
| Motif queries (`motif(pin)`, `motif(fork)`, ...) | SQL EXISTS subqueries + Parquet join (after export) | Analytical queries |
| Game metadata (Elo, ECO, result, ...) | SQL + Parquet (after export) | Needed for ChessQL filters |
| PGN text | SQL only | Large, variable-length, not scanned by ChessQL |
| Motif occurrences (ply, side, ...) | SQL + Parquet (after export) | Needed for `sequence()` and `ORDER BY motif_count()` |
| `indexing_requests` | SQL only | Mutable operational state |
| `indexed_periods` | SQL only | Mutable cache |
| `game_storage_backends` | SQL only | Routing metadata |

### Query Flow (Substrait-based)

```
POST /v1/query { "query": "motif(fork) AND white.elo >= 2500" }
  ↓
one_d4 QueryController
  ↓
Parser.parse() → ParsedQuery (AST)
  ↓
SubstraitCompiler.compile() → Substrait Plan (protobuf)
  ↓
StorageAwareQueryRouter.execute(plan)
  │
  ├─ Check game_storage_backends for queried partitions
  │
  ├── All partitions in Parquet → DataFusion backend:
  │     plan.toByteArray() → POST motif_query/v1/query/substrait
  │     → datafusion-substrait → LogicalPlan → Parquet scan
  │     → JSON results back to one_d4
  │
  ├── All partitions in SQL only → SQL backend:
  │     substrait-java SqlConverter → SQL string + bind params
  │     → JDBC PreparedStatement → PostgreSQL → ResultSet
  │
  └── Mixed → fan out to both, merge results
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
Chess.com games are SQL-only until the next export runs. But this is
fine: the SQL backend handles these queries today, and the export
frequency is configurable. For users who just indexed their games,
queries hit SQL immediately.

## Do We Keep H2/PostgreSQL?

**Short answer: Yes — PostgreSQL is the primary write store for Chess.com
games and keeps all data that Parquet doesn't need.**

### What lives where

| Data | PostgreSQL | Parquet | Notes |
|------|-----------|---------|-------|
| `game_features` (motif flags, metadata) | Always (source of truth for Chess.com) | After export job / Lichess ingest | Parquet is derived for Chess.com, primary for Lichess |
| `motif_occurrences` | Always (source of truth for Chess.com) | After export job / Lichess ingest | Same as above |
| PGN text | Always (Chess.com) | Never | Large, variable-length, not needed for analytical queries |
| `indexing_requests` | Always | Never | Mutable operational state (PENDING → PROCESSING → COMPLETED) |
| `indexed_periods` | Always | Never | Mutable cache of fetched (player, platform, month) combos |
| `game_storage_backends` | Always | Never | Routing metadata — which backend has which partitions |

### PGN Storage

PGN text stays in PostgreSQL for Chess.com games. For Lichess games,
link back to `https://lichess.org/game/export/{id}` — no need to store
the PGN at all. This keeps Parquet files lean (only columns needed for
ChessQL queries).

### When Can We Drop SQL Game Tables?

In the previous (dual-write) design, the goal was to eventually drop
`game_features` and `motif_occurrences` from PostgreSQL. With the
SQL-first approach, **we don't drop them** — PostgreSQL remains the
write store for Chess.com games and the source of PGN text. The tables
stay, but the analytical query load shifts to DataFusion/Parquet for
historical data.

If we later want to stop storing Chess.com game data in SQL (e.g. to
reduce PostgreSQL storage), we'd need to solve PGN storage separately
(object storage or a dedicated Parquet table). That's a future
optimization, not a near-term goal.

### Migration Path

```
Phase 1: SubstraitCompiler + motif_query crate scaffold.
         ChessQL compiles to Substrait. SqlQueryRouter converts
         Substrait → SQL and executes on PostgreSQL (functionally
         equivalent to today's SqlCompiler path). motif_query Rust
         service accepts Substrait plans over Parquet. IndexWorker
         is unchanged — still writes to SQL.

Phase 2: Parquet export job + storage routing.
         Export job runs weekly/monthly: SELECT from SQL → write
         Parquet via motif_query/v1/write. game_storage_backends
         tracks which partitions have Parquet data.
         StorageAwareQueryRouter dispatches to SQL or DataFusion
         based on storage metadata.

Phase 3: Shadow mode validation.
         For partitions in 'both' state, run queries on both
         backends, compare results, log mismatches. SQL results
         are returned to client.

Phase 4: DataFusion primary for exported partitions.
         Queries for partitions in 'both' or 'parquet' state go
         to DataFusion. Only current-month (unexported) queries
         hit SQL.

Phase 5: Lichess bulk ingest (after Phase 9 / issue #1049 lands).
         Run lichess_ingest Java CLI on historical Lichess dumps.
         All 18 motif detectors (11 existing + 7 new chariot-based)
         are included in bulk processing. Lichess partitions are
         'parquet' only.
```

## Implementation Plan

### Phase 1: SubstraitCompiler in chessql (2-3 days)

- Add `io.substrait:core` and `io.substrait:isthmus` maven dependencies
- New `SubstraitCompiler implements QueryCompiler<Plan>` in chessql library
  - Walk ChessQL AST → Substrait `ReadRel`, `FilterRel`, `SortRel`,
    `AggregateRel`, `JoinRel` nodes
  - Handle `MotifExpr` → EXISTS subquery on `motif_occurrences` (most motifs by stored name; `fork`, `checkmate`, `discovered_attack`, `discovered_check`, `double_check` derived from `ATTACK` rows)
  - Handle `ComparisonExpr` / `InExpr` → scalar functions
  - Handle `SequenceExpr` → join-based exists subquery
  - Handle `OrderByClause` → aggregate + sort on motif_occurrences
  - Include `NamedStruct` schema for `game_features` and
    `motif_occurrences` tables in the plan
- New `SqlQueryRouter` that consumes Substrait plans:
  - Use `isthmus` `SubstraitToSql` to convert Plan → SQL + params
  - Execute via JDBC (same as today's `GameFeatureDao.query()`)
  - This replaces `SqlCompiler` as the production path, keeping
    PostgreSQL as the query backend while compiling through Substrait
- Unit tests:
  - `SubstraitCompilerTest`: verify plan structure for each AST node type
  - `SqlQueryRouterTest`: verify roundtrip ChessQL → Substrait → SQL
    produces equivalent SQL to `SqlCompiler` output
  - Keep `SqlCompilerTest` passing (existing compiler not yet removed)
- Wire `SubstraitCompiler` + `SqlQueryRouter` into `QueryController`
  behind a config flag (`USE_SUBSTRAIT=true|false`, default false)

### Phase 2: motif_query crate scaffold (1-2 days)

- Create `domains/games/apis/motif_query/` with Cargo.toml, BUILD.bazel
- Add `datafusion`, `datafusion-substrait`, `arrow`, `parquet`, `prost`
  workspace dependencies
- Implement `catalog.rs`: register Parquet listing tables with partition
  columns
- Implement `query.rs`: accept Substrait plan bytes, decode protobuf,
  call `from_substrait_plan()` → `LogicalPlan`, execute, return JSON
- Implement `writer.rs`: accept JSON rows, write directly to partitioned
  Parquet (no buffering needed — callers send complete batches)
- axum server with `/v1/query/substrait`, `/v1/write`, `/health` endpoints
- Unit tests with in-memory Parquet roundtrips + a sample Substrait plan

### Phase 3: Parquet export job + storage routing (2-3 days)

- New `game_storage_backends` SQL table + DAO
- New `ParquetExportJob`:
  - SELECT game_features + motif_occurrences for each (platform, month)
    with games not yet exported
  - POST complete partition to `motif_query/v1/write`
  - UPDATE `game_storage_backends` to 'both'
- Admin endpoint: `POST /admin/export/parquet?platform=X&month=Y`
  (trigger export for specific partition)
- Cron scheduling: weekly or monthly via config
- `StorageAwareQueryRouter`:
  - Check `game_storage_backends` to determine which backend has data
  - Route to SQL, DataFusion, or fan-out to both
  - Start with time-based routing (simpler): current month → SQL,
    older months → DataFusion (if exported)
- `DataFusionQueryRouter`: serialize Substrait plan → HTTP POST to
  motif_query → deserialize JSON response
- Shadow mode: for 'both' partitions, run both backends, compare, log

### Phase 4: Lichess ingest pipeline — Java (3-5 days)

- New `lichess_ingest` binary target in `domains/games/apis/one_d4/`
- Streaming PGN parser: read `.pgn.zst` via `zstd-jni`, extract headers
- GM/title filter: parse `WhiteTitle`/`BlackTitle` headers, Elo thresholds
- Reuse existing Java motif detectors (GameReplayer + FeatureExtractor +
  all MotifDetector implementations — no porting needed)
- HTTP client to batch-POST results to `motif_query/v1/write`
- CLI interface: `java -jar lichess_ingest.jar --input ... --motif-query-url ...`
- Insert `game_storage_backends` rows with backend='parquet' for Lichess
  partitions
- Test against a small Lichess sample file
- **Dependency:** Should run after Phase 9 (issue #1049) lands so that
  the 7 new chariot-based motifs are included in the bulk ingest

### Phase 5: Cost-based routing (optional, 1 day)

- `CostQueryRouter` inspects the Substrait plan:
  - Simple boolean filter on `game_features` → DataFusion (fast scan)
  - `sequence()` with small expected result set → SQL (mature optimizer)
  - Aggregate queries → DataFusion (columnar aggregation)
- Configurable cost thresholds, with override via `QUERY_BACKEND` env var

### Phase 6: Remove `SqlCompiler` (1 day)

- All compilation goes through Substrait
- Remove `SqlCompiler`, `CompiledQuery`
- Simplify `QueryController` — only uses `SubstraitCompiler`
- Remove `USE_SUBSTRAIT` feature flag (always on)

## Cost and Performance Estimates

### Storage

| Dataset | PostgreSQL | Parquet (Snappy) |
|---------|-----------|-----------------|
| 1 month Chess.com (50K games) | ~40 MB | ~2.5-5 MB |
| 12 months Chess.com (600K games) | ~480 MB | ~30-60 MB |
| 12 months Lichess GM games (~20M) | ~16 GB | ~1.5-2.5 GB |
| Combined (12 mo Chess.com + Lichess) | ~16.5 GB | ~1.6-2.6 GB |

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
| Lichess PGN header parse + filter (Java, streaming) | ~100-200K games/sec |
| Full motif analysis (chariot + Java detectors) | ~2-5K games/sec |
| HTTP batch write to motif_query | ~10K rows/sec |
| End-to-end: 1 month Lichess (100M games, ~2M qualifying) | ~15-30 minutes |

## Open Questions

1. **Object storage vs local disk?** Starting with local disk is simpler.
   DataFusion supports `object_store` for S3/GCS — add it later when
   deploying to cloud.

2. **Do we need the motif_occurrences table at all in Parquet?** The
   `game_features` boolean flags handle most ChessQL queries. The
   occurrences table is only needed for `sequence()` queries and
   `ORDER BY motif_count()`. If those features are rarely used, we could
   keep `sequence()` queries routed to SQL permanently and skip
   exporting motif_occurrences to Parquet entirely.

3. **Phase 9 before or after Lichess ingest?** Ideally Phase 9 (issue
   #1049 — 7 new chariot-based motifs) lands before the first Lichess
   bulk ingest, so all 18 motifs are captured in one pass. If Phase 9 is
   delayed, we could ingest with the current 11 detectors and re-analyze
   later via the admin endpoint, but that doubles the processing cost.

4. **Java Parquet writer alternative?** Instead of HTTP POST to the Rust
   motif_query service, the export job and `lichess_ingest` CLI could
   write Parquet directly using `org.apache.parquet:parquet-avro` or
   `org.apache.arrow:arrow-dataset`. This eliminates the HTTP hop but
   means two codepaths for Parquet writing (Java for writes, Rust for
   reads). The Rust write endpoint is simpler to keep consistent with
   the query schema.

5. **Substrait coverage for `sequence()`?** The `sequence()` construct
   compiles to a correlated EXISTS with self-joins on `motif_occurrences`.
   Substrait's `SetPredicateRel` (EXISTS) and `JoinRel` should handle
   this, but `datafusion-substrait` may not support all Substrait
   relation types. If `sequence()` hits a gap, we can either:
   (a) extend the DataFusion Substrait consumer,
   (b) always route `sequence()` queries to SQL, or
   (c) represent sequences differently (e.g. pre-materialized boolean
   columns like `has_sequence_fork_then_pin`).

6. **Substrait version pinning.** The `substrait-java` library and
   `datafusion-substrait` crate must agree on the Substrait protobuf
   spec version. Pin both to the same Substrait spec release (e.g.
   v0.42.x). Mismatches cause deserialization failures.

7. **Export frequency — weekly vs monthly?** Weekly exports mean fresher
   Parquet data and faster query routing to DataFusion for recent games.
   Monthly exports are simpler and produce cleaner partition boundaries.
   At 10K-100K games/month, the difference is small. Could also be
   event-driven: export when a partition reaches N games or on admin
   trigger.

8. **Handling re-indexed games in export?** If a player is re-indexed
   (e.g. after new motif detectors land), the SQL rows are updated but
   the Parquet file for that partition is now stale. The export job
   needs to detect this — either by comparing `indexed_at` timestamps
   against `parquet_exported_at`, or by always re-exporting partitions
   that have been touched since the last export.

9. **Mixed-backend query merging.** When a query spans partitions in
   different backends (e.g. current month in SQL, older months in
   Parquet), the `StorageAwareQueryRouter` needs to merge results. For
   simple queries this is concatenation + re-sort. For aggregates and
   `ORDER BY motif_count()`, merging is more complex. The time-based
   routing shortcut (current month → SQL, older → DataFusion) avoids
   this for most queries.
