# Chess Game Indexer — MongoDB / DocumentDB Storage Backend

## Overview

Replace PostgreSQL with MongoDB (or AWS DocumentDB, which implements the MongoDB 5.0 wire protocol) as the primary storage backend. The document model is a natural fit for game data: each game is a self-contained document with nested motif occurrence data, eliminating the JSONB column and the relational impedance mismatch.

## Motivation

| Concern | PostgreSQL | MongoDB / DocumentDB |
|---------|-----------|---------------------|
| Schema evolution | ALTER TABLE migrations | Schemaless — add fields freely |
| Motif detail storage | JSONB column (second-class) | Native nested documents |
| Horizontal scaling | Read replicas, partitioning | Built-in sharding |
| Cold storage tiering | Manual partition management | TTL indexes, automatic expiry |
| Retention policies | Custom worker + cron | Native TTL indexes |
| Query flexibility | SQL (powerful but rigid schema) | MQL (flexible, document-native) |
| Ops complexity (AWS) | RDS — managed but schema-bound | DocumentDB — managed, elastic |
| Cost at scale | RDS instances are fixed-size | DocumentDB scales storage independently |

DocumentDB is especially attractive if the deployment is already AWS-native, since it provides MongoDB API compatibility with Aurora-style storage (replicated, durable, auto-scaling storage volume).

## Document Model

### indexing_requests Collection

```json
{
  "_id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "player": "hikaru",
  "platform": "chess.com",
  "startMonth": "2024-03",
  "endMonth": "2024-03",
  "status": "COMPLETED",
  "createdAt": {"$date": "2024-03-15T10:00:00Z"},
  "updatedAt": {"$date": "2024-03-15T10:05:00Z"},
  "errorMessage": null,
  "gamesIndexed": 147
}
```

### game_features Collection

```json
{
  "_id": "f1e2d3c4-b5a6-7890-abcd-ef1234567890",
  "requestId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "gameUrl": "https://www.chess.com/game/live/12345",
  "platform": "chess.com",
  "white": {
    "username": "hikaru",
    "elo": 2850
  },
  "black": {
    "username": "magnuscarlsen",
    "elo": 2830
  },
  "timeClass": "blitz",
  "eco": "B90",
  "result": "win",
  "playedAt": {"$date": "2024-03-15T18:30:00Z"},
  "numMoves": 42,
  "motifs": {
    "pin": true,
    "crossPin": false,
    "fork": true,
    "skewer": false,
    "discoveredAttack": false
  },
  "motifOccurrences": {
    "pin": [
      {"moveNumber": 15, "description": "Pin detected at move 15"}
    ],
    "fork": [
      {"moveNumber": 23, "description": "Fork detected at move 23"},
      {"moveNumber": 31, "description": "Fork detected at move 31"}
    ]
  },
  "pgn": "[Event \"Live Chess\"]..."
}
```

Key differences from the relational model:
- `white` and `black` are nested objects (not flat `white_username`, `white_elo` columns)
- `motifs` is a nested object with boolean fields (not top-level `has_*` columns)
- `motifOccurrences` replaces the `motifs_json` JSONB column with a native nested structure
- No foreign key constraint — `requestId` is a string reference, enforced at the application level

### Indexes

```javascript
// game_features collection
db.game_features.createIndex({ "gameUrl": 1 }, { unique: true });
db.game_features.createIndex({ "requestId": 1 });
db.game_features.createIndex({ "white.username": 1 });
db.game_features.createIndex({ "black.username": 1 });
db.game_features.createIndex({ "white.elo": 1 });
db.game_features.createIndex({ "black.elo": 1 });
db.game_features.createIndex({ "motifs.pin": 1 });
db.game_features.createIndex({ "motifs.fork": 1 });
db.game_features.createIndex({ "motifs.skewer": 1 });
db.game_features.createIndex({ "motifs.crossPin": 1 });
db.game_features.createIndex({ "motifs.discoveredAttack": 1 });
db.game_features.createIndex({ "eco": 1 });
db.game_features.createIndex({ "timeClass": 1 });
db.game_features.createIndex({ "playedAt": 1 });
db.game_features.createIndex({ "platform": 1 });

// Compound indexes for common query patterns
db.game_features.createIndex({ "white.elo": 1, "motifs.fork": 1 });
db.game_features.createIndex({ "black.elo": 1, "motifs.fork": 1 });
db.game_features.createIndex({ "eco": 1, "motifs.pin": 1 });

// TTL index for automatic retention (see Retention section)
db.game_features.createIndex({ "playedAt": 1 }, { expireAfterSeconds: 7776000 }); // 90 days

// indexing_requests collection
db.indexing_requests.createIndex({ "player": 1, "platform": 1, "startMonth": 1, "endMonth": 1 });
db.indexing_requests.createIndex({ "status": 1 });
```

---

## ChessQL Compiler Changes

The `SqlCompiler` currently emits parameterized SQL. A new `MongoCompiler` emits MongoDB query documents (`org.bson.Document` / `Bson` filters).

### New Class: MongoCompiler

```java
public class MongoCompiler {
    public Bson compile(Expr expr) {
        return compileExpr(expr);
    }

    private Bson compileExpr(Expr expr) {
        return switch (expr) {
            case OrExpr or -> Filters.or(
                or.operands().stream().map(this::compileExpr).toList()
            );
            case AndExpr and -> Filters.and(
                and.operands().stream().map(this::compileExpr).toList()
            );
            case NotExpr not -> Filters.not(compileExpr(not.operand()));
            case ComparisonExpr cmp -> compileComparison(cmp);
            case InExpr in -> Filters.in(resolveField(in.field()), in.values());
            case MotifExpr motif -> Filters.eq(resolveMotifField(motif.motifName()), true);
        };
    }

    private Bson compileComparison(ComparisonExpr cmp) {
        String field = resolveField(cmp.field());
        Object value = cmp.value();
        return switch (cmp.operator()) {
            case "="  -> Filters.eq(field, value);
            case "!=" -> Filters.ne(field, value);
            case "<"  -> Filters.lt(field, value);
            case "<=" -> Filters.lte(field, value);
            case ">"  -> Filters.gt(field, value);
            case ">=" -> Filters.gte(field, value);
            default -> throw new IllegalArgumentException("Invalid operator: " + cmp.operator());
        };
    }
}
```

### Field Mapping Changes

The document model uses nested paths instead of flat column names:

| ChessQL Field    | PostgreSQL Column  | MongoDB Field Path         |
|------------------|--------------------|----------------------------|
| `white.elo`      | `white_elo`        | `white.elo`                |
| `black.elo`      | `black_elo`        | `black.elo`                |
| `white.username` | `white_username`   | `white.username`           |
| `black.username` | `black_username`   | `black.username`           |
| `time.class`     | `time_class`       | `timeClass`                |
| `num.moves`      | `num_moves`        | `numMoves`                 |
| `game.url`       | `game_url`         | `gameUrl`                  |
| `played.at`      | `played_at`        | `playedAt`                 |
| `eco`            | `eco`              | `eco`                      |
| `result`         | `result`           | `result`                   |
| `platform`       | `platform`         | `platform`                 |

Motif field mapping:

| ChessQL              | PostgreSQL                                                    | MongoDB                      |
|----------------------|---------------------------------------------------------------|------------------------------|
| `motif(pin)`         | `EXISTS (SELECT 1 FROM motif_occurrences mo WHERE mo.game_url = g.game_url AND mo.motif = 'PIN')` | `motifs.pin = true`          |
| `motif(cross_pin)`   | `EXISTS (... mo.motif = 'CROSS_PIN')`                        | `motifs.crossPin = true`     |
| `motif(fork)`        | `EXISTS (... mo.motif = 'ATTACK' GROUP BY ply, attacker HAVING COUNT(*) >= 2)` | `motifs.fork = true`         |
| `motif(skewer)`      | `EXISTS (... mo.motif = 'SKEWER')`                           | `motifs.skewer = true`       |
| `motif(discovered_attack)` | `EXISTS (... mo.motif = 'ATTACK' AND mo.is_discovered = TRUE)` | `motifs.discoveredAttack = true` |

The dotted ChessQL field syntax (`white.elo`) maps directly to MongoDB's dot-notation for nested documents. This is a natural fit — the ChessQL grammar was accidentally designed for document databases.

### Compiler Interface

Abstract the compilation target behind an interface so both backends coexist:

```java
public interface QueryCompiler<T> {
    T compile(Expr expr);
}

public class SqlCompiler implements QueryCompiler<CompiledQuery> { ... }
public class MongoCompiler implements QueryCompiler<Bson> { ... }
```

The `QueryController` selects the compiler based on the active storage backend.

---

## DAO Changes

### New Interface: GameFeatureStore

Abstract the storage layer so PostgreSQL and MongoDB implementations are swappable:

```java
public interface GameFeatureStore {
    void insert(GameFeatureDocument doc);
    List<GameFeatureDocument> query(Object compiledQuery, int limit, int offset);
}

public interface IndexingRequestStore {
    String create(String player, String platform, String startMonth, String endMonth);
    Optional<IndexingRequestDocument> findById(String id);
    void updateStatus(String id, String status, String errorMessage, int gamesIndexed);
}
```

### MongoGameFeatureStore

```java
public class MongoGameFeatureStore implements GameFeatureStore {
    private final MongoCollection<Document> collection;

    public MongoGameFeatureStore(MongoDatabase database) {
        this.collection = database.getCollection("game_features");
    }

    @Override
    public void insert(GameFeatureDocument doc) {
        Document mongoDoc = toDocument(doc);
        collection.insertOne(mongoDoc);
        // For idempotency: catch DuplicateKeyException (unique index on gameUrl)
    }

    @Override
    public List<GameFeatureDocument> query(Object compiledQuery, int limit, int offset) {
        Bson filter = (Bson) compiledQuery;
        return collection.find(filter)
                .skip(offset)
                .limit(limit)
                .map(this::fromDocument)
                .into(new ArrayList<>());
    }
}
```

### MongoIndexingRequestStore

```java
public class MongoIndexingRequestStore implements IndexingRequestStore {
    private final MongoCollection<Document> collection;

    @Override
    public String create(String player, String platform, String startMonth, String endMonth) {
        String id = UUID.randomUUID().toString();
        Document doc = new Document()
                .append("_id", id)
                .append("player", player)
                .append("platform", platform)
                .append("startMonth", startMonth)
                .append("endMonth", endMonth)
                .append("status", "PENDING")
                .append("createdAt", new Date())
                .append("updatedAt", new Date())
                .append("gamesIndexed", 0);
        collection.insertOne(doc);
        return id;
    }

    @Override
    public void updateStatus(String id, String status, String errorMessage, int gamesIndexed) {
        collection.updateOne(
                Filters.eq("_id", id),
                Updates.combine(
                        Updates.set("status", status),
                        Updates.set("errorMessage", errorMessage),
                        Updates.set("gamesIndexed", gamesIndexed),
                        Updates.set("updatedAt", new Date())
                )
        );
    }
}
```

---

## DocumentDB-Specific Considerations

AWS DocumentDB implements MongoDB 5.0 API with some differences. These require attention:

### Supported Features (no changes needed)

- CRUD operations (insertOne, find, updateOne, deleteMany)
- Query filters (Filters.eq, gt, gte, lt, lte, ne, in, and, or, not)
- Indexes (single field, compound, unique, TTL)
- Skip / limit pagination
- Dot-notation for nested documents
- `$set` update operator

### Unsupported or Limited Features

| Feature | MongoDB | DocumentDB | Workaround |
|---------|---------|------------|------------|
| `$jsonSchema` validation | Full support | Not supported | Validate in application layer |
| Change streams | Full support | Supported (with limitations on sharded collections) | Use polling if change streams are unreliable |
| Transactions (multi-doc) | Full support | Supported on 4.0+ compatible clusters | Use single-document operations where possible |
| `$merge` aggregation | Supported | Not supported | Use application-side read + write for cold storage export |
| `$out` aggregation | Supported | Limited | Same as above |
| `$lookup` (joins) | Supported | Supported but slower | Denormalize — the document model already avoids joins |
| Text search (`$text`) | Full text index | Not supported | Use regex or application-side filtering for text search |
| Collation | Full support | Partial | Use case-insensitive regex for username matching |
| Aggregation pipeline | Full support | Subset supported | Test specific pipeline stages against DocumentDB |

### Required DocumentDB Cluster Configuration

```
Engine version:        5.0.0 (latest DocumentDB version)
Instance class:        db.r6g.large (minimum for production)
Storage encryption:    Enabled (AES-256)
TLS:                   Required (DocumentDB enforces TLS by default)
Cluster parameter group:
  - tls: enabled
  - audit_logs: enabled
  - profiler: enabled (threshold: 100ms)
```

### Connection String

```
mongodb://<username>:<password>@<cluster-endpoint>:27017/?tls=true&tlsCAFile=global-bundle.pem&replicaSet=rs0&readPreference=secondaryPreferred&retryWrites=false
```

Key differences from standard MongoDB connections:
- **`retryWrites=false`**: DocumentDB does not support retryable writes. The application must handle write retries.
- **`tlsCAFile`**: DocumentDB requires TLS with the AWS-provided CA bundle.
- **`replicaSet=rs0`**: DocumentDB always uses replica set `rs0`.
- **`readPreference=secondaryPreferred`**: Route reads to replicas to reduce primary load.

### Application-Side Retry for Writes

Since DocumentDB does not support `retryWrites`, add retry logic to the store layer:

```java
public class RetryableMongoStore {
    private static final int MAX_RETRIES = 3;

    protected <T> T withRetry(Supplier<T> operation) {
        for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
            try {
                return operation.get();
            } catch (MongoException e) {
                if (isRetryable(e) && attempt < MAX_RETRIES) {
                    sleep(100L * attempt); // linear backoff
                    continue;
                }
                throw e;
            }
        }
        throw new IllegalStateException("Unreachable");
    }

    private boolean isRetryable(MongoException e) {
        // Network errors, not-primary errors
        return e.hasErrorLabel("RetryableWriteError")
            || e.getCode() == 10107   // NotWritablePrimary
            || e.getCode() == 13435;  // NotPrimaryNoSecondaryOk
    }
}
```

---

## Retention with TTL Indexes

MongoDB/DocumentDB TTL indexes provide automatic document expiry — no retention worker needed for simple cases.

### Hot → Delete (Simple)

```javascript
// Documents deleted 90 days after playedAt
db.game_features.createIndex({ "playedAt": 1 }, { expireAfterSeconds: 7776000 });
```

DocumentDB runs a background task every 60 seconds to remove expired documents. No application code needed.

### Hot → Cold (With Archive)

For the tiered retention policy from ROADMAP.md, TTL indexes handle the delete tier, but hot→cold migration still needs application logic:

```
                TTL = none              TTL = 90 days             S3 lifecycle = 365 days
   ┌──────────────────┐    Export    ┌──────────────────┐       ┌──────────────┐
   │  game_features   │ ──────────► │  S3 NDJSON.gz    │ ────► │   Deleted    │
   │  (DocumentDB)    │  App logic  │  (cold storage)  │  Auto │              │
   │  Auto-expire     │             └──────────────────┘       └──────────────┘
   │  after 90 days   │
   └──────────────────┘
```

The export step runs before TTL expiry:

```java
public class ColdStorageExporter {
    public void exportExpiringSoon(int daysBeforeExpiry) {
        // Find documents expiring within N days
        Instant cutoff = Instant.now().minus(Duration.ofDays(90 - daysBeforeExpiry));
        Bson filter = Filters.lt("playedAt", Date.from(cutoff));

        // Stream to S3 as NDJSON
        try (MongoCursor<Document> cursor = collection.find(filter).iterator()) {
            while (cursor.hasNext()) {
                s3Writer.writeLine(cursor.next().toJson());
            }
        }
    }
}
```

Run this daily, exporting documents that will expire in the next 7 days. TTL handles the actual deletion.

### Adjusting TTL After Creation

DocumentDB supports modifying TTL index expiry:

```javascript
db.runCommand({
  collMod: "game_features",
  index: {
    keyPattern: { "playedAt": 1 },
    expireAfterSeconds: 15552000  // Change to 180 days
  }
});
```

---

## Migration Path from PostgreSQL

### Phase 1: Dual-Write

Write to both PostgreSQL and DocumentDB simultaneously. Read from PostgreSQL. This validates the DocumentDB schema and write path without risk.

```java
public class DualWriteGameFeatureStore implements GameFeatureStore {
    private final GameFeatureDao postgresDao;
    private final MongoGameFeatureStore mongoStore;

    @Override
    public void insert(GameFeatureDocument doc) {
        postgresDao.insert(toRelationalRow(doc));
        try {
            mongoStore.insert(doc);
        } catch (Exception e) {
            LOG.warn("DocumentDB write failed, PostgreSQL is authoritative", e);
        }
    }
}
```

### Phase 2: Dual-Read Validation

Read from both and compare results. Log discrepancies. This validates the MongoCompiler produces equivalent results to SqlCompiler.

### Phase 3: Switch Reads

Point reads to DocumentDB. PostgreSQL is still written to as a fallback.

### Phase 4: Remove PostgreSQL

Stop writing to PostgreSQL. Remove the relational DAOs and SqlCompiler (or keep as an option via configuration).

---

## Dependencies

### Maven Artifacts

```
# bazel/java.MODULE.bazel
"org.mongodb:mongodb-driver-sync:5.1.0",
"org.mongodb:bson:5.1.0",
```

Bazel labels:
```
@maven//:org_mongodb_mongodb_driver_sync
@maven//:org_mongodb_bson
```

### New BUILD Targets

```
# domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/db/mongo/BUILD.bazel
load("@rules_java//java:java_library.bzl", "java_library")

java_library(
    name = "mongo",
    srcs = [
        "MongoClientFactory.java",
        "MongoGameFeatureStore.java",
        "MongoIndexingRequestStore.java",
        "MongoMigration.java",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//domains/games/apis/one_d4/db",
        "@maven//:org_mongodb_mongodb_driver_sync",
        "@maven//:org_mongodb_bson",
        "@maven//:org_slf4j_slf4j_api",
    ],
)
```

```
# domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/chessql/compiler/mongo/BUILD.bazel
load("@rules_java//java:java_library.bzl", "java_library")

java_library(
    name = "mongo",
    srcs = ["MongoCompiler.java"],
    visibility = ["//visibility:public"],
    deps = [
        "//domains/games/apis/one_d4/chessql/ast",
        "@maven//:org_mongodb_mongodb_driver_sync",
        "@maven//:org_mongodb_bson",
    ],
)
```

---

## Configuration

```
INDEXER_STORAGE=postgres|docdb          # Storage backend (default: postgres)
INDEXER_DOCDB_URI=mongodb://...         # DocumentDB connection string
INDEXER_DOCDB_DATABASE=indexer          # Database name
INDEXER_DOCDB_TLS_CA_FILE=/path/to/global-bundle.pem  # AWS CA bundle
INDEXER_DOCDB_RETRY_WRITES=false        # Must be false for DocumentDB
```

### IndexerModule Wiring

```java
@Context
public GameFeatureStore gameFeatureStore(
        @Value("${indexer.storage:postgres}") String storage,
        ...) {
    return switch (storage) {
        case "docdb" -> {
            MongoClient client = MongoClientFactory.create(docdbUri, caFile);
            MongoDatabase db = client.getDatabase(database);
            new MongoMigration(db).run(); // create indexes
            yield new MongoGameFeatureStore(db);
        }
        default -> new PostgresGameFeatureStore(dataSource);
    };
}

@Context
public QueryCompiler<?> queryCompiler(
        @Value("${indexer.storage:postgres}") String storage) {
    return switch (storage) {
        case "docdb" -> new MongoCompiler();
        default -> new SqlCompiler();
    };
}
```

---

## Cost Comparison

### Small (< 100K games)

| Resource | PostgreSQL (RDS) | DocumentDB |
|----------|-----------------|------------|
| Instance | db.t4g.micro — $15/mo | db.t3.medium — $58/mo |
| Storage | 20GB gp3 — $2/mo | Auto-scaling — $0.10/GB ($2/mo) |
| I/O | Included in gp3 | $0.20 per 1M reads, $0.20 per 1M writes |
| Backup | Free (7 days) | Free (1 day), $0.02/GB beyond |
| **Total** | **~$17/mo** | **~$62/mo** |

DocumentDB is ~3.5x more expensive at small scale due to higher minimum instance cost. **PostgreSQL wins here.**

### Medium (100K - 1M games)

| Resource | PostgreSQL (RDS) | DocumentDB |
|----------|-----------------|------------|
| Instance | db.t4g.medium — $50/mo | db.r6g.large — $195/mo |
| Storage | 100GB gp3 — $10/mo | 10GB auto — $1/mo |
| I/O | Included | ~$5/mo |
| Read replica | db.t4g.medium — $50/mo | db.r6g.large — $195/mo |
| **Total** | **~$110/mo** | **~$396/mo** |

DocumentDB remains more expensive. However, schema evolution is free (no ALTER TABLE downtime), and TTL indexes eliminate the retention worker. **PostgreSQL still wins on cost, DocumentDB wins on operational simplicity.**

### Large (1M+ games, multi-motif, frequent schema changes)

| Resource | PostgreSQL (RDS) | DocumentDB |
|----------|-----------------|------------|
| Primary | db.r7g.large — $250/mo | db.r6g.xlarge — $390/mo |
| Replicas (2x) | $500/mo | $780/mo |
| Storage | 500GB gp3 — $50/mo | 50GB auto — $5/mo |
| I/O | Included | ~$30/mo |
| Partition management | Manual (ops time) | Automatic (TTL) |
| Schema migrations | Downtime risk | Zero-downtime |
| **Total** | **~$800/mo** | **~$1,205/mo** |

DocumentDB is ~50% more expensive on compute but eliminates partition management, schema migration risk, and the retention worker. **Choose based on whether ops time or compute cost dominates your budget.**

### When to Choose DocumentDB

- Frequent schema changes (new motifs, new game metadata fields)
- AWS-native deployment where DocumentDB is already in use
- Team more familiar with MongoDB than PostgreSQL
- Retention policy is a hard requirement and TTL indexes remove significant ops burden
- Query patterns are primarily document lookups, not complex joins or aggregations

### When to Stay on PostgreSQL

- Cost-sensitive deployment
- Complex analytical queries beyond simple ChessQL (window functions, CTEs)
- Need JSONB querying within motif occurrence data
- Team more familiar with SQL
- Smaller dataset where schema migration is infrequent

---

## Files to Create / Modify

| File | Action | Description |
|------|--------|-------------|
| `bazel/java.MODULE.bazel` | Modify | Add MongoDB driver deps |
| `db/GameFeatureStore.java` | Create | Storage interface |
| `db/IndexingRequestStore.java` | Create | Storage interface |
| `db/PostgresGameFeatureStore.java` | Rename/refactor | Existing `GameFeatureDao` implements interface |
| `db/PostgresIndexingRequestStore.java` | Rename/refactor | Existing `IndexingRequestDao` implements interface |
| `db/mongo/MongoClientFactory.java` | Create | DocumentDB connection builder (TLS, CA bundle) |
| `db/mongo/MongoGameFeatureStore.java` | Create | MongoDB implementation of GameFeatureStore |
| `db/mongo/MongoIndexingRequestStore.java` | Create | MongoDB implementation of IndexingRequestStore |
| `db/mongo/MongoMigration.java` | Create | Index creation on startup |
| `chessql/compiler/QueryCompiler.java` | Create | Compiler interface |
| `chessql/compiler/mongo/MongoCompiler.java` | Create | ChessQL → Bson filter compiler |
| `IndexerModule.java` | Modify | Storage backend switch |
| `worker/IndexWorker.java` | Modify | Use `GameFeatureStore` interface |
| `api/QueryController.java` | Modify | Use `QueryCompiler` interface |
| `api/IndexController.java` | Modify | Use `IndexingRequestStore` interface |

~8 new files, ~6 modified files, ~800 lines of new code.

---

## Leveraging the Rust doc_db Service

The repository contains an existing Rust-based document database service at `rust/doc_db/` that wraps MongoDB with a gRPC interface. This section analyzes its suitability for the chess indexer and documents required changes.

### Current doc_db Architecture

```
┌─────────────────┐     gRPC      ┌─────────────────┐
│  Java Indexer   │ ────────────► │  Rust doc_db    │
│  (client)       │               │  (tonic server) │
└─────────────────┘               └────────┬────────┘
                                           │
                                    ┌──────▼──────┐
                                    │   MongoDB   │
                                    │  /DocumentDB│
                                    └─────────────┘
```

### Current doc_db API (from `protos/doc_db/doc_db.proto`)

```protobuf
service DocDb {
  rpc InsertDoc (InsertDocRequest) returns (InsertDocResponse) {}
  rpc UpdateDoc (UpdateDocRequest) returns (UpdateDocResponse) {}
  rpc FindDocById (FindDocByIdRequest) returns (FindDocByIdResponse) {}
  rpc FindDoc (FindDocRequest) returns (FindDocResponse) {}
}

message Document {
  string id = 1;
  string version = 2;
  bytes bytes = 3;
  map<string, string> tags = 4;
}
```

### Current doc_db Data Model

| Field     | Type                   | Purpose                                      |
|-----------|------------------------|----------------------------------------------|
| `_id`     | ObjectId               | MongoDB document ID                          |
| `bytes`   | `Vec<u8>`              | Opaque payload (serialized document)         |
| `version` | String (UUID)          | Optimistic concurrency control               |
| `tags`    | `HashMap<String,String>` | Queryable string key-value pairs           |

### Suitability Analysis

| Requirement | Current doc_db Support | Gap |
|-------------|----------------------|-----|
| Store game documents | Partial — `bytes` field can hold serialized JSON | No native nested document support |
| Query by numeric fields (`white.elo >= 2500`) | **No** — tags are string equality only | Cannot express range queries |
| Query by motif (`motif(fork)`) | Partial — could encode as tag `"has_fork": "true"` | String equality; no EXISTS subquery support |
| Complex boolean queries (AND/OR/NOT) | **No** — `FindDoc` matches exact tag set | No boolean combinators |
| Pagination (limit/offset) | **No** — returns single document | Cannot paginate results |
| Batch queries | **No** — `find_one` only | No `find_many` |
| TTL / retention | **No** — no TTL support | Would need MongoDB-side TTL index + bypass doc_db |
| Indexing | **No** — no index management | Must create indexes out-of-band |

**Verdict**: The current doc_db API is designed for simple key-value / tag-based document storage with optimistic locking. It is **not suitable** for the chess indexer's ChessQL query requirements without significant extensions.

### Required doc_db Extensions

To support the chess indexer, doc_db would need the following additions:

#### 1. Rich Query Support

New RPC for MongoDB-style queries:

```protobuf
message QueryRequest {
  string collection = 1;
  bytes filter_bson = 2;     // Serialized BSON filter document
  int32 limit = 3;
  int32 skip = 4;
  bytes sort_bson = 5;       // Optional sort specification
}

message QueryResponse {
  repeated Document docs = 1;
  int64 total_count = 2;     // For pagination UI
}

service DocDb {
  // ... existing RPCs ...
  rpc Query (QueryRequest) returns (QueryResponse) {}
}
```

The `filter_bson` field would accept a raw BSON-serialized MongoDB query document, allowing the Java client to build arbitrary queries using the MongoDB Java driver's `Filters` API and serialize to BSON.

**Rust implementation sketch**:

```rust
async fn query(&self, request: Request<QueryRequest>) -> Result<Response<QueryResponse>, Status> {
    let req = request.into_inner();
    let db_name = read_db_name_from_metadata(request.metadata())?;

    let filter: BsonDocument = bson::from_slice(&req.filter_bson)
        .map_err(|e| Status::invalid_argument(format!("invalid filter: {}", e)))?;

    let collection = self.client.database(&db_name).collection::<MongoDoc>(&req.collection);

    let options = FindOptions::builder()
        .limit(req.limit as i64)
        .skip(req.skip as u64)
        .build();

    let mut cursor = collection.find(filter, options).await
        .map_err(|e| Status::internal(e.to_string()))?;

    let mut docs = Vec::new();
    while let Some(doc) = cursor.try_next().await? {
        docs.push(to_proto_document(doc));
    }

    Ok(Response::new(QueryResponse { docs, total_count: docs.len() as i64 }))
}
```

#### 2. Batch Insert

For efficient game indexing:

```protobuf
message BatchInsertRequest {
  string collection = 1;
  repeated DocumentEgg docs = 2;
}

message BatchInsertResponse {
  repeated string ids = 1;
  int32 inserted_count = 2;
}

service DocDb {
  rpc BatchInsert (BatchInsertRequest) returns (BatchInsertResponse) {}
}
```

#### 3. Index Management

```protobuf
message CreateIndexRequest {
  string collection = 1;
  bytes index_spec_bson = 2;  // e.g., {"white.elo": 1}
  IndexOptions options = 3;
}

message IndexOptions {
  bool unique = 1;
  int64 expire_after_seconds = 2;  // For TTL indexes
  string name = 3;
}

message CreateIndexResponse {
  string index_name = 1;
}

service DocDb {
  rpc CreateIndex (CreateIndexRequest) returns (CreateIndexResponse) {}
}
```

#### 4. Native Document Storage (Alternative to bytes)

Instead of serializing to `bytes`, store native BSON documents:

```protobuf
message RichDocument {
  string id = 1;
  string version = 2;
  bytes bson_content = 3;  // Full BSON document, not just payload
}

message InsertRichDocRequest {
  string collection = 1;
  bytes bson_content = 2;  // Client serializes full document
}
```

This allows MongoDB to index nested fields directly without the doc_db service needing to understand the schema.

### Integration Architecture Options

#### Option A: Extend doc_db (Recommended if Rust investment is desired)

```
┌─────────────────┐     gRPC      ┌─────────────────┐
│  Java Indexer   │ ────────────► │  Rust doc_db    │
│                 │               │  (extended)     │
│  - ChessQL      │               │  - Query()      │
│  - MongoCompiler│               │  - BatchInsert()│
│    → BSON bytes │               │  - CreateIndex()│
└─────────────────┘               └────────┬────────┘
                                           │
                                    ┌──────▼──────┐
                                    │  DocumentDB │
                                    └─────────────┘
```

**Pros**:
- Single MongoDB connection pool managed by Rust service
- Rust service can add caching, rate limiting, connection management
- Language-agnostic — other services can use the gRPC API
- Existing doc_db codebase provides foundation

**Cons**:
- Requires Rust development for new RPCs
- Additional network hop (Java → gRPC → MongoDB)
- BSON serialization/deserialization overhead at both ends
- More complex deployment (two services)

#### Option B: Direct MongoDB from Java (Current doc recommendation)

```
┌─────────────────┐
│  Java Indexer   │
│                 │
│  - ChessQL      │──────────────────┐
│  - MongoCompiler│                  │
│  - MongoDriver  │                  │
└─────────────────┘                  │
                              ┌──────▼──────┐
                              │  DocumentDB │
                              └─────────────┘
```

**Pros**:
- No additional service to deploy
- Native MongoDB driver has full feature support
- Lower latency (no gRPC hop)
- Simpler debugging

**Cons**:
- Each Java service manages its own connection pool
- MongoDB-specific code in Java (less portable)

#### Option C: Hybrid — Use doc_db for Simple Ops, Direct for Queries

```
┌─────────────────┐     gRPC      ┌─────────────────┐
│  Java Indexer   │ ────────────► │  Rust doc_db    │ ← InsertDoc, UpdateDoc
│                 │               │  (unchanged)    │
│  - ChessQL      │               └────────┬────────┘
│  - MongoCompiler│──────────────────┐     │
│  - MongoDriver  │                  │     │
└─────────────────┘                  │     │
                              ┌──────▼─────▼┐
                              │  DocumentDB │
                              └─────────────┘
```

Use doc_db for writes (with optimistic locking), direct MongoDB driver for complex queries. This avoids extending doc_db while leveraging its write-side features.

### Estimated Changes to doc_db

| Change | Complexity | Lines of Rust |
|--------|-----------|---------------|
| Add `Query` RPC with BSON filter | Medium | ~100 |
| Add `BatchInsert` RPC | Low | ~50 |
| Add `CreateIndex` RPC | Low | ~40 |
| Add pagination to `FindDoc` | Low | ~30 |
| Proto file updates | Low | ~50 |
| Tests for new RPCs | Medium | ~200 |
| **Total** | | **~470 lines** |

### Java Client for Extended doc_db

Generate Java gRPC stubs from the updated proto:

```
# bazel/java.MODULE.bazel
"io.grpc:grpc-netty-shaded:1.62.2",
"io.grpc:grpc-protobuf:1.62.2",
"io.grpc:grpc-stub:1.62.2",
```

```java
public class DocDbGameFeatureStore implements GameFeatureStore {
    private final DocDbGrpc.DocDbBlockingStub stub;
    private final ObjectMapper mapper;

    @Override
    public List<GameFeatureDocument> query(Object compiledQuery, int limit, int offset) {
        Bson bsonFilter = (Bson) compiledQuery;
        byte[] filterBytes = toBsonBytes(bsonFilter);

        QueryRequest request = QueryRequest.newBuilder()
                .setCollection("game_features")
                .setFilterBson(ByteString.copyFrom(filterBytes))
                .setLimit(limit)
                .setSkip(offset)
                .build();

        QueryResponse response = stub.query(request);
        return response.getDocsList().stream()
                .map(this::fromProtoDocument)
                .toList();
    }
}
```

### Recommendation

**For the chess indexer specifically**: Use **Option B (Direct MongoDB from Java)** as documented in the main sections above. The doc_db service's current API is not a good fit, and extending it adds complexity without significant benefit for a single-consumer scenario.

**Consider extending doc_db if**:
- Multiple services (not just the indexer) need MongoDB access
- You want to centralize connection management, caching, and rate limiting
- The team prefers Rust for database interaction logic
- You need a language-agnostic document API for future polyglot services

**Keep doc_db as-is for**:
- Its original use case (simple document storage with optimistic locking)
- Services that only need tag-based lookup (not range queries)
- Write-heavy workloads where the version-based update pattern is valuable
