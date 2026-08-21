-- The live-request dedupe constraint (see V009). H2 supports
-- ADD CONSTRAINT IF NOT EXISTS directly.

ALTER TABLE indexing_requests ADD CONSTRAINT IF NOT EXISTS indexing_requests_dedupe_unique UNIQUE (dedupe_key);
