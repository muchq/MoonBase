-- Re-key the period cache's unique constraint by (player, platform,
-- year_month, exclude_bullet). Postgres must drop the old 3-column constraint
-- before adding the 4-column one, and has no ADD CONSTRAINT IF NOT EXISTS, so
-- the add runs in a DO block that swallows the already-exists errors.

ALTER TABLE indexed_periods DROP CONSTRAINT IF EXISTS indexed_periods_player_platform_year_month_key;

DO $$ BEGIN
  ALTER TABLE indexed_periods ADD CONSTRAINT indexed_periods_unique
    UNIQUE (player, platform, year_month, exclude_bullet);
EXCEPTION WHEN duplicate_table OR duplicate_object THEN NULL;
END $$;
