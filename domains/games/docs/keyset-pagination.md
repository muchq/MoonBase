# Keyset Pagination for Game Queries

## Current approach

Game queries use `LIMIT ? OFFSET ?` with `ORDER BY played_at DESC, game_url ASC`.
This is correct and fast enough for typical usage: most queries are filtered by
ChessQL predicates (motif, ECO, ELO range, etc.) that shrink the result set well
below 1000 rows, and users rarely page past the first few pages of results.

## When to migrate

Switch to keyset pagination if you observe any of the following:

- Slow page loads on unfiltered username queries for accounts with 10k+ games,
  particularly on pages beyond ~100 (OFFSET 2500+)
- Complaints from heavy users about pagination getting slower as they go deeper
- P95 query latency on `/v1/query` climbing above ~200ms

## The migration

Replace `LIMIT ? OFFSET ?` with a keyset `WHERE` clause using the last row of
the previous page as a cursor.

### API change

Add an optional `cursor` field to `QueryRequest` and `QueryResponse`:

```json
// Request
{ "query": "...", "limit": 25, "cursor": "2024-06-01T00:00:00Z|https://chess.com/game/abc" }

// Response
{ "games": [...], "cursor": "2024-01-15T12:00:00Z|https://chess.com/game/xyz" }
```

The cursor encodes `played_at` + `game_url` of the last row returned. Absence of
`cursor` in the response means there are no more pages.

### SQL change

In `GameFeatureDao.query()`, when a cursor is present, append a keyset predicate
instead of `OFFSET`:

```sql
-- cursor = (cursorPlayedAt, cursorGameUrl)
{compiledSql}
AND (played_at < ? OR (played_at = ? AND game_url > ?))
ORDER BY played_at DESC, game_url ASC
LIMIT ?
```

Without a cursor, omit the `AND (...)` clause (first page).

### Index

Add a composite index to make keyset seeks O(log N):

```sql
CREATE INDEX idx_game_features_played_at_game_url
ON game_features (played_at DESC, game_url ASC);
```

This index also speeds up the current LIMIT/OFFSET approach and is safe to add
at any time without waiting for the full keyset migration.

### Frontend change

Replace `page` state with `cursorStack: string[]` state:

- Forward: push the cursor from the current response onto the stack, fetch with
  the new cursor
- Back: pop the stack, fetch with the previous cursor (or no cursor for page 1)
- The `prefetchQuery` pattern is preserved: prefetch with `cursor = last cursor`
  when `hasMore` is true

### Tradeoffs vs. current approach

| | LIMIT/OFFSET | Keyset |
|---|---|---|
| Performance at depth | O(offset) index scan | O(log N) always |
| Stable under writes | No (gaps/dupes possible) | Yes |
| Arbitrary page jumps | Yes | No |
| Implementation complexity | Low | Moderate |
| API contract change | No | Yes (cursor field) |

## Recommended short-term action

Add the composite index now — it's a one-line migration, improves the current
LIMIT/OFFSET approach, and is required for keyset pagination anyway:

```sql
CREATE INDEX CONCURRENTLY idx_game_features_played_at_game_url
ON game_features (played_at DESC, game_url ASC);
```

`CONCURRENTLY` avoids a table lock and is safe to run on a live database.
