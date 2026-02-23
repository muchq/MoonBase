# Chess Game Indexer — API Reference

## Base URL

```
http://localhost:8080
```

Configurable via `PORT` environment variable.

---

## POST /v1/index

Start indexing games for a player over a month range.

### Request

```json
{
  "player": "hikaru",
  "platform": "CHESS_COM",
  "startMonth": "2024-03",
  "endMonth": "2024-03"
}
```

| Field       | Type   | Required | Description                            |
|-------------|--------|----------|----------------------------------------|
| player      | string | yes      | Username on the chess platform          |
| platform    | string | yes      | `"CHESS_COM"` (lichess planned)        |
| startMonth  | string | yes      | Start month inclusive, format `YYYY-MM` |
| endMonth    | string | yes      | End month inclusive, format `YYYY-MM`   |

### Response (201)

```json
{
  "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "status": "PENDING",
  "gamesIndexed": 0,
  "errorMessage": null
}
```

### Status Lifecycle

```
PENDING → PROCESSING → COMPLETED
                     → FAILED (with errorMessage)
```

---

## GET /v1/index/{id}

Poll the status of an indexing request.

### Response (200)

```json
{
  "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "status": "COMPLETED",
  "gamesIndexed": 147,
  "errorMessage": null
}
```

### Response (404 — via RuntimeException, needs error mapping)

Returned when the ID does not match any indexing request.

---

## POST /v1/query

Search indexed games using ChessQL.

### Request

```json
{
  "query": "white.elo >= 2500 AND motif(fork)",
  "limit": 10,
  "offset": 0
}
```

| Field  | Type   | Required | Default | Max  | Description                     |
|--------|--------|----------|---------|------|---------------------------------|
| query  | string | yes      | —       | —    | ChessQL query string            |
| limit  | int    | no       | 50      | 1000 | Max results to return           |
| offset | int    | no       | 0       | —    | Pagination offset               |

### Response (200)

```json
{
  "games": [
    {
      "gameUrl": "https://www.chess.com/game/live/12345",
      "platform": "CHESS_COM",
      "whiteUsername": "Hikaru",
      "blackUsername": "MagnusCarlsen",
      "whiteElo": 2850,
      "blackElo": 2830,
      "timeClass": "blitz",
      "eco": "B90",
      "result": "1-0",
      "playedAt": 1710524400.0,
      "numMoves": 42,
      "hasPin": true,
      "hasCrossPin": false,
      "hasFork": true,
      "hasSkewer": false,
      "hasDiscoveredAttack": false
    }
  ],
  "count": 1
}
```

**Result values:**
- `1-0` — White wins
- `0-1` — Black wins
- `1/2-1/2` — Draw (stalemate, repetition, agreement, etc.)
- `unknown` — Result could not be determined
```

### Error Responses

| Condition          | HTTP Status | Cause                                |
|--------------------|-------------|--------------------------------------|
| Bad ChessQL syntax | 500         | `ParseException` (needs error mapping) |
| Unknown field      | 500         | `IllegalArgumentException`           |
| Unknown motif      | 500         | `IllegalArgumentException`           |

> **Note**: Error mapping to proper 400 responses is a planned improvement. See ROADMAP.md Phase 2.

---

## Example Session

```bash
# Start the service (in-process mode with H2)
INDEXER_DB_URL="jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1" bazel run //domains/games/apis/one_d4:indexer

# 1. Start indexing
curl -X POST http://localhost:8080/v1/index \
  -H "Content-Type: application/json" \
  -d '{"player":"hikaru","platform":"CHESS_COM","startMonth":"2026-01","endMonth":"2026-01"}'

# Response: {"id":"abc-123","status":"PENDING","gamesIndexed":0}

# 2. Poll until completed
curl http://localhost:8080/v1/index/abc-123

# Response: {"id":"abc-123","status":"COMPLETED","gamesIndexed":828}

# 3. Query indexed games
curl -X POST http://localhost:8080/v1/query \
  -H "Content-Type: application/json" \
  -d '{"query":"white.elo > 2700 AND motif(fork)","limit":10,"offset":0}'

# 4. Query games with multiple motifs
curl -X POST http://localhost:8080/v1/query \
  -H "Content-Type: application/json" \
  -d '{"query":"motif(pin) AND motif(skewer)","limit":10,"offset":0}'

# 5. Query by ECO opening code
curl -X POST http://localhost:8080/v1/query \
  -H "Content-Type: application/json" \
  -d '{"query":"eco = \"B90\"","limit":10,"offset":0}'
```
