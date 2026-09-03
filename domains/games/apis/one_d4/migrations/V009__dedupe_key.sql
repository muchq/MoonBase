-- dedupe_key enforces "at most one live request per (player, platform,
-- start_month, end_month, exclude_bullet)" in the database, closing the
-- check-then-act race in IndexRequestService (#1249). It carries the
-- composite key while a request is PENDING/PROCESSING and NULL once it
-- reaches a terminal status — a plain UNIQUE constraint ignores NULLs on
-- both engines, so terminal rows accumulate freely while live ones cannot
-- collide.
--
-- A Postgres partial unique index would express this more directly, but H2
-- does not support one, and H2 is what the default CI suite runs — the
-- pg-backed suites skip silently without GAMES_HUB_TEST_DB_URL /
-- PG_TEST_DB_URL. A PG-only constraint would leave the race guard
-- effectively untested on every ordinary PR. One nullable column costs a
-- little schema surface and buys the same invariant, enforced identically
-- on both engines and exercised by db_tests.

ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS dedupe_key VARCHAR(600);

-- The backfill keys exactly one live row per group, in a single statement,
-- before the unique constraint (V010) exists. The pre-constraint schema
-- permitted duplicate live rows, so any group holding several would fail the
-- ADD CONSTRAINT if they were all keyed. Losers keep dedupe_key NULL: they
-- stay visible and keep their status, they simply hold no slot, and the
-- staleness reclamation retires them on its own clock.
--
-- The (created_at, id) order here is the same one findExistingRequest uses,
-- and they have to stay that way: this picks which duplicate holds the slot,
-- that picks which one a submit attaches to, and if they disagree a caller
-- can be handed a row nobody is working on while the keyed row does the
-- work. created_at alone does not settle it — ties are exactly what
-- duplicate submits produce.
--
-- Two details, both learned by watching this fail:
--
-- LOWER(CAST(... AS VARCHAR)) rather than bare concatenation, because H2
-- renders a BOOLEAN as 'TRUE' while Postgres and Boolean.toString render
-- 'true'. Left implicit, a backfilled row's key would not match the one
-- IndexingRequestDao computes in Java on H2, and dedupe would miss it
-- exactly once.
--
-- And the winner is picked by a total order — (created_at, id), with ids
-- compared by '<' rather than MIN(id), since Postgres has ordering operators
-- for uuid but no min/max aggregate over it. Selecting on MIN(created_at)
-- alone is not enough: duplicate submits are precisely the rows most likely
-- to share a timestamp, a tie makes MIN match every row in the group, and
-- the statement then tries to write one key onto several rows. Cleaning that
-- up afterwards is too late — on an upgrade where the constraint already
-- exists the UPDATE itself is rejected, and on a first run ADD CONSTRAINT
-- is. Either way the migration aborts, so the tiebreak has to be part of the
-- selection rather than a follow-up pass.
--
-- The IS NULL guards make re-running the migration a no-op.

UPDATE indexing_requests r
SET dedupe_key = r.platform || '|' || r.start_month || '|' || r.end_month || '|'
                 || LOWER(CAST(r.exclude_bullet AS VARCHAR)) || '|' || r.player
WHERE r.status IN ('PENDING', 'PROCESSING')
  AND r.dedupe_key IS NULL
  AND NOT EXISTS (
    SELECT 1 FROM indexing_requests h
    WHERE h.player = r.player AND h.platform = r.platform
      AND h.start_month = r.start_month AND h.end_month = r.end_month
      AND h.exclude_bullet = r.exclude_bullet
      AND h.status IN ('PENDING', 'PROCESSING')
      AND h.dedupe_key IS NOT NULL)
  AND NOT EXISTS (
    SELECT 1 FROM indexing_requests o
    WHERE o.player = r.player AND o.platform = r.platform
      AND o.start_month = r.start_month AND o.end_month = r.end_month
      AND o.exclude_bullet = r.exclude_bullet
      AND o.status IN ('PENDING', 'PROCESSING')
      AND o.dedupe_key IS NULL
      AND (o.created_at < r.created_at
           OR (o.created_at = r.created_at AND o.id < r.id)));
