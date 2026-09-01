# log_shipper

Moves Caddy's rolled access logs to S3 on an interval (#1457) — the first
stage of the stats pipeline (#1365). No streaming infra on purpose: at this
volume a cron-shaped upload is the whole job, and Kinesis carries a fixed
hourly fee that batch stats have no use for.

## What it does

Every `SHIP_INTERVAL` (default `1h`), scan `LOG_DIR` for files Caddy's
roller has rolled — `access-<timestamp>.log`, `.log.gz` when Caddy has
compressed it (a plain `.log` may still be awaiting Caddy's compressor,
which is why a `.gz` with a `.log` sibling is skipped and nothing younger
than `MinAge` ships) — gzip the ones that need it, and PUT each to

```
s3://$S3_BUCKET/logs/source=$LOG_SOURCE/dt=YYYY-MM-DD/<filename>.gz
```

partitioned so DuckDB / Athena / Spark read it directly. The date is the
roll timestamp in the filename. A file is deleted only after its upload
returned 200; a failed file stays for the next pass. The live `access.log`
and anything that is not a rolled log are never touched.

## What it deliberately is not

- **No AWS SDK.** The one call is a header-signed PutObject; SigV4 is four
  HMACs and some canonicalization, pinned in `sigv4_test.go` against AWS's
  published worked example. The SDK tree is ~20 modules bought for one
  request shape.
- **No retention logic.** Retention on disk is "uploaded means deleted";
  retention in S3 is the bucket's lifecycle policy, not this program's.

## Configuration

| Variable | Meaning |
| --- | --- |
| `S3_BUCKET` | Destination bucket (required) |
| `S3_REGION` | Bucket's region (required) |
| `AWS_ACCESS_KEY_ID` / `AWS_SECRET_ACCESS_KEY` | The stats IAM user: `s3:PutObject` for this shipper, plus `s3:GetObject`/`s3:ListBucket` for the aggregator, all scoped to the bucket's `logs/*` prefix (required) |
| `LOG_DIR` | Directory Caddy rolls into (`/var/log/caddy` in compose) |
| `LOG_SOURCE` | Partition label, default `caddy` |
| `SHIP_INTERVAL` | Go duration between passes, default `1h` |

## Deploying

The compose service is behind the `stats` profile: a default `up -d` does
not start it, because without credentials it would fail at startup by
design. Once the bucket and the put-only IAM user exist and
`STATS_AWS_ACCESS_KEY_ID`, `STATS_AWS_SECRET_ACCESS_KEY`, `STATS_S3_BUCKET`
and `STATS_S3_REGION` are in `~/.env` on the host:

```
COMPOSE_PROFILES=stats
```

in `~/.env` (the permanent opt-in — deploy.sh's unflagged `up -d` then
includes the profile on every deploy; a one-off `docker compose --profile
stats up -d` works but stops being updated by later deploys).

The mount is read-write on purpose — deletion after upload is what keeps
the host disk bounded once shipping owns retention. Caddy's `roll_keep`
still matters whenever the shipper is down **or failing**: after five
further rolls Caddy deletes the oldest unshipped file, and that deletion
is silent. A persistently failing upload logs an error every pass; that
log is the only warning before data ages out.
