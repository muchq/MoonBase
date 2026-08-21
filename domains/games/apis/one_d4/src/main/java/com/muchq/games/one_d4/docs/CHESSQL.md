# ChessQL — Query Language Reference

## Overview

ChessQL is a domain-specific query language for searching indexed chess games. Queries are compiled to parameterized SQL at runtime — no string interpolation, no injection risk.

## Grammar (EBNF)

```
query       ::= expr EOF
expr        ::= or_expr
or_expr     ::= and_expr ("OR" and_expr)*
and_expr    ::= not_expr ("AND" not_expr)*
not_expr    ::= "NOT" not_expr | primary
primary     ::= comparison | in_expr | motif_call | "(" expr ")"
comparison  ::= field comp_op value
in_expr     ::= field "IN" "[" value_list "]"
motif_call  ::= "motif" "(" IDENTIFIER ")"
field       ::= IDENTIFIER ("." IDENTIFIER)*
comp_op     ::= "=" | "!=" | "<" | "<=" | ">" | ">="
value       ::= NUMBER | STRING
value_list  ::= value ("," value)*
```

## Operator Precedence

From lowest to highest:

1. `OR`
2. `AND`
3. `NOT` (unary prefix)
4. Parenthesized expressions

`AND` binds tighter than `OR`, so `a OR b AND c` is parsed as `a OR (b AND c)`.

## Fields

Dotted field names are mapped to database columns:

| ChessQL Field    | DB Column        | Type    |
|------------------|------------------|---------|
| `white.elo`      | `white_elo`      | INT     |
| `black.elo`      | `black_elo`      | INT     |
| `white.username` | `white_username` | VARCHAR |
| `black.username` | `black_username` | VARCHAR |
| `white.title`    | `white_title`    | VARCHAR |
| `black.title`    | `black_title`    | VARCHAR |
| `time.class`     | `time_class`     | VARCHAR |
| `num.moves`      | `num_moves`      | INT     |
| `game.url`       | `game_url`       | VARCHAR |
| `played.at`      | `played_at`      | TIMESTAMP |
| `eco`            | `eco`            | VARCHAR |
| `opening.name`   | `opening_name`   | VARCHAR |
| `opening.family` | `opening_family` | VARCHAR |
| `result`         | `result`         | VARCHAR |
| `platform`       | `platform`       | VARCHAR |
| `date`           | *(virtual — `played_at` range)* | ISO date string, filter-only |
| `month`          | *(virtual — `played_at` range)* | `"YYYY-MM"` string, filter-only |

Underscore-separated names also work directly: `white_elo >= 2500` is equivalent to `white.elo >= 2500`.

`date` and `month` have no column of their own: they compile to `played_at` ranges and are
filter-only, rejected in `IN` lists and in `groupBy`. See [Date scoping](#date-scoping) below.

`white.title` / `black.title` hold chess.com titles (`GM`, `IM`, `WGM`, ...) as of index time;
untitled players are NULL. A title awarded after a game was indexed does not appear on that row
until the month is indexed again.

`opening.name` is the human-readable opening line derived from the chess.com `ECOUrl` (e.g. `Caro Kann Defense Two Knights Attack 3...dxe4`);
`opening.family` is its leading family segment (e.g. `Caro Kann Defense`) — the level most
questions are asked at, e.g. `white.username = "hikaru" AND opening.family = "Caro Kann Defense"`.
The family drops the move continuation first, so `Owens Defense...3.Nc3 e6` files under
`Owens Defense`; the continuation stays in `opening.name`, which is what makes it the fine-grained
field of the two.
Rows indexed before these columns existed hold NULL until reindexed with `skipCache: true` on
`POST /v1/index` (or `skip_cache` on the `index_chess_games` MCP tool) — a plain re-request is
served from the indexed-period cache and does not refetch. One thing to check when a backfill
appears to do nothing: `skipCache` will not start a second run over a range that is already being
indexed. If a request for that player and month range is still PENDING or PROCESSING, the submit
returns *that* request and refetches nothing, so the NULLs stay. Poll it to COMPLETED, then submit
the backfill again. The same backfill is what corrects stored values, not just NULLs: rows indexed
before the move-continuation strip (#1344) hold the family derived at the time, so a month indexed
earlier keeps splitting one family across two group keys until it is reindexed.

> **Caveat — `opening.family` is not a normalized taxonomy.** Both opening fields are string
> slices of chess.com's ECO-URL, so a qualified name forms its own value and its own aggregation
> group: `Closed Sicilian Defense` and `Alapin Sicilian Defense` do not roll up into
> `Sicilian Defense`. When aggregating by `opening_family`, check the response's `truncated` flag:
> it is true when the group limit cut off a long tail of small variant groups, and `totalGroups`
> says how many there really were.

### Date scoping

Two virtual fields scope queries by when the game was played. Neither maps to its own column —
both compile to `played_at` range predicates over UTC day/month boundaries:

| Field   | Value format          | Operators | Meaning                                    |
|---------|-----------------------|-----------|--------------------------------------------|
| `date`  | ISO date `"YYYY-MM-DD"` | all     | Day-granularity comparison on `played_at`  |
| `month` | `"YYYY-MM"`           | `=` only  | Sugar for the month's `played_at` range    |

Because a day covers a range of timestamps, operators are rewritten against day boundaries:
`date = "2026-07-01"` means "played on that day", `date <= "2026-07-01"` includes the whole day,
and `date >= "2026-07-01" AND date < "2026-08-01"` is equivalent to `month = "2026-07"`. Values
are validated at compile time; malformed strings (or numbers) are rejected. `date` and `month`
are filter-only: they are not allowed in `IN` lists or in `groupBy` on `/v1/aggregate`.

`played.at` itself stays available for sub-day precision — it compares the raw timestamp and
requires a full ISO value (`played.at >= "2026-07-01T13:30:00"`, the stored UTC wall clock; a
trailing `Z` or offset is converted to UTC). A bare date there is rejected rather than silently
compared as midnight, because `date` already means "that day" and a midnight comparison
masquerading as one is the kind of zero-row answer nothing would ever flag.

> **A date filter scopes the indexed corpus, not your whole game history.** Nothing reports which
> periods have been indexed, and a `date` / `month` filter over a period that was never indexed
> returns zero rows — not an error. That result is indistinguishable from "played no games then",
> so do not read it as one: index the period first (`POST /v1/index`, or `index_chess_games` on
> the MCP server) and re-run the query.

```
month = "2026-07" AND outcome = "loss"
date >= "2026-07-01" AND motif(fork)
```

### Perspective fields

Every physical field is absolute (`white.*` / `black.*`), so "hikaru as White" is expressible but
"hikaru's results regardless of color" would require writing and unioning two queries. Perspective
fields close that gap: they are resolved at compile time against a `player` supplied on the
request (`player` on `POST /v1/query` / `POST /v1/aggregate`, or the `player` argument on the
`query_chess_games` / `aggregate_chess_games` MCP tools).

| Field               | Meaning                                                    |
|---------------------|------------------------------------------------------------|
| `me.color`          | `white` or `black` — the side the player played            |
| `me.elo`            | The player's rating in that game                           |
| `me.title`          | The player's title in that game                            |
| `opponent.username` | The other side's username                                  |
| `opponent.elo`      | The other side's rating                                    |
| `opponent.title`    | The other side's title                                     |
| `outcome`           | `win` / `loss` / `draw` / `unknown`, relative to the player |

A query that uses any perspective field requires the `player` parameter and is implicitly
restricted to games the player participated in. Example — "hikaru's blitz wins against GMs,
regardless of color":

```
outcome = "win" AND opponent.title = "GM" AND time.class = "blitz"
```

with `player: "hikaru"`. Perspective fields compile to `CASE` expressions over the `white_*` /
`black_*` columns plus a participation guard.

The guard exists to repair those `CASE` expressions — they read "not white" as "black" — so it is
added only when a perspective field is used. `player` on its own is **not** a filter: a query
that names a player but uses no perspective field is compiled without any player predicate, and
matches every indexed game. On `/v1/query` that is visible in the first row's usernames, and it
stays permitted; on `/v1/aggregate` no column would reveal it, so that combination is rejected
(400) unless the filter itself restricts the games to that player — see API.md.

The categorical perspective fields may also be used in `groupBy` on `/v1/aggregate` (and the
`aggregate_chess_games` tool) when `player` is supplied: `me.color`, `me.title`,
`opponent.username`, `opponent.title`, and `outcome`. This is what makes both-colors
breakdowns expressible at all: grouping by `["me.color", "opening_family"]` answers "which
openings do I face as Black" where `opening_family` alone conflates both sides' choices, and
grouping by `opponent.title` or `opponent.username` is the only correct opponent breakdown
across both colors — the color-specific columns (`white_title`, ...) hold the *player's own*
value on half the rows, so grouping them silently mixes the player into the opponent buckets.
Group keys in the response use the underscore form (`me_color`, `opponent_title`, ...), and
either spelling is accepted in the request. Untitled opponents form a `null` group, the same as
grouping the physical nullable title columns. `opponent.username` groups by the stored username
as-is — the perspective *filter* matches case-insensitively, but group keys are not
case-normalized, so the same opponent stored under two casings forms two groups (same trap as
`opening_family` variants). Without `player`, grouping by any perspective field is rejected.

`player` also decides whether the aggregate carries outcome metrics. With one supplied, each
group comes back with `wins` / `losses` / `draws` / `score` (W + D/2) from that player's side,
which is what makes `groupBy: ["opening_family"]` answer "how do I score in each opening" without
a second `outcome` dimension to pivot back — and without spending the group limit three times per
family. Without a player the four fields are absent rather than zero: there is no side to
attribute a result to. A result that is neither a decision nor a draw (an unfinished `*`) is
counted in `count` and in none of the three, so they need not add up. `orderBy: "score"` ranks by
score per game before the limit truncates and requires `player`; pair it with `minGames` so a
one-game sideline does not top the list. See API.md for the request fields.

The two rating fields (`me.elo`, `opponent.elo`) group as fixed-width buckets, never raw by
default — grouping by a raw rating makes one bucket per distinct value, burying the answer under
one-game groups. A `groupBy` term is either a field name or a rating field with a
bucket width:

```
group_term ::= field | rating_field "(" NUMBER ")"
```

Bare `opponent.elo` buckets by 100 points; a parenthesized width overrides it:
`groupBy: ["opponent.elo(200)"]`. Any width down to 1 is honored (an explicit `opponent.elo(1)`
*is* raw grouping, deliberately — `totalGroups` / `truncated` will say what it cost). Bands are
half-open and keyed by their numeric lower bound — a group key of `2400` at width 200 means
ratings in [2400, 2600) — under the underscore response key (`opponent_elo`), so bands sort
numerically in the tiebreak. Rows with a NULL elo (chess.com omitted that side's rating data)
form a `null` bucket, like the title fields. One width per field per request:
`["me.elo(100)", "me.elo(200)"]` is rejected rather than silently picking one.

Like the physical `*.title` / `*.elo` columns they resolve to, `me.title`, `opponent.title`,
`me.elo`, and `opponent.elo` follow SQL NULL semantics: NULLs (untitled players, rows indexed
before the title columns existed, sides whose rating chess.com omitted) never match a comparison
— so `opponent.title != "GM"` returns only games against *titled* non-GM opponents, not games
against untitled ones (#1302 tracks making `!=` NULL-inclusive; until then, grouping by
`opponent.title` is how to count the `null` bucket `!=` drops).

## Motifs

The `motif()` function checks for tactical pattern presence. Queries compile to `EXISTS` subqueries
against the `motif_occurrences` table. 11 motifs are stored directly as rows in that table;
5 are derived from `ATTACK` rows using flag or grouping conditions at query time.

**Directly stored motifs** (one row per occurrence, 11 total):

| ChessQL                       | motif_occurrences filter        |
|-------------------------------|---------------------------------|
| `motif(pin)`                  | `motif = 'PIN'`                 |
| `motif(cross_pin)`            | `motif = 'CROSS_PIN'`           |
| `motif(skewer)`               | `motif = 'SKEWER'`              |
| `motif(check)`                | `motif = 'CHECK'`               |
| `motif(promotion)`            | `motif = 'PROMOTION'`           |
| `motif(promotion_with_check)` | `motif = 'PROMOTION_WITH_CHECK'`|
| `motif(promotion_with_checkmate)` | `motif = 'PROMOTION_WITH_CHECKMATE'` |
| `motif(back_rank_mate)`       | `motif = 'BACK_RANK_MATE'`      |
| `motif(smothered_mate)`       | `motif = 'SMOTHERED_MATE'`      |
| `motif(zugzwang)`             | `motif = 'ZUGZWANG'`            |
| `motif(overloaded_piece)`     | `motif = 'OVERLOADED_PIECE'`    |

**Derived motifs** (computed from `ATTACK` rows at query time, 5 total):

| ChessQL                 | Derivation condition |
|-------------------------|----------------------|
| `motif(discovered_attack)` | `motif = 'ATTACK' AND is_discovered = TRUE` |
| `motif(checkmate)`      | `motif = 'ATTACK' AND is_mate = TRUE` |
| `motif(discovered_check)` | `motif = 'ATTACK' AND is_discovered = TRUE AND target LIKE 'K%' OR 'k%'` |
| `motif(fork)`           | `motif = 'ATTACK' AND is_discovered = FALSE AND attacker IS NOT NULL`, grouped by `(ply, attacker)` with `HAVING COUNT(*) >= 2` |
| `motif(double_check)`   | `motif = 'ATTACK' AND target IS king`, grouped by `ply` with `HAVING COUNT(*) >= 2` |

## Values

- **Numbers**: integer literals, optionally negative. Examples: `2500`, `-1`, `0`
- **Strings**: double-quoted. Backslash escapes supported. Examples: `"chess.com"`, `"B90"`, `"hikaru"`
- **Timestamps**: `played.at` takes a full ISO timestamp string read as the stored UTC wall
  clock: `played.at >= "2026-07-01T13:30:00"`. A trailing `Z` or explicit offset is accepted and
  converted to UTC. A bare date on `played.at` is rejected at compile time — day and month
  filtering is what `date` / `month` are for (see [Date scoping](#date-scoping)).
- **No NULL literal.** `played.at = NULL` — or any `= NULL` / `!= NULL`, or SQL's `IS NULL` — is
  a syntax error, deliberately: a game whose field is unset never matches *any* comparison (the
  NULL semantics described for the title/elo fields above), so there is nothing a NULL value
  could usefully mean in a filter. To count unset rows, group by the field on `/v1/aggregate`
  and read the `null` group.

## Examples

### Simple comparisons

```
white.elo >= 2500
eco = "B90"
num.moves > 40
```

### Motif queries

```
motif(fork)
motif(cross_pin)
NOT motif(pin)
```

### Boolean combinations

```
white.elo >= 2500 AND motif(cross_pin)
motif(fork) OR motif(skewer)
motif(fork) AND NOT motif(pin)
```

### IN expressions

```
platform IN ["lichess", "chess.com"]
eco IN ["B90", "B91", "B92"]
```

### Complex queries

```
white.elo >= 2500 AND motif(fork) AND NOT motif(pin)
(motif(fork) OR motif(skewer)) AND white.elo > 2000
platform IN ["chess.com"] AND black.elo > 2700 AND motif(discovered_attack)
```

## Compilation Examples

| ChessQL Input | SQL Output (WHERE clause fragment) | Parameters |
|---------------|-----------------------------------|------------|
| `white.elo >= 2500` | `white_elo >= ?` | `[2500]` |
| `motif(pin)` | `EXISTS (SELECT 1 FROM motif_occurrences mo WHERE mo.game_url = g.game_url AND mo.motif = 'PIN')` | `[]` |
| `motif(checkmate)` | `EXISTS (SELECT 1 FROM motif_occurrences mo WHERE mo.game_url = g.game_url AND mo.motif = 'ATTACK' AND mo.is_mate = TRUE)` | `[]` |
| `white.elo >= 2500 AND motif(pin)` | `(white_elo >= ? AND EXISTS (...motif = 'PIN'...))` | `[2500]` |
| `NOT motif(pin)` | `(NOT EXISTS (...motif = 'PIN'...))` | `[]` |
| `platform IN ["lichess", "chess.com"]` | `LOWER(platform) IN (LOWER(?), LOWER(?))` | `["lichess", "chess.com"]` |
| `date >= "2026-07-01"` | `played_at >= ?` | `[2026-07-01T00:00:00Z]` |
| `month = "2026-07"` | `(played_at >= ? AND played_at < ?)` | `[2026-07-01T00:00:00Z, 2026-08-01T00:00:00Z]` |

## Error Handling

Two exception kinds, and the split is visible on the API's 400 envelope: a `ParseException`
(syntax) carries a structured `position` field alongside `error`; an `IllegalArgumentException`
(everything else below) carries only `error`, with any position mentioned in the prose.

- **Syntax error**: `ParseException` — says what the grammar wanted in the language's own terms
  (`Expected a number or a double-quoted string, got 'B90'`), plus the position; common
  near-misses get a specific hint (`NULL` / `IS NULL` → no NULL literal, an unquoted string →
  quote it, two conditions with no connector → AND/OR, an out-of-range number)
- **Unknown field**: `IllegalArgumentException` — "Unknown field: X. Known fields: ..." (the
  sorted roster, filterable and perspective)
- **Unknown motif**: `IllegalArgumentException` — "Unknown motif: X. Known motifs: ..."
- **Wrong value type**: `IllegalArgumentException` — a quoted number against a numeric field
  (`white.elo takes a number, got: "2500" — drop the quotes`) or a bare number against a string
  field; enforced at compile time so it cannot become an engine-dependent runtime error
- **Single-quoted or unterminated string**: `IllegalArgumentException` — the fix is in the
  message (`Strings use double quotes...`), position in prose only
- **Bad `played.at` / `date` / `month` value**: `IllegalArgumentException` — names the field,
  the accepted format, and the rejected value; ISO-valid years outside 0–9999 are rejected too

## Security

All values are bound as JDBC parameters (`?`), never interpolated into SQL strings. Field names and motif names are validated against a whitelist before being included in SQL. The compiler rejects any unrecognized identifiers.
