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

| Field         | Type   | Required | Description                            |
|---------------|--------|----------|----------------------------------------|
| player        | string | yes      | Username on the chess platform (normalized to lowercase) |
| platform      | string | yes      | `"chess.com"` or `"CHESS_COM"`, case-insensitive; echoed back as `"CHESS_COM"` (lichess planned) |
| startMonth    | string | yes      | Start month inclusive, format `YYYY-MM` |
| endMonth      | string | yes      | End month inclusive, format `YYYY-MM`   |
| excludeBullet | bool   | no       | Skip bullet games (default false)       |
| skipCache     | bool   | no       | Refetch every month in the range even if already indexed, refreshing stored rows — e.g. to backfill titles and opening names on rows indexed before those columns existed. Does **not** start a rival run of a range already in flight: if a request for the same player/months is PENDING or PROCESSING, that request is returned and nothing is refetched (default false) |

### Response (200)

```json
{
  "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "player": "hikaru",
  "platform": "CHESS_COM",
  "startMonth": "2024-03",
  "endMonth": "2024-03",
  "status": "PENDING",
  "gamesIndexed": 0,
  "excludeBullet": false
}
```

Null fields are omitted rather than serialized as `null`, so a fresh request carries no
`errorMessage` and no `data` key at all. Read them as absent, not null.

### Status Lifecycle

```
PENDING → PROCESSING → COMPLETED
                     → FAILED (with errorMessage)
```

---

## GET /v1/index

List the 50 most recent indexing requests, newest first. Each entry has the same shape as
`GET /v1/index/{id}`.

---

## GET /v1/index/{id}

Poll the status of an indexing request.

### Response (200)

```json
{
  "id": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
  "player": "hikaru",
  "platform": "CHESS_COM",
  "startMonth": "2024-03",
  "endMonth": "2024-03",
  "status": "COMPLETED",
  "gamesIndexed": 147,
  "excludeBullet": false,
  "data": {
    "status": "AVAILABLE",
    "monthsAvailable": 1,
    "monthsTotal": 1,
    "expiresAt": 1785542400.000000000
  }
}
```

`errorMessage` is absent here because it is null; a FAILED request carries it.

### The `data` object — is the indexed data still there?

A request row outlives the games it produced. The retention worker deletes games and indexed
periods once they are older than **7 days**, and the request row itself after **30 days** — so
without `data`, a request from two weeks ago is indistinguishable from one indexed an hour ago.
Both say `"status": "COMPLETED", "gamesIndexed": 147`; only one of them still has games to query.

The 23-day gap between the two windows is deliberate. It is the period in which a request can
still answer "what happened to my index?" with `EXPIRED` — pruned, re-run it — instead of the
request having vanished too. After 30 days the row is deleted and the id stops resolving.

The key is **absent** until the request reaches COMPLETED — never `"data": null`.

| Field           | Type        | Description                                                     |
|-----------------|-------------|-----------------------------------------------------------------|
| status          | string      | `AVAILABLE`, `PARTIAL`, `EXPIRED`, or `UNKNOWN` (see below)      |
| monthsAvailable | int         | Months in the request's range that are still indexed             |
| monthsTotal     | int         | Months the request covers                                        |
| expiresAt       | float    | When the first remaining month is due to be swept. **Absent** once none remain — like every null field here, it is omitted rather than sent as null. Epoch seconds with nanos, e.g. `1785542400.000000000` |

- `AVAILABLE` — every month in the range is still indexed.
- `PARTIAL` — some months have been swept. Within a single request every month is fetched minutes
  apart and ages out together, so PARTIAL usually means the range was re-covered piecemeal by
  later requests rather than that it decayed unevenly.
- `EXPIRED` — nothing is left. Re-run the request to index the games again.
- `UNKNOWN` — the stored month range could not be parsed, so coverage could not be checked.

Availability is computed against `indexed_periods`, keyed by (player, platform, month,
excludeBullet) — the same rows the indexer's cache consults, swept on the same clock as the
games. Counting surviving `game_features` by `request_id` would report differently: reindexing a
period reassigns those rows to the newer request, so an older request would read `EXPIRED` while
its games are in fact still stored.

`expiresAt` tracks the **earliest**-fetched surviving month, which is when the request stops
being whole rather than when its last month disappears.

### Response (404)

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
| player | string | no       | —       | —    | Username that perspective fields (`me.*`, `opponent.*`, `outcome`) are resolved against; required when the query uses them (see CHESSQL.md) |

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
      "numMoves": 42
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

---

## POST /v1/aggregate

Count indexed games grouped by one or more fields, filtered by a ChessQL query. This is the
endpoint behind "most popular opening" style questions — without it, callers would page out every
matching row and group client-side.

### Request

```json
{
  "query": "white.username = \"hikaru\" AND time.class = \"blitz\"",
  "groupBy": ["opening_family"],
  "orderBy": "count",
  "limit": 20
}
```

| Field   | Type     | Required | Default | Max  | Description                                       |
|---------|----------|----------|---------|------|---------------------------------------------------|
| query   | string   | yes      | —       | —    | ChessQL filter (may use perspective fields when `player` is set) |
| groupBy | string[] | yes      | —       | 5    | Fields to group by (dotted or underscore form; physical columns, plus `me.color` / `outcome` when `player` is set) |
| orderBy | string   | no       | "count" | —    | Only "count" is supported (descending)            |
| limit   | int      | no       | 50      | 1000 | Max groups to return                              |
| player  | string   | no       | —       | —    | Username that perspective fields in the filter and groupBy are resolved against |

Group-by fields validate against the same column whitelist as ChessQL comparisons; `me.color`
and `outcome` are the only perspective fields allowed, and only with `player` (see CHESSQL.md).

### Response (200)

```json
{
  "groups": [
    { "group": { "opening_family": "Caro Kann Defense" }, "count": 42 },
    { "group": { "opening_family": "Sicilian Defense" }, "count": 17 }
  ],
  "count": 2,
  "totalGames": 59,
  "totalGroups": 2,
  "truncated": false
}
```

Group keys are canonical column names (e.g. `opening_family`, even when requested as
`opening.family`; perspective group keys use the underscore form `me_color` / `outcome`). Groups
are ordered by count descending, then by group values ascending. `count` is the number of groups
returned — not a game count, which is what `totalGames` reports. `totalGames` and `totalGroups`
are computed over the untruncated result, and `truncated` is true when `limit` cut off groups —
important for `opening_family`, whose ECO-URL-derived values fragment into long tails of small
groups. When fewer groups come back than `limit` allowed, nothing was cut off and the totals are
derived from the returned rows rather than from a second query.

A `date` / `month` filter scopes the indexed corpus only. A period that was never indexed returns
zero groups rather than an error, which is indistinguishable from "played no games then" — index
it first via `POST /v1/index`.

### Error Responses

| Condition                                        | HTTP Status |
|--------------------------------------------------|-------------|
| Bad ChessQL syntax                                | 400         |
| Unknown group-by field                            | 400         |
| Missing query/groupBy                             | 400         |
| Filter-only field in groupBy (`date`, `month`)    | 400         |
| Perspective field in groupBy other than `me.color` / `outcome` | 400 |
| `me.color` / `outcome` in groupBy without `player` | 400        |
| Perspective field in the filter without `player`  | 400         |

---

### Error Responses

| Condition          | HTTP Status | Cause                                |
|--------------------|-------------|--------------------------------------|
| Bad ChessQL syntax | 400         | `ParseException` (body includes `position`) |
| Unknown field      | 400         | `IllegalArgumentException`           |
| Unknown motif      | 400         | `IllegalArgumentException`           |
| Unknown request ID | 404         | `NoSuchElementException`             |

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
