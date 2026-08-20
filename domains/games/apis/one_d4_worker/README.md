# one_d4_worker — the C++ index worker

Claims a range off `indexing_requests`, reads the months from chess.com,
extracts features and motifs, and writes `game_features`,
`motif_occurrences` and `indexed_periods`. Tracking issue: MoonBase#1389.

It runs *beside* the Java worker rather than instead of it. The table is
the queue (#1279), so both poll the same rows under the same claims,
leases and fences. It creates no schema — one_d4 owns the migrations and
must be up first.

```bash
ONE_D4_DB_URL=postgresql://... bazel run //domains/games/apis/one_d4_worker
kill -TERM <pid>   # drains the runs in flight and exits 0
```

## The second queue: reanalysis

Each instance also polls `reanalysis_requests` on a thread of its own
(#1389 phase 5). A pass re-extracts every stored game against the current
detectors — same code the index path runs, on `pgn` already in the table,
so no chess.com call and no network at all.

**Its own table, not a job type on `indexing_requests`.** Both indexers
claim from that table with no job-type predicate, so a reanalysis row
there is one an indexer takes, finds no player or months on, and fails —
spending an attempt on work it cannot do. Separate tables make that
unreachable rather than a filter every present and future poller has to
remember.

**Its own thread, not a pool slot.** A pass runs for hours; a slot spent
on one is a slot not indexing. The queue hands out at most one pass at a
time anyway, since a pass is a single-owner walk of the whole corpus.

**It resumes.** Progress writes the last `game_url` a finished page
covered along with the counts, in one statement, so a pass that loses its
lease or hits the six-hour ceiling picks up where it stopped instead of
walking the corpus again. That is also why the ceiling refunds the
attempt when the cursor moved: a pass making progress on a large corpus
is not a wedged one, and spending an attempt each time would retire the
request after three before it ever reached the end. A ceiling that moved
*nothing* still spends it.

Scaling out does not speed a pass up — one owner at a time is the point —
but it does mean any instance can pick up a pass whose owner died.

## Scaling out

There is no ingress. Nothing routes to this service, nothing load
balances it, and no replica is addressable — every instance finds its own
work by claiming rows. That makes scaling out unusually cheap, and it
means the two axes are independent.

**Within a process: `ONE_D4_INDEX_SLOTS`** (default 4, capped at 16).
Each slot is a thread that claims a request and runs it. A run is mostly
waiting on chess.com and on Postgres, so size this for concurrent calls
rather than for the CPU cap.

**Across processes: replicas.** Start another container. There is nothing
to configure and nothing to tell it about its siblings — no leader, no
partitioning, no shard map, no coordination of any kind.

`deploy/consolidated/compose.yaml` sets no `replicas` today, so this is
the untried axis of the two. The service is shaped for it — no
`container_name`, no ports, no ingress — and the deploy runs `docker
compose up -d`, which honours `deploy.replicas`:

```yaml
one_d4_worker:
  deploy:
    replicas: 3          # alongside the resources.limits already there
```

Both axes multiply: three replicas of four slots is twelve requests at
once — and the Postgres connections are the ceiling to check first.
Each slot holds two (its queue and its run's sink), and each instance
adds one more for the reanalysis poller plus a second while a pass is
actually running: four slots is 8–10 per instance, three replicas 27–30.

### Why that needs no coordination

`ClaimNext` is one conditional `UPDATE` whose candidate is chosen `FOR
UPDATE SKIP LOCKED`, so two claimants racing for a row cannot both win —
the row lock decides and the loser's `WHERE` no longer matches. A claim
somebody holds live is not a candidate at all. Every write after the
claim — heartbeat, progress, the terminal write, and the sink's own
writes — is fenced on the `owner_id` that claimed it, and each *run*
claims under a token of its own, so a wedged run cannot be mistaken for
its replacement.

That is what makes a replica safe to add or remove at any moment. A
worker that dies mid-run strands its claim only until the lease expires
(5 minutes), after which any other worker takes the range and spends an
attempt on it; three attempts retire the request.

### What actually limits it

Scaling out is cheap, not free. The three ceilings, in the order you will
hit them:

| | cost per slot | where it bites |
|---|---|---|
| Postgres connections | 2 — one to claim and renew over, one to flush over | `max_connections` is 100 by default, shared with one_d4 and golf_hub |
| chess.com requests | 1 concurrent | a run fetches one month at a time, so `slots × replicas` is the concurrency against a rate-limited API |
| CPU | fraction of one | PGN replay and motif detection; `cpus: '0.5'` in compose bounds it |

Neither connection may be shared. A heartbeat queued behind a flush is a
lease lost under a healthy run, and one `pg::Client` is one connection
serialised by a mutex.

### Reading the log

Every run's lines are tied together by the request id, which matters
because slots interleave:

```
Claimed 4f3c… hikaru 2026-01..2026-03 as cpp/indexer-7/1234/9a1f…
Finished 4f3c… completed in 84213ms
```

A reanalysis pass says where it resumed from and what it got through:

```
Claimed reanalysis request_id=b925… from=start owner=cpp/indexer-7/1234/3fc5…
Finished reanalysis request_id=b925… processed=2 failed=1
```

`from=start` is a fresh pass; anything else is the cursor it resumed at.
`failed` counts games whose stored PGN would not replay — those still get
their occurrences cleared, because a game whose second look finds nothing
must not keep the rows it had.

`Draining N indexing threads` on the way out means shutdown is waiting
for runs in flight — expected, and it can take as long as a chess.com
call a run is already inside, which is why the stop grace is 240s.

## Configuration

| | |
|---|---|
| `ONE_D4_DB_URL` | required; no default, the worker exits 1 without it |
| `ONE_D4_INDEX_SLOTS` | requests at once, default 4, capped at 16 |
| `ONE_D4_POLL_SECONDS` | how long to wait before asking an empty queue again, default 5 |

The lease, its renewal interval and the run ceiling are deliberately not
configurable. They are protocol constants of the queue rather than
deployment knobs — two pollers that disagree about them misbehave against
each other — and `schema_contract_test` holds the C++ values equal to
`RetentionPolicy`'s.
