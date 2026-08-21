-- The same index for H2, which has no partial indexes. The composite is
-- what a partial index approximates there, and H2 is the test engine rather
-- than the deployment target, so the cost of it being the weaker plan is a
-- slower test rather than a slower production poll.

CREATE INDEX IF NOT EXISTS idx_indexing_requests_claimable ON indexing_requests(status, created_at);
