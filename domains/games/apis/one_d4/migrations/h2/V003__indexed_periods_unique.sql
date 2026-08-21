-- Re-key the period cache's unique constraint by (player, platform,
-- year_month, exclude_bullet). H2 supports ADD CONSTRAINT IF NOT EXISTS
-- directly, and never carried the old 3-column constraint name Postgres has
-- to drop first.

ALTER TABLE indexed_periods ADD CONSTRAINT IF NOT EXISTS indexed_periods_unique UNIQUE (player, platform, year_month, exclude_bullet);
