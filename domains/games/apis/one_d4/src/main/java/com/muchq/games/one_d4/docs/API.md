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

### First-page cache

The exact request the 1d4 web UI sends on first page load —
`{"query": "num.moves >= 0", "limit": 25, "offset": 0}` with no `player` — is served from an
in-memory snapshot that a background task refreshes every 30 seconds (measured from the previous
refresh's completion), so the first paint does not wait on a database round trip. That response may
therefore be up to ~60 seconds stale. Matching is on the trimmed query string, and a blank `player`
counts as absent; any other request — different query, page, page size, or a non-blank `player` —
always hits the database. If the snapshot is missing or older than 60 seconds (refresher dead,
database down at startup), the request loads a fresh snapshot through the cache — concurrent cold
misses share a single query — so serving the live result also re-warms the cache.

### Error Responses

| Condition          | HTTP Status | Cause                                |
|--------------------|-------------|--------------------------------------|
| Bad ChessQL syntax | 400         | `ParseException` (body includes `position`) |
| Unknown field      | 400         | `IllegalArgumentException`           |
| Unknown motif      | 400         | `IllegalArgumentException`           |
| Query exceeds the 10s read timeout | 500 | Statement cancelled server-side; previously such a request never returned |

---

## POST /v1/aggregate

Count indexed games grouped by one or more fields, filtered by a ChessQL query. This is the
endpoint behind "most popular opening" style questions — without it, callers would page out every
matching row and group client-side.

### Request

```json
{
  "query": "(white.username = \"hikaru\" OR black.username = \"hikaru\") AND time.class = \"blitz\"",
  "groupBy": ["opening_family"],
  "orderBy": "count",
  "limit": 20
}
```

| Field    | Type     | Required | Default | Max  | Description                                       |
|----------|----------|----------|---------|------|---------------------------------------------------|
| query    | string   | yes      | —       | —    | ChessQL filter (may use perspective fields when `player` is set) |
| groupBy  | string[] | yes      | —       | 5    | Fields to group by (dotted or underscore form; physical columns, plus the perspective fields when `player` is set — the rating fields as width-bucketed terms like `"opponent.elo(200)"`) |
| orderBy  | string   | no       | "count" | —    | `"count"` (most games first) or `"score"` (best score per game first, ties broken by game count) — `"score"` requires `player` |
| limit    | int      | no       | 50      | 1000 | Max groups to return                              |
| player   | string   | no       | —       | —    | Username that perspective fields in the filter and groupBy are resolved against, and whose results the per-group outcome metrics are computed for. It does **not** by itself scope the aggregate — see below |
| minGames | int      | no       | 0       | —    | Drop groups with fewer games than this. Negative values clamp to 0 |

`player` resolves perspective fields; it is not a filter. Games are restricted to that player
only through the participation guard, which is added when a perspective field is actually in play
(in the filter or in `groupBy`). An aggregate that names a `player`, uses no perspective field,
and whose filter cannot by itself exclude other players' games is therefore rejected with **400**
rather than answered: the response would count games that are not that player's while reading as
theirs, and — unlike `/v1/query`, whose rows show the usernames — nothing in an aggregate
response would reveal it. To count one player's games either use a perspective field, or filter
explicitly:

```json
{ "query": "white.username = \"hikaru\" OR black.username = \"hikaru\"", "groupBy": ["opening_family"] }
```

(Both sides, or the aggregate counts only the games they had White.)

"Cannot admit another player's games" is checked, not assumed, so mentioning a username is not
enough on its own. `AND` qualifies if any one conjunct does; an `OR` qualifies only if *every*
branch does; `NOT` never does; only `=` pins a value (`!=` and the ordering operators do not);
an `IN` list qualifies only when every alternative is the player; and the name must be the
`player` named on the request, compared case-insensitively. So `white.username != "hikaru"`,
`NOT white.username = "hikaru"`, `white.username = "hikaru" OR time.class = "blitz"`, and
`black.username IN ["hikaru", "magnus"]` are all rejected — each still admits games that are not
hikaru's. Omitting `player` entirely
is unaffected — a corpus-wide aggregate is a legitimate question.

Group-by fields validate against the same column whitelist as ChessQL comparisons. With
`player`, the perspective fields are also groupable (response keys use their underscore forms):
the categorical fields — `me.color`, `me.title`, `opponent.username`, `opponent.title`,
`outcome` — group by value, and the rating fields group as fixed-width buckets keyed by the
band's numeric lower bound, 100 points wide unless the term carries a width
(`"opponent.elo(200)"`). A NULL value (untitled opponents, NULL elos) forms a `null` group (see
CHESSQL.md).

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

### Outcome metrics

With `player` set, every group also carries how that player did in it, so "which openings do I
play" and "how do I score in them" are one request instead of a `["opening_family", "outcome"]`
grouping the caller pivots back by hand — a grouping that also spends the group limit up to three
times over, because each family fans out into a win, a loss and a draw row:

```json
{
  "groups": [
    { "group": { "opening_family": "Caro Kann Defense" },
      "count": 41, "wins": 15, "losses": 20, "draws": 6, "score": 18.0 }
  ],
  "count": 1, "totalGames": 41, "totalGroups": 1, "truncated": false
}
```

`score` is the conventional W + D/2, so it can carry a half point. The four fields are **absent**
without a `player` — not zero, which would read as "won none of these" rather than "nobody was
asked about". `wins + losses + draws` can be less than `count`: a game whose result is neither a
decision nor a draw (an unfinished `*`) is counted and scored nowhere.

`"orderBy": "score"` ranks groups by score **per game** — (W + D/2) / games — before `limit`
truncates, with game count breaking ties. That ordering is only useful with `minGames`: a
one-game opening that was won scores 100% and would otherwise top every list. Ranking by total
points would only re-spell `"count"`, since the most-played group collects the most points.
`minGames` applies to `totalGames` / `totalGroups` too, so `truncated` still describes the answer
being returned rather than a tail the caller asked not to see.

A rating-bucket group key is a JSON *number* (the band's lower bound), and a NULL group — an
untitled opponent, a NULL elo — is an explicit `null` value, so `group` values are not uniformly
strings:

```json
{ "group": { "opponent_elo": 2400 }, "count": 12 },
{ "group": { "opponent_elo": null }, "count": 3 }
```

Group keys are canonical column names (e.g. `opening_family`, even when requested as
`opening.family`; perspective group keys use their underscore forms — `me_color`, `me_title`,
`opponent_username`, `opponent_title`, `outcome`, `me_elo`, `opponent_elo`). Groups
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
| Bucket width not a positive integer (`me.elo(0)`) | 400         |
| Bucket width on a non-rating field (`me.color(100)`) | 400      |
| Conflicting bucket widths for one field           | 400         |
| Perspective field in groupBy without `player`     | 400         |
| Perspective field in the filter without `player`  | 400         |
| `player` set, but neither a perspective field nor a filter that excludes other players' games | 400 |
| Query exceeds the 10s read timeout (per statement; an aggregate that truncates runs a second totals statement) | 500 |

---

## Example Session

```bash
# Start the service (in-process mode with H2)
INDEXER_DB_URL="jdbc:h2:mem:indexer;DB_CLOSE_DELAY=-1" bazel run //domains/games/apis/one_d4:one_d4

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
