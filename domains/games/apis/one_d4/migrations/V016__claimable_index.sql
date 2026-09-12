-- What claimNext scans: the oldest live row nobody currently holds, on a
-- query every instance runs every few seconds. Ordered by created_at
-- because the queue it replaces was FIFO, and a poller that skipped ahead
-- would starve the front under sustained load.
--
-- Partial on Postgres, and that is not a preference. Postgres before 17
-- cannot emit btree output already ordered on a trailing column when the
-- leading one sits under a ScalarArrayOp, which status IN (...) is — so a
-- composite (status, created_at) is never chosen, and forcing it still
-- produces a full top-N sort of every live row. Measured at 200k rows with
-- 10k live, the partial index planned three orders of magnitude cheaper.
-- Worth pinning explicitly because CI runs postgres:18, where ordered SAOP
-- scans do exist and the composite would have looked perfectly healthy.

CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable ON indexing_requests(created_at) WHERE status IN ('PENDING', 'PROCESSING');
