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

Aggregates are bounded on purpose: hosts are Caddy's vhosts, methods
collapse through the nine-verb rule the metrics rails use, user agents
collapse to four classes (`ai_scraper`, `bot`, `browser`, `other`), and
iili slugs are the one caller-shaped key — one path segment, max 64
bytes, only on the redirect routes. The raw lines stay in S3, so a
better classifier is a re-aggregation, not lost data.

## The API

- `GET /stats/v1/summary?days=7` — per day/host/agent-class request and
  error counts
- `GET /stats/v1/iili/top?days=30&limit=20` — most-followed short links
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
