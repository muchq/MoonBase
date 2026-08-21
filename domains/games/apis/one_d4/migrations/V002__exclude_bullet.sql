-- exclude_bullet on both request and period rows, so a request that filters
-- bullet games and one that does not are distinct work and distinct cache
-- entries. Existing rows get false (bullet games included), matching
-- pre-existing behavior. The unique constraint that keys the period cache by
-- the new column follows in V003, where the engines fork.

ALTER TABLE indexing_requests ADD COLUMN IF NOT EXISTS exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE;

ALTER TABLE indexed_periods ADD COLUMN IF NOT EXISTS exclude_bullet BOOLEAN NOT NULL DEFAULT FALSE;
