# Chess Game Indexer — System Design

## Overview

A Micronaut service that indexes chess games from chess.com (lichess planned), extracts tactical motifs via position replay, and exposes a custom query language (ChessQL) for searching indexed games.

**Stack**: Java 21, Micronaut 4.x, Bazel, PostgreSQL, chariot (chess library), HikariCP

## Architecture

```
                         ┌──────────────┐
                         │   Client     │
                         └──────┬───────┘
                                │
                    ┌───────────▼────────────┐
                    │    Micronaut HTTP       │
                    │  (Netty + JAX-RS)      │
                    ├────────────┬───────────┤
                    │ IndexCtrl  │ QueryCtrl │
                    └─────┬──────┴─────┬─────┘
                          │            │
                 ┌────────▼──┐   ┌─────▼──────────┐
                 │IndexQueue │   │  ChessQL        │
                 │(in-memory)│   │  Lexer→Parser→  │
                 └────┬──────┘   │  Compiler→SQL   │
                      │          └─────┬───────────┘
                ┌─────▼──────┐         │
                │IndexWorker │         │
                │  (daemon)  │         │
                └─────┬──────┘         │
                      │                │
           ┌──────────▼─────┐          │
           │ FeatureExtract │          │
           │  PgnParser     │          │
           │  GameReplayer  │          │
           │  MotifDetect[] │          │
           └──────┬─────────┘          │
                  │                    │
            ┌─────▼────────────────────▼──┐
            │         PostgreSQL          │
            │  indexing_requests          │
            │  game_features             │
            └─────────────────────────────┘
```

## Package Structure

```
com.muchq.indexer/
  App.java                          Micronaut entry point
  IndexerModule.java                @Factory — wires all beans

  api/
    IndexController.java            POST /index, GET /index/{id}
    QueryController.java            POST /query

  api/dto/
    IndexRequest.java               Inbound: player, platform, month range
    IndexResponse.java              Outbound: id, status, gamesIndexed, error
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
    IndexingRequestDao.java         CRUD for indexing_requests
    GameFeatureDao.java             Insert + parameterized query for game_features

  engine/
    PgnParser.java                  Extracts headers + movetext from PGN strings
    GameReplayer.java               Uses chariot to replay moves → List<PositionContext>
    FeatureExtractor.java           Orchestrates replay + all motif detectors

  engine/model/
    ParsedGame.java                 Headers map + movetext string
    GameFeatures.java               Set<Motif>, numMoves, occurrence details
    Motif.java                      Enum: PIN, CROSS_PIN, FORK, SKEWER, DISCOVERED_ATTACK
    PositionContext.java            moveNumber, FEN, whiteToMove

  motifs/
    MotifDetector.java              Interface: motif(), detect(positions)
    PinDetector.java                Ray-casting from king to find pinned pieces
    CrossPinDetector.java           Piece pinned along two axes simultaneously
    ForkDetector.java               Piece attacking 2+ valuable enemy pieces
    SkewerDetector.java             Sliding attack through a more valuable piece
    DiscoveredAttackDetector.java   Piece moves to reveal sliding attacker behind it

  chessql/
    lexer/   TokenType, Token, Lexer
    ast/     Expr (sealed), OrExpr, AndExpr, NotExpr, ComparisonExpr, InExpr, MotifExpr
    parser/  Parser (recursive descent), ParseException
    compiler/ SqlCompiler, CompiledQuery
```

## Data Flow

### Indexing

1. Client sends `POST /index` with player, platform, month range
2. `IndexController` creates a row in `indexing_requests` (status=PENDING), enqueues an `IndexMessage`
3. `IndexWorkerLifecycle` daemon thread polls the queue
4. `IndexWorker.process()`:
   - Sets status to PROCESSING
   - Iterates months, fetches games from chess.com API via `ChessClient`
   - For each game: `FeatureExtractor.extract(pgn)` → `PgnParser` → `GameReplayer` → `MotifDetector[]`
   - Inserts `GameFeatureRow` into `game_features` (ON CONFLICT DO NOTHING for idempotency)
   - Updates `games_indexed` count periodically
   - Sets status to COMPLETED or FAILED

### Querying

1. Client sends `POST /query` with a ChessQL string, limit, offset
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
| has_pin               | BOOLEAN       | Indexed for fast query                 |
| has_cross_pin         | BOOLEAN       |                                        |
| has_fork              | BOOLEAN       |                                        |
| has_skewer            | BOOLEAN       |                                        |
| has_discovered_attack | BOOLEAN       |                                        |
| motifs_json           | JSONB         | Detailed occurrence data               |
| pgn                   | TEXT          | Full PGN for re-analysis               |

Boolean columns enable fast indexed queries. JSONB stores detailed motif occurrence data (move numbers, descriptions) for drill-down.

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
