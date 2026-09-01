# stats

Serves the aggregates the log pipeline computes (#1460, part of #1365),
and runs the aggregation loop that computes them — one process, because
the box budgets a quarter CPU per container and the loop is idle between
passes.

## The loop

Every `AGGREGATE_INTERVAL` (default `15m`): list
`s3://$S3_BUCKET/logs/source=caddy/`, and for every object no successful
pass has marked processed, stream it (gunzip included), roll up its lines
in memory, and apply the rollup plus the processed marker in one
transaction. A crash between the two re-processes the object; the marker's
conflict arm makes a duplicate application a no-op — so counts survive
crashes without double-counting. Per-object failures are logged and
retried next pass.

Aggregates are bounded per row, on purpose: hosts are Caddy's vhosts,
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

What stays ad hoc: IP-range clusters (a /24 key is caller-shaped and
unbounded) and geography (nothing in the repo maps addresses to
countries). Both are one query over the raw partitions in S3, which keep
`request.remote_ip` and `request.client_ip` per line.

## The API

- `GET /stats/v1/summary?days=7` — per day/host/agent-class request and
  error counts
- `GET /stats/v1/iili/top?days=30&limit=20` — most-followed short links
- `GET /stats/v1/agents?days=30&limit=500` — per day/host/class/agent
  request and 403 counts, busiest rows first: which scrapers and bots hit
  which host, and whether they back off after being refused
- `GET /stats/v1/probes?days=30` — per host/scanner-family request counts
  and how many were served (status < 400). On vhosts whose Caddy block
  has no catch-all 404 (`api.muchq.com`, `gpt.muchq.com`), an unmatched
  path is answered with an empty 200 and so reads as served; the 1d4 and
  iili vhosts do have one, and there the column means what it says
- `GET /health`

Public through Caddy at `api.muchq.com/stats/v1/*`; the reasons for 500s
stay in the log, not on the wire.

## Configuration

`STATS_DB_URL` (postgres), `S3_BUCKET`, `S3_REGION`, `AWS_ACCESS_KEY_ID`,
`AWS_SECRET_ACCESS_KEY` — the same stats IAM user the shipper writes with,
which therefore needs `s3:GetObject` and `s3:ListBucket` on the `logs/*`
prefix as well as `s3:PutObject`. `AGGREGATE_INTERVAL` and `PORT`
(default 8092) are optional.

The store integration test needs `STATS_TEST_DB_URL` and skips without it,
like the repo's other Postgres-gated suites.
