# stats

Serves the aggregates the log pipeline computes (#1460, part of #1365),
and runs the aggregation loop that computes them — one process, because
the box budgets a quarter CPU per container and the loop is idle between
passes.

## The loop

Every `AGGREGATE_INTERVAL` (default `15m`): list the three source prefixes,
`s3://$S3_BUCKET/logs/source=caddy/` (Caddy's access logs),
`logs/source=one_d4/` (one_d4's query events, #1465) and
`logs/source=games_hub/` (the hub's domain events, #1571), and for every object
no successful pass has marked processed, stream it (gunzip included), roll
up its lines with the parser its source names, and apply the rollup plus
the processed marker in one transaction. A crash between the two
re-processes the object; the marker's conflict arm makes a duplicate
application a no-op — so counts survive crashes without double-counting.
Per-object failures are logged and retried next pass. Rows are dated by
each line's own timestamp (Caddy's `ts`, logback's `timestamp`, the hub's
own `ts`), not the
object's partition: Caddy rolls by size, so an object spans whatever days
it took to fill. That roll size (`roll_size` in the Caddyfile) is the
pipeline's latency, and `deploy_config_test` bounds it.

`service` is the column that answers "which backend the route goes to",
and `route` is how it is computed rather than a synonym for it. It is not
the same as which backend answered: Caddy answers some requests itself
above the handle that would have proxied them — a CORS preflight is a 204
from the gateway, and `refuse_bots` 403s a scraper — and those rows carry
the backend's name, with the refusals landing in its errors. Worst on
`git.muchq.com`, where every crawler refusal reads as forgejo. `route` and `source` are
narrower than they look. A route names the
matcher that claimed the path, which on a site that has matchers is the
backend that served it — but `git.muchq.com` is a bare reverse proxy with
no path matchers at all, so every Forgejo page is `other`, sharing the
token with the 404s nothing served. Grouping by route answers "which
matcher", not "which backend", and the difference is all of Forgejo.
`source` is `mcp` for anything that reached the MCP endpoint and `ui` for
a browser arriving from an origin the Caddyfile grants CORS to; `api` is
everything else, so it is the residue rather than a caller class — a
human reading `git.muchq.com` and a scanner walking it are both `api`.

Aggregates are bounded per row, on purpose: hosts fold to the Caddyfile
site the request addressed and everything else to `other`, so a port, a
case or an invented Host mints no row of its own; routes are the
Caddyfile's own path matchers, and `other` for anything no matcher
claimed; sources are one of `mcp`, `ui` and `api`, the words one_d4 uses
for the same question;
methods collapse through the nine-verb rule the metrics rails use, and
user agents collapse to four classes (`ai_scraper`, `bot`, `browser`,
`other`). Each request row also carries a bounded agent name (#1458): the
marker that classified it for AI scrapers and named bots, the UA's first
product token (max 32 bytes) for the anonymous tail, and nothing for
browsers, since every browser's token is `mozilla`. The token is the one
caller-shaped key besides iili slugs, so an object may mint at most 500
distinct ones before the rest collapse into a single `(more)` row — a
scanner rotating its User-Agent gets one row, not one per request. The
probe rollup counts requests whose path matched one of the scanner
families in `classify.go` (`wordpress`, `env`, `git`, `php`, ...) and
mints nothing for ordinary routes; there is deliberately no `admin`
family, and backup-file shapes match only at the root, because Forgejo
serves real archives and `.sql` files under deeper paths. iili slugs are
one path segment, max 64 bytes, only on the two routes that reach iili.
Row width is what's bounded; row count is what Postgres is for, which is
the division of labor #1460 drew against the tsdb.

The query rollup reads one_d4's `query_event` lines (their shape is in
one_d4's API.md) into two tables: `query_stats`, keyed by day, entry,
source, outcome, and cache — one_d4's own vocabulary, pinned against it
in `otel_contract`, with a word this build does not know collapsing to
`other` so the request still counts and the drift shows as a row; and
`query_term_stats`, which fields, motifs, order-by motifs, and group-by
columns queries used, all the compiler's names. That table keys on the
entry point as well, which the read folds away — the storage keeps the
finer grain in case a later question wants it. Latency is not here: the
tsdb holds one_d4's query histogram (#1460).

The raw lines stay in S3, so a better classifier is a re-aggregation,
not lost data — and re-aggregation is a mechanism, not a runbook. The
store records a rollup version made of `RollupVersion` plus a hash of
the schema DDL; a boot that finds a different one drops every aggregate
table and processed marker in one transaction, recreates the tables, and
the next pass recomputes everything from S3. Editing a table re-aggregates
by itself; bump the constant when a classifier changes what a row means.
The pass runs while the API serves, so for its length the counts climb
back up from zero — minutes at this scale, and the log says when it is
done.

The geo rollup (#1467) places each request's `client_ip` (`remote_ip` on
older lines) in a country and keys `geo_stats` on day, host, agent class,
and the two-letter code, with request, 403, and probe counts — where the
scrapers, bots, and scanners come from. The database is DB-IP's free
country CSV (CC BY 4.0; muchq.com's stats page carries the attribution),
pinned by URL and sha256 as `@dbip_country_lite` in
`bazel/tools.MODULE.bazel` — DB-IP's own URL first, a release asset on
this repository as the fallback — and bundled into the image at
`/geo/dbip-country-lite.csv.gz`; a new month is a pin bump (the steps are
on the pin), nothing on the host. The service loads it at boot into a sorted range table and
binary-searches it, no library. Overlapping rows lose to the range they
sit in, and the `ZZ` rows DB-IP uses for reserved and private space are
not placements. An address outside every range files under `--`, and so
does everything when the file will not load — an error in the log, never a
boot failure. `GEO_DB_PATH` points at another file, or empty switches geo
off. Rows aggregated before a database was available stay `--` until a
re-aggregation (bump `RollupVersion`).

What stays ad hoc: IP-range clusters — a /24 key is caller-shaped and
unbounded — which is one query over the raw partitions in S3, keeping
`request.remote_ip` and `request.client_ip` per line.

## The API

- `GET /stats/v1/summary?days=7` — per day/host/agent-class request and
  error counts
- `GET /stats/v1/services?days=7&limit=2000` — per day/host/service/caller/
  agent-class request and error counts, busiest rows first, with `total`
  saying how many there were before the limit: which backend, for whom,
  and whether the whom was a person or a crawler. The service is the
  container the Caddyfile proxies the route to, named at read time rather
  than stored, so re-pointing a matcher costs no re-aggregation — and
  rewrites how the past reads, since every old row is named by today's
  table. A path no matcher claimed reached no backend and is `other`,
  except on a site whose handle carries no path matcher at all, where
  every path reaches the one service behind it. A truncated read returns
  the busiest services complete and drops the rest whole, so every total
  it does report is exact — a row is one day of one caller of one class,
  and cutting the row list instead would leave a service its busy days
  and take its quiet ones
- `GET /stats/v1/iili/top?days=30&limit=20` — most-followed short links
- `GET /stats/v1/agents?days=30&limit=500` — per day/host/class/agent
  request and 403 counts, busiest rows first: which scrapers and bots hit
  which host, and whether they back off after being refused
- `GET /stats/v1/probes?days=30` — per host/scanner-family request counts
  and how many were served (status < 400). Every vhost answers an
  unmatched path with a 404 (#1468), so a served probe is a real answer —
  with two shapes that still read as served on any path: OPTIONS
  preflights on the gateway hosts, and any method on the websocket routes.
  Rows from before #1468 landed on `api.muchq.com` and `gpt.muchq.com`
  overcount served, and the summary's error count there rose with the
  change, because scanner traffic now gets the 404 it always deserved.
- `GET /stats/v1/countries?days=30&limit=2000` — per host/class/country
  request, 403, and probe counts, busiest first
- `GET /stats/v1/one_d4/queries?days=30` — one_d4 queries per
  day/entry/source/outcome/cache
- `GET /stats/v1/one_d4/terms?days=30&limit=200` — which fields, motifs,
  and group-by terms queries used, busiest first, with `total` saying how
  many there were before the limit — both from one statement, so the count
  describes the row list it arrives with rather than a set the loop may
  have added to in between. Not per entry point: `query` and `aggregate`
  are two doors onto one language, and the fold happens in the query
  rather than in a reader so that the limit cuts between whole totals. Folding after a per-entry cut would drop one half of a term
  used at both and rank the other half as though it were the whole.
- `GET /stats/v1/games_hub/events?days=30` — the hub's funnel per day:
  rooms made and closed, joins, reshapes, messages, tables dealt and games
  ended. One row per event shape, and the columns are the event's own
  fields — an event that carries no variant leaves it empty, so filter on
  `event` first. A window rather than a top-N: at this volume every shape
  of every day fits, and which shapes are absent is as much of the answer
  as which are busy.

  `players` is three quantities told apart by `event`: the seats a table
  was dealt (`game_started`), the seats it still held at the end
  (`game_finished`, near enough always 1 for an abandonment), and the
  room's size (`room_joined`, `chat_message`). They share a column and
  not a range — a table seats four, a room has no cap — so each is
  bounded by its own event. A `players` of **-1** is a count past that
  bound: not a count, and not to be summed or averaged with its
  neighbours.

  The table bound is the engine's four seats, pinned against
  `golf_hub.cc` in `otel_contract` so raising one raises the other. The
  room bound of 16 is **this reader's alone** — nothing caps a room, and
  a room hosting several tables plus its chat and its world holds more
  than a table does. It is a ceiling on how many rows one room shape may
  mint, not a fact about the hub, so a room of 17 reading -1 is the
  aggregate declining to grow rather than the hub misbehaving. Give the
  hub a real room cap and this becomes a pin like the other.

  The room is on every line in S3 and on no row here. An aggregate keyed
  by it would be one row per room per day forever; the questions it
  answers — how long a room lasted, how long a game took, how many tables
  it got through — are pairs across lines, which live in the archive and
  not in a per-day count.
- `GET /health`

Public through Caddy at `api.muchq.com/stats/v1/*`; the reasons for 500s
stay in the log, not on the wire.

## Configuration

`STATS_DB_URL` (postgres), `S3_BUCKET`, `S3_REGION`, `AWS_ACCESS_KEY_ID`,
`AWS_SECRET_ACCESS_KEY` — the same stats IAM user the shipper writes with,
which therefore needs `s3:GetObject` and `s3:ListBucket` on the `logs/*`
prefix as well as `s3:PutObject`. `AGGREGATE_INTERVAL`, `PORT`
(default 8092), and `GEO_DB_PATH` are optional.

## Tests

Unit tests per layer, and two gated suites. `stats_test` holds the store
tests, which need `STATS_TEST_DB_URL` and skip without it, like the repo's
other Postgres-gated suites. `stats_integration_test` is the end-to-end
pattern: `e2e_test.go` puts shipped objects in a memory bucket, runs the
real loop with the real vendor geo file into the real store, and reads
every endpoint through `NewRouter` over HTTP — the same objects production
wires together. A new endpoint or source gets a few lines there, not a new
harness. Locally, a throwaway `postgres:18` in Docker on port 55432 and
`STATS_TEST_DB_URL=postgresql://t:t@localhost:55432/t` run both.
