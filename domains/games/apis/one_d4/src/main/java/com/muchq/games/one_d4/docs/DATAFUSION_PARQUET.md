# Chess Game Indexer — DataFusion + Parquet Query Engine

## Overview

Add a Rust service (`motif_query`) that uses Apache DataFusion to query
Parquet files containing game features and motif occurrences. The Java
indexing pipeline writes Parquet instead of (or in addition to) SQL rows.
A pre-indexing pipeline bulk-loads GM games from the Lichess open database
into the same Parquet format.

**Query IR: Substrait.** ChessQL compiles to [Substrait](https://substrait.io/)
relational algebra plans (protobuf) instead of SQL strings. A `QueryRouter`
dispatches Substrait plans to either PostgreSQL (via `substrait-java`
SQL conversion) or DataFusion (via `datafusion-substrait` plan consumer),
based on a feature flag or cost-based routing. This gives us backend
portability without maintaining dialect-specific SQL compilers.

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
│  - GameReplayer (chariot)    │
│  - FeatureExtractor          │
│  - all MotifDetectors        │
└──────────┬───────────────────┘
           │ HTTP batch POST
           ▼
┌───────────────────────────────────────────────────────────────────┐
│  one_d4 Java API                                                 │
│                                                                   │
│  /v1/index ─► GameReplayer + FeatureExtractor + MotifDetectors   │
│                  └─► GameFeatureStore.insert() (SQL + Parquet)    │
│                                                                   │
│  /v1/query ─► Parser ─► SubstraitCompiler ─► Substrait Plan      │
│                                                  │                │
│                              ┌────────────────────┤               │
│                              ▼                    ▼               │
│                    ┌─────────────────┐  ┌────────────────────┐   │
│                    │  QueryRouter    │  │  QueryRouter        │   │
│                    │  (SQL backend)  │  │  (DataFusion)       │   │
│                    └────────┬────────┘  └─────────┬──────────┘   │
│                             │                     │               │
└─────────────────────────────┼─────────────────────┼───────────────┘
                              │                     │
                              ▼                     ▼
                   ┌────────────────┐    ┌──────────────────────────┐
                   │  PostgreSQL/H2 │    │  motif_query (Rust)      │
                   │  (game tables) │    │  - axum HTTP API         │
                   └────────────────┘    │  - datafusion-substrait  │
                                         │    (Plan → LogicalPlan)  │
                                         │  - DataFusion execution  │
                                         │  - Parquet writer        │
                                         │  - /v1/write (ingest)    │
                                         │  - /v1/query/substrait   │
                                         └──────────┬───────────────┘
                                                    │ reads/writes
                                                    ▼
                                         ┌──────────────────────────┐
                                         │  Parquet files on disk/S3│
                                         │                          │
                                         │  /data/games/            │
                                         │    platform=chess.com/   │
                                         │      month=2024-03/      │
                                         │        part-0000.parquet │
                                         │    platform=lichess/     │
                                         │      month=2024-03/      │
                                         │        part-0000.parquet │
                                         └──────────────────────────┘
```

### Component Responsibilities

| Component | Language | Role |
|-----------|----------|------|
| `one_d4` (existing) | Java | REST API, indexing, motif detection (chariot), ChessQL parse + Substrait compile, query routing |
| `chessql` (existing lib) | Java | Parser, AST, `SubstraitCompiler` (new), `SqlCompiler` (existing) |
| `motif_query` (new) | Rust | DataFusion query engine via Substrait consumer, Parquet reads/writes |
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
# Phase 9 motifs (issue #1049) — added after chariot integration:
has_back_rank_mate: Boolean
has_smothered_mate: Boolean
has_sacrifice:      Boolean
has_zugzwang:       Boolean
has_double_check:   Boolean
has_interference:   Boolean
has_overloaded_piece: Boolean
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
| `motif(fork)` | `Filter(ReadRel("game_features"), ScalarFunction(equal, FieldRef("has_fork"), BoolLiteral(true)))` |
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
| `CostQueryRouter` | Inspect the plan: if it touches only `game_features` (boolean filter), route to DataFusion (fast columnar scan). If it has `sequence()` subqueries on small datasets, route to SQL (mature optimizer). |

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
    writer.rs            # Parquet file writing (accepts JSON batches from Java)
    compaction.rs        # Background small-file merging
  BUILD.bazel
```

Note: no chess logic or PGN parsing in Rust. Motif detection and Lichess
ingest live in Java (see "Lichess Ingest Architecture" below).

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
  Body: { "table": "game_features", "rows": [...] }
  Response: { "files_written": 1 }

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
- **7 new motifs**: back rank mate, smothered mate, sacrifice, zugzwang,
  double check, interference, overloaded piece — several of these need
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
QueryRouter.execute(plan)
  ├── SQL backend:
  │     substrait-java SqlConverter → SQL string + bind params
  │     → JDBC PreparedStatement → PostgreSQL → ResultSet
  │
  └── DataFusion backend:
        plan.toByteArray() → POST motif_query/v1/query/substrait
        → datafusion-substrait → LogicalPlan → Parquet scan
        → JSON results back to one_d4
  ↓
one_d4 formats response, returns to client
```

The backend is selected by `QueryRouter` based on a feature flag
(`QUERY_BACKEND=sql|datafusion`) or cost-based routing. During
migration, both backends can run in shadow mode to compare results.

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
Phase 1: SubstraitCompiler + motif_query crate scaffold.
         ChessQL compiles to Substrait. SqlQueryRouter converts
         Substrait → SQL and executes on PostgreSQL (functionally
         equivalent to today's SqlCompiler path). motif_query
         Rust service accepts Substrait plans and writes Parquet.
         Chess.com indexer dual-writes to PostgreSQL + Parquet.
         Queries still execute on PostgreSQL (via SqlQueryRouter).

Phase 2: Shadow mode.
         QueryRouter runs both backends in parallel. SQL results
         are returned to the client; DataFusion results are logged
         and diffed. Alerts on mismatches.

Phase 3: DataFusion primary.
         QUERY_BACKEND=datafusion. PostgreSQL game tables become
         write-only backup. Queries go to DataFusion.

Phase 4: Drop PostgreSQL game tables.
         Only indexing_requests and indexed_periods remain in PostgreSQL.
         Retention = delete partition directories on a schedule.

Phase 5: Lichess bulk ingest (after Phase 9 / issue #1049 lands).
         Run lichess_ingest Java CLI on historical Lichess dumps.
         All 18 motif detectors (11 existing + 7 new chariot-based)
         are included in bulk processing.
         Monthly cron to ingest new dumps as they're published.
```

## Implementation Plan

### Phase 1: SubstraitCompiler in chessql (2-3 days)

- Add `io.substrait:core` and `io.substrait:isthmus` maven dependencies
- New `SubstraitCompiler implements QueryCompiler<Plan>` in chessql library
  - Walk ChessQL AST → Substrait `ReadRel`, `FilterRel`, `SortRel`,
    `AggregateRel`, `JoinRel` nodes
  - Handle `MotifExpr` → boolean column filter
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
- Implement `writer.rs`: accept JSON rows, buffer, flush to partitioned
  Parquet
- axum server with `/v1/query/substrait`, `/v1/write`, `/health` endpoints
- Unit tests with in-memory Parquet roundtrips + a sample Substrait plan

### Phase 3: DataFusionQueryRouter + dual-write (1-2 days)

- New `DataFusionQueryRouter` in one_d4:
  - Serialize `Plan` to protobuf bytes
  - HTTP POST to `motif_query/v1/query/substrait`
  - Deserialize JSON response → `GameFeature` list
- New `ShadowQueryRouter` that runs both backends, compares results,
  logs mismatches, returns SQL backend results
- Dual-write from `IndexWorker`:
  - POST batches to `motif_query/v1/write` in addition to SQL INSERT
  - Config toggle: `MOTIF_QUERY_URL` (if set, dual-write is enabled)

### Phase 4: Cost-based routing (optional, 1 day)

- `CostQueryRouter` inspects the Substrait plan:
  - Simple boolean filter on `game_features` → DataFusion (fast scan)
  - `sequence()` with small expected result set → SQL (mature optimizer)
  - Aggregate queries → DataFusion (columnar aggregation)
- Configurable cost thresholds, with override via `QUERY_BACKEND` env var

### Phase 5: Lichess ingest pipeline — Java (3-5 days)

- New `lichess_ingest` binary target in `domains/games/apis/one_d4/`
- Streaming PGN parser: read `.pgn.zst` via `zstd-jni`, extract headers
- GM/title filter: parse `WhiteTitle`/`BlackTitle` headers, Elo thresholds
- Reuse existing Java motif detectors (GameReplayer + FeatureExtractor +
  all MotifDetector implementations — no porting needed)
- HTTP client to batch-POST results to `motif_query/v1/write`
- CLI interface: `java -jar lichess_ingest.jar --input ... --motif-query-url ...`
- Test against a small Lichess sample file
- **Dependency:** Should run after Phase 9 (issue #1049) lands so that
  the 7 new chariot-based motifs are included in the bulk ingest

### Phase 6: Query switchover + PostgreSQL game table removal (1 day)

- Set `QUERY_BACKEND=datafusion` as default
- Verify correctness via shadow mode logs (Phase 3)
- Remove `game_features` and `motif_occurrences` from Migration.java
- Update `RetentionWorker` to delete old partition directories instead
  of SQL DELETE
- Remove `SqlCompiler` (all compilation now goes through Substrait)

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
| Lichess PGN header parse + filter (Java, streaming) | ~100-200K games/sec |
| Full motif analysis (chariot + Java detectors) | ~2-5K games/sec |
| HTTP batch write to motif_query | ~10K rows/sec |
| End-to-end: 1 month Lichess (100M games, ~2M qualifying) | ~15-30 minutes |

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

5. **Phase 9 before or after Lichess ingest?** Ideally Phase 9 (issue
   #1049 — 7 new chariot-based motifs) lands before the first Lichess
   bulk ingest, so all 18 motifs are captured in one pass. If Phase 9 is
   delayed, we could ingest with the current 11 detectors and re-analyze
   later via the admin endpoint, but that doubles the processing cost.

6. **Java Parquet writer alternative?** Instead of HTTP POST to the Rust
   motif_query service, the Java `lichess_ingest` CLI could write Parquet
   directly using `org.apache.parquet:parquet-avro` or
   `org.apache.arrow:arrow-dataset`. This eliminates the HTTP hop but
   means two codepaths for Parquet writing (Java for ingest, Rust for
   query-time reads). The Rust write endpoint is simpler to keep
   consistent with the query schema.

7. **Substrait coverage for `sequence()`?** The `sequence()` construct
   compiles to a correlated EXISTS with self-joins on `motif_occurrences`.
   Substrait's `SetPredicateRel` (EXISTS) and `JoinRel` should handle
   this, but `datafusion-substrait` may not support all Substrait
   relation types. If `sequence()` hits a gap, we can either:
   (a) extend the DataFusion Substrait consumer,
   (b) fall back to SQL for queries containing `sequence()`, or
   (c) represent sequences differently (e.g. pre-materialized boolean
   columns like `has_sequence_fork_then_pin`).

8. **Substrait version pinning.** The `substrait-java` library and
   `datafusion-substrait` crate must agree on the Substrait protobuf
   spec version. Pin both to the same Substrait spec release (e.g.
   v0.42.x). Mismatches cause deserialization failures.

9. **When to remove `SqlCompiler`?** Once `SubstraitCompiler` +
   `SqlQueryRouter` is verified (Phase 1), the direct `SqlCompiler`
   is redundant — it produces the same SQL but without the Substrait
   intermediate step. Keep it as a fallback during Phase 1-2, remove
   in Phase 6 after DataFusion is primary.
