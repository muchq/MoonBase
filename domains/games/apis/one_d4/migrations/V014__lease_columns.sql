-- Explicit ownership, replacing the inference that a request whose
-- updated_at has not moved must be abandoned. owner_id names the worker
-- that holds the request; lease_expires_at is how long that claim is good
-- for without renewal. Both NULL means nobody has claimed it yet — which is
-- a different state from "claimed and gone quiet", and the sweep now has to
-- tell them apart.
--
-- Nullable with no backfill on purpose. Rows already in flight when this
-- deploys have no owner, so they read as unclaimed and fall to the orphan
-- clock, which is exactly how they were being handled before this column
-- existed. Anything else would mean inventing an owner for a worker this
-- process cannot see.

ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS owner_id VARCHAR(128);

ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS lease_expires_at TIMESTAMP;

-- The reclaim sweep looks for live rows whose lease has run out, and the
-- orphan sweep for live rows that never had one. Both filter on status plus
-- a lease column every hour.

CREATE INDEX IF NOT EXISTS idx_indexing_requests_lease ON indexing_requests(status, lease_expires_at);
