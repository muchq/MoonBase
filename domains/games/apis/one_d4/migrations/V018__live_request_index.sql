-- One live request per (player, platform, start_month, end_month,
-- exclude_bullet), said directly: a partial unique index over the live
-- statuses. V009 carried the same invariant in a nullable concatenated
-- column; the index is the whole mechanism now, and a terminal status
-- releases the range without any writer remembering to NULL a key.
--
-- V009 and V010 still run ahead of this, rebuilding the column and its
-- constraint for the few statements it takes this step to remove them again.
-- The steps are append-only and each is cheap. The cost that is not free:
-- Postgres never reuses the attnum of a dropped column, so every migration
-- pass spends one of indexing_requests' 1600 column slots. At roughly 1,580
-- passes ADD COLUMN starts failing, and the fix then is to rebuild the table.
--
-- This step also drops a column the immediately preceding release still
-- writes, and the append-only set has no down step to restore it: while the
-- old containers are still up they take 42703 on every write naming it, and
-- a rollback to that image fails its own boot verification. #1532 accepted
-- both against a beta with no stored state worth preserving.

-- A range can hold several live rows before this step: V009 keyed one and
-- left the rest NULL, and a row holding no key was still claimable, so the
-- index below would reject them. Exactly one per range survives.
--
-- The survivor is the row a worker is running, if there is one. Age alone
-- would retire a leased run in favour of an older idle duplicate — clearing
-- its owner mid-flush, which no reclaim arm ever does — and the oldest row is
-- not the one that holds the range once a terminal write has freed V009's key
-- and a resubmit has taken it. ROW_NUMBER because the ordering has to be a
-- total order per range: two rows tied at rank 1 abort the CREATE below, and
-- no rank 1 leaves every duplicate live.

WITH ranked AS (
    SELECT id,
           ROW_NUMBER() OVER (
               PARTITION BY player, platform, start_month, end_month, exclude_bullet
               ORDER BY (owner_id IS NOT NULL AND lease_expires_at > now()) DESC,
                        created_at,
                        id
           ) AS live_rank
      FROM indexing_requests
     WHERE status IN ('PENDING', 'PROCESSING')
)
UPDATE indexing_requests r
   SET status = 'FAILED',
       error_message = 'Superseded: another request already held this range.',
       -- Surrendered with the status, so the old release's key lookup — which
       -- has no status filter, correct while terminal writes always cleared it
       -- — cannot hand this row back during the statements before DROP COLUMN.
       dedupe_key = NULL,
       updated_at = now(),
       owner_id = NULL,
       lease_expires_at = NULL
  FROM ranked
 WHERE ranked.id = r.id
   AND ranked.live_rank > 1;

CREATE UNIQUE INDEX IF NOT EXISTS idx_indexing_requests_live
  ON indexing_requests (player, platform, start_month, end_month, exclude_bullet)
  WHERE status IN ('PENDING', 'PROCESSING');

ALTER TABLE indexing_requests DROP CONSTRAINT IF EXISTS indexing_requests_dedupe_unique;

ALTER TABLE indexing_requests DROP COLUMN IF EXISTS dedupe_key;
