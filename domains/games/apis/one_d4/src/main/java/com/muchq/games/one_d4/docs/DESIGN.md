# Chess Game Indexer — System Design

## Overview

A Micronaut service that indexes chess games from chess.com (lichess planned), extracts tactical motifs via position replay, and exposes a custom query language (ChessQL) for searching indexed games.

**Stack**: Java 21, Micronaut 4.x, Bazel, PostgreSQL, chariot (chess library), JDBI3, HikariCP

## Architecture

```
                         ┌──────────────┐
                         │   Client     │
                         └──────┬───────┘
                                │
                    ┌───────────▼───────────┐
                    │    Micronaut HTTP     │
                    │   (Netty + JAX-RS)    │
                    ├───────────┬───────────┤
                    │ IndexCtrl │ QueryCtrl │
                    └─────┬─────┴─────┬─────┘
                          │           │
                   INSERT PENDING      │
                          │     ┌─────▼───────────┐
                          │     │  ChessQL        │
                          │     │  Lexer→Parser→  │
                          │     │  Compiler→SQL   │
                          │     └─────┬───────────┘
                          │           │
     ┌────────────────────▼────────┐  │
     │  indexing_requests          │  │   IndexQueue (in-memory)
     │  — the work queue —         │  │   wake-up nudge only,
     └────┬───────────────▲────────┘  │   payload ignored
      claimNext     fenced writes     │            ╎
          │               │           │            ╎
          │      ┌────────┴──────┐    │            ╎
          └─────►│  IndexWorker  │◄╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╌╯
                 │   (daemon,    │    │
                 │    any host)  │    │
                 └───────┬───────┘    │
                         │            │
              ┌──────────▼─────┐      │
              │ FeatureExtract │      │
              │  PgnParser     │      │
              │  GameReplayer  │      │
              │  MotifDetect[] │      │
              └──────┬─────────┘      │
                     │                │
               ┌─────▼────────────────▼──────┐
               │      PostgreSQL / H2        │
               │  indexing_requests          │
               │  game_features              │
               │  motif_occurrences          │
               └─────────────────────────────┘

Any instance's worker may claim any queued row, so the arrow into IndexWorker
is not the nudge — the nudge only saves it waiting out the poll interval.
```

## Package Structure

```
com.muchq.indexer/
  App.java                          Micronaut entry point
  IndexerModule.java                @Factory — wires all beans

  api/
    IndexController.java            POST /v1/index, GET /v1/index/{id}
    QueryController.java            POST /v1/query

  api/dto/
    IndexRequest.java               Inbound: player, platform, month range
    IndexResponse.java              Outbound: id, player, platform, startMonth, endMonth, status, gamesIndexed, errorMessage
    QueryRequest.java               Inbound: ChessQL query string, limit, offset
    QueryResponse.java              Outbound: list of GameFeatureRow, count
    GameFeatureRow.java             Projection of game_features for API consumers

  queue/
    IndexQueue.java                 Interface: enqueue, poll, size
    IndexMessage.java               Queue payload record
    InMemoryIndexQueue.java         LinkedBlockingQueue implementation

  worker/
    IndexWorker.java                Processes IndexMessages: fetch→parse→detect→store
    IndexWorkerLifecycle.java       Daemon thread started on ServerStartupEvent

  db/
    DataSourceFactory.java          HikariCP DataSource builder
    Migration.java                  DDL bootstrap (CREATE TABLE IF NOT EXISTS)
    IndexingRequestDao.java         CRUD for indexing_requests (JDBI3 fluent API)
    IndexingRequestStore.java       Store interface for indexing_requests
    GameFeatureDao.java             Insert + parameterized query for game_features (JDBI3 fluent API)
    GameFeatureStore.java           Store interface for game_features
    IndexedPeriodDao.java           Period cache upserts (JDBI3 fluent API)
    IndexedPeriodStore.java         Store interface for indexed_periods

  engine/
    PgnParser.java                  Extracts headers + movetext from PGN strings
    GameReplayer.java               Replays SAN moves on ReplayBoard → List<PositionContext>
    ReplayBoard.java                Internal mutable board: SAN application + FEN emission
    FeatureExtractor.java           Orchestrates replay + all motif detectors

  engine/model/
    ParsedGame.java                 Headers map + movetext string
    GameFeatures.java               Set<Motif>, numMoves, occurrence details
    Motif.java                      Enum: PIN, CROSS_PIN, FORK, SKEWER, DISCOVERED_ATTACK, CHECK, CHECKMATE, PROMOTION, PROMOTION_WITH_CHECK, PROMOTION_WITH_CHECKMATE
    PositionContext.java            moveNumber, FEN, whiteToMove, lastMove

  motifs/
    MotifDetector.java              Interface: motif(), detect(positions)
    PinDetector.java                Ray-casting from king to find pinned pieces
    CrossPinDetector.java           Piece pinned along two axes simultaneously
    ForkDetector.java               Piece attacking 2+ valuable enemy pieces
    SkewerDetector.java             Sliding attack through a more valuable piece
    DiscoveredAttackDetector.java   Piece moves to reveal sliding attacker behind it
    CheckDetector.java              Move notation ends with '+'
    CheckmateDetector.java          Move notation ends with '#'
    PromotionDetector.java          Move notation contains '=' without check/mate
    PromotionWithCheckDetector.java Move notation contains '=' and ends with '+'
    PromotionWithCheckmateDetector.java Move notation contains '=' and ends with '#'

  chessql/
    lexer/   TokenType, Token, Lexer
    ast/     Expr (sealed), OrExpr, AndExpr, NotExpr, ComparisonExpr, InExpr, MotifExpr
    parser/  Parser (recursive descent), ParseException
    compiler/ SqlCompiler, CompiledQuery
```

## Data Flow

### Indexing

1. Client sends `POST /v1/index` with player, platform, month range
2. `IndexController` creates a row in `indexing_requests` (status=PENDING). That row *is* the work queue; an `IndexMessage` is also enqueued locally, but only as a wake-up nudge whose payload is ignored
3. `IndexWorkerLifecycle` on any instance claims the oldest unheld row (`claimNext`), taking a lease it renews for the duration
4. `IndexWorker.process()`:
   - Sets status to PROCESSING
   - Iterates months, fetches games from chess.com API via `ChessClient`
   - For each game: `FeatureExtractor.extract(pgn)` → `PgnParser` → `GameReplayer` → `MotifDetector[]`
   - Inserts `GameFeatureRow` into `game_features` (ON CONFLICT DO NOTHING for idempotency)
   - Updates `games_indexed` count periodically
   - Sets status to COMPLETED or FAILED

### Querying

1. Client sends `POST /v1/query` with a ChessQL string, limit, offset
2. `QueryController` parses ChessQL → AST → compiles to parameterized SQL
3. `GameFeatureDao.query()` executes the SQL against `game_features`
4. Results mapped to API DTOs and returned

## DB Schema

### indexing_requests

| Column        | Type         | Notes                          |
|---------------|--------------|--------------------------------|
| id            | UUID PK      | gen_random_uuid()              |
| player        | VARCHAR(255) | chess.com username             |
| platform      | VARCHAR(50)  | "chess.com", "lichess" (future)|
| start_month   | VARCHAR(7)   | "2024-01"                      |
| end_month     | VARCHAR(7)   | "2024-03"                      |
| status        | VARCHAR(20)  | PENDING→PROCESSING→COMPLETED/FAILED |
| created_at    | TIMESTAMP    | Immutable                      |
| updated_at    | TIMESTAMP    | Updated on status change       |
| error_message | TEXT         | Populated on FAILED            |
| games_indexed | INT          | Running count during processing|
| exclude_bullet | BOOLEAN     | Part of the dedupe tuple       |
| dedupe_key    | VARCHAR      | UNIQUE. Held while live, NULLed on a terminal status — one live request per (player, platform, range, exclude_bullet) |
| owner_id      | VARCHAR(128) | The worker process holding the lease; the fencing token every write is conditioned on |
| lease_expires_at | TIMESTAMP | Renewed every 75s while the owner is alive. Past this the request is reclaimable. Deliberately survives a terminal write, as the record of when a worker last held the row |
| skip_cache    | BOOLEAN      | Persisted so a worker on any instance honours what the submitter asked for |
| attempts      | INT          | Claims so far. Bounds the requeue loop for a request that keeps killing its worker |

### game_features

| Column                | Type          | Notes                                  |
|-----------------------|---------------|----------------------------------------|
| id                    | UUID PK       | gen_random_uuid()                      |
| request_id            | UUID FK       | References indexing_requests(id)       |
| game_url              | VARCHAR(1024) | UNIQUE — deduplication key             |
| platform              | VARCHAR(50)   |                                        |
| white_username        | VARCHAR(255)  |                                        |
| black_username        | VARCHAR(255)  |                                        |
| white_elo             | INT           |                                        |
| black_elo             | INT           |                                        |
| time_class            | VARCHAR(50)   | bullet, blitz, rapid, classical        |
| eco                   | VARCHAR(10)   | ECO opening code                       |
| result                | VARCHAR(20)   | win, checkmated, stalemate, etc.       |
| played_at             | TIMESTAMP     |                                        |
| num_moves             | INT           |                                        |
| indexed_at            | TIMESTAMP     | When this record was indexed           |
| pgn                   | TEXT          | Full PGN for re-analysis               |

Motif data is stored separately in `motif_occurrences` (see below) and queried via EXISTS subqueries.

## Configuration

Environment variables (with defaults):

| Variable               | Default                                   |
|------------------------|-------------------------------------------|
| `PORT`                 | 8080                                      |
| `APP_NAME`             | helloworld (shared application.yml)       |
| `INDEXER_DB_URL`       | jdbc:postgresql://localhost:5432/indexer   |
| `INDEXER_DB_USERNAME`  | indexer                                   |
| `INDEXER_DB_PASSWORD`  | indexer                                   |

## Build & Test

```bash
# Build everything
bazel build //domains/games/apis/one_d4/...

# Run all tests
bazel test //domains/games/apis/one_d4/...

# Run specific test suites
bazel test //domains/games/libs/chessql:src/test/java/com/muchq/games/chessql/lexer/LexerTest
bazel test //domains/games/libs/chessql:src/test/java/com/muchq/games/chessql/parser/ParserTest
bazel test //domains/games/libs/chessql:src/test/java/com/muchq/games/chessql/compiler/SqlCompilerTest
bazel test //domains/games/apis/one_d4:src/test/java/com/muchq/games/one_d4/engine/PgnParserTest
bazel test //domains/games/apis/one_d4:src/test/java/com/muchq/games/one_d4/queue/InMemoryIndexQueueTest

# Build OCI image
bazel build //domains/games/apis/one_d4:indexer_image
```
