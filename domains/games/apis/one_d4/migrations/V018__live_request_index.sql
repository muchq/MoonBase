-- One live request per (player, platform, start_month, end_month,
-- exclude_bullet), said directly: a partial unique index over the live
-- statuses. V009 carried the same invariant in a nullable concatenated
-- column because H2 has no partial unique index and H2 was the default test
-- engine; #1532 removed H2 as a schema engine, so the index is the whole
-- mechanism and no writer has to remember to NULL a key on its way to a
-- terminal status.
--
-- V009 and V010 still run ahead of this, rebuilding the column and its
-- constraint for the few statements it takes this step to remove them again.
-- The steps are append-only and each is cheap here; what matters is the
-- schema this leaves.

-- V009's losers kept a NULL key while staying PENDING, so a range can hold
-- several live rows and the index below would reject them. Retire all but the
-- oldest: nothing is working on them, they hold no slot, and the reclaim
-- sweep would retire them on its own clock. The (created_at, id) order is
-- findExistingRequest's, so the row that survives here is the row a resubmit
-- attaches to.

UPDATE indexing_requests r
SET status = 'FAILED',
    error_message = 'Superseded: another request already held this range.',
    updated_at = now(),
    owner_id = NULL,
    lease_expires_at = NULL
WHERE r.status IN ('PENDING', 'PROCESSING')
  AND EXISTS (
    SELECT 1 FROM indexing_requests w
    WHERE w.player = r.player AND w.platform = r.platform
      AND w.start_month = r.start_month AND w.end_month = r.end_month
      AND w.exclude_bullet = r.exclude_bullet
      AND w.status IN ('PENDING', 'PROCESSING')
      AND (w.created_at < r.created_at
           OR (w.created_at = r.created_at AND w.id < r.id)));

CREATE UNIQUE INDEX IF NOT EXISTS idx_indexing_requests_live
  ON indexing_requests (player, platform, start_month, end_month, exclude_bullet)
  WHERE status IN ('PENDING', 'PROCESSING');

ALTER TABLE indexing_requests DROP CONSTRAINT IF EXISTS indexing_requests_dedupe_unique;

ALTER TABLE indexing_requests DROP COLUMN IF EXISTS dedupe_key;
